// SPDX-License-Identifier: Apache-2.0
//
// The two decisions `UdpReceiver::Receive` makes around its `poll`, tested.
//
// This file is a debt being paid rather than a new suite. Two of OSC-1's four
// fixes shipped without a test, and the obstacle was structural: a poll timeout
// of `-1` and one of `INT_MAX` differ only after 24.8 days, and a wake-up
// reporting `POLLERR` instead of a datagram is not producible on three
// platforms from a suite that owns only its own sockets — an unconnected UDP
// socket collects no ICMP error, and `POLLNVAL` needs a descriptor closed
// underneath a poll already running, which is the race `UdpReceiver` documents
// as unsupported. A test that passed against the defect would have been worse
// than none.
//
// The honest seam was named there too: a unit test of the mapping and of the
// wake-up predicate, in a library that can hold an internal header without
// either adapter growing an API. That library is this one.
//
// The `readableBit` here is a synthetic 1, not `POLLIN`. That is the point —
// the predicate's content is "the bit the poll asked for, and nothing else,
// means a datagram", and no platform header is needed to state it or to try
// every combination that is not it.
#include "PollTimeout.h"

#include <cassert>
#include <cstdio>
#include <limits>

namespace
{

using liveTransport::internal::ClassifyPollWakeUp;
using liveTransport::internal::kPollForever;
using liveTransport::internal::kPollMaxMilliseconds;
using liveTransport::internal::PollWakeUp;
using liveTransport::internal::TimeoutToMilliseconds;

// The defect this replaces: a finite request at or above INT_MAX milliseconds
// was mapped onto -1, which both `poll` and `WSAPoll` read as "wait forever".
// A caller that asked to wait 25 days and got an unbounded wait has no way to
// stop, which is the exact opposite of what it asked for.
void
TestLargeFiniteTimeoutsAreClampedNotMadeIndefinite()
{
    // Exactly the boundary, in seconds.
    const double boundary = static_cast<double>(kPollMaxMilliseconds) / 1000.0;
    assert(TimeoutToMilliseconds(boundary) == kPollMaxMilliseconds);
    assert(TimeoutToMilliseconds(boundary * 2.0) == kPollMaxMilliseconds);
    assert(TimeoutToMilliseconds(1.0e9) == kPollMaxMilliseconds);
    assert(TimeoutToMilliseconds(std::numeric_limits<double>::max())
           == kPollMaxMilliseconds);
    assert(TimeoutToMilliseconds(std::numeric_limits<double>::infinity())
           == kPollMaxMilliseconds);

    // None of them is the sentinel, which is the whole assertion.
    assert(TimeoutToMilliseconds(boundary) != kPollForever);
    assert(TimeoutToMilliseconds(1.0e9) != kPollForever);
}

// Only a negative request may produce the sentinel, and every negative one
// does: a caller that asks to wait indefinitely is the one caller entitled to.
void
TestOnlyANegativeRequestWaitsForever()
{
    assert(TimeoutToMilliseconds(-1.0) == kPollForever);
    assert(TimeoutToMilliseconds(-0.001) == kPollForever);
    assert(TimeoutToMilliseconds(-std::numeric_limits<double>::infinity())
           == kPollForever);

    // Zero is a poll, not a wait, and is emphatically not the sentinel.
    assert(TimeoutToMilliseconds(0.0) == 0);
}

// Rounded up rather than truncated, so a timeout below the platform's
// resolution waits a tick instead of degenerating into a spin. The failure this
// prevents is a caller in a tight loop asking for 100 microseconds and getting
// a zero-timeout poll every time.
void
TestSubMillisecondTimeoutsWaitATick()
{
    assert(TimeoutToMilliseconds(0.0001) == 1);
    assert(TimeoutToMilliseconds(0.0009) == 1);
    assert(TimeoutToMilliseconds(0.001) == 1);
    assert(TimeoutToMilliseconds(0.0011) == 2);

    // Ordinary values are still the number a caller would compute by hand.
    assert(TimeoutToMilliseconds(0.25) == 250);
    assert(TimeoutToMilliseconds(1.0) == 1000);
    assert(TimeoutToMilliseconds(2.5) == 2500);
}

// The defect this replaces: `revents` was never inspected, so a descriptor that
// reported ready without the readable bit sent the receive loop back to a poll
// that was still ready — forever, at 100% of a core, for a caller that had
// asked to wait indefinitely.
void
TestOnlyTheRequestedBitMeansADatagram()
{
    constexpr int kReadable = 0x0001; // stands in for POLLIN
    constexpr int kError = 0x0008;    // POLLERR
    constexpr int kHangUp = 0x0010;   // POLLHUP
    constexpr int kInvalid = 0x0020;  // POLLNVAL

    assert(ClassifyPollWakeUp(kReadable, kReadable) == PollWakeUp::Readable);

    // The three a platform reports whether or not they were requested. Each is
    // an error condition on a poll that asked for the readable bit alone, and
    // each is a wake-up no test can make a real socket produce on demand.
    assert(ClassifyPollWakeUp(kError, kReadable) == PollWakeUp::ErrorCondition);
    assert(ClassifyPollWakeUp(kHangUp, kReadable) == PollWakeUp::ErrorCondition);
    assert(ClassifyPollWakeUp(kInvalid, kReadable)
           == PollWakeUp::ErrorCondition);
    assert(ClassifyPollWakeUp(kError | kHangUp | kInvalid, kReadable)
           == PollWakeUp::ErrorCondition);

    // Ready with nothing set at all: the same answer, and the one a `revents`
    // left uninitialised would produce.
    assert(ClassifyPollWakeUp(0, kReadable) == PollWakeUp::ErrorCondition);

    // An error condition *alongside* a datagram is still a datagram. The
    // datagram is there to be read, and refusing it would drop traffic on a
    // socket that also happened to report a peer's ICMP reply.
    assert(ClassifyPollWakeUp(kReadable | kError, kReadable)
           == PollWakeUp::Readable);
    assert(ClassifyPollWakeUp(kReadable | kHangUp, kReadable)
           == PollWakeUp::Readable);
}

// `constexpr` is part of the contract rather than an optimisation: the
// classification is a pure function of two integers, and a compile-time
// assertion is the strongest statement available that it stayed one.
void
TestTheClassificationIsAConstantExpression()
{
    static_assert(ClassifyPollWakeUp(1, 1) == PollWakeUp::Readable, "");
    static_assert(ClassifyPollWakeUp(8, 1) == PollWakeUp::ErrorCondition, "");
}

} // namespace

int
main()
{
    TestLargeFiniteTimeoutsAreClampedNotMadeIndefinite();
    TestOnlyANegativeRequestWaitsForever();
    TestSubMillisecondTimeoutsWaitATick();
    TestOnlyTheRequestedBitMeansADatagram();
    TestTheClassificationIsAConstantExpression();
    std::printf("liveTransport poll: timeout mapping and wake-up predicate "
                "verified\n");
    return 0;
}
