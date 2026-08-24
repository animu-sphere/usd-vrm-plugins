// SPDX-License-Identifier: Apache-2.0
//
// Two decisions `UdpReceiver::Receive` makes around its `poll`, pulled out
// where a test can reach them.
//
// This header is the reason OSC-2 could pay a debt OSC-1 could not. Two of that
// step's four fixes shipped without a test, and the obstacle was structural
// rather than lazy: a poll timeout of `-1` and one of `INT_MAX` differ only
// after 24.8 days, and a wake-up reporting `POLLERR` instead of a datagram is
// not producible on three platforms from a suite that owns only its own
// sockets. The honest seam is a unit test of the mapping and of the predicate,
// and writing one inside a single adapter would have meant giving that adapter
// a public function or an internal header its sibling did not have — divergence,
// in the step whose purpose was convergence.
//
// A library can hold an internal header. This is it, and
// `tests/test_poll_timeout.cpp` is the test.
//
// It names no platform. `POLLIN` is passed in as `readableBit` rather than
// included, so this file compiles on a host with no `poll.h` and a test can
// exercise the predicate with synthetic bits.
#pragma once

#include <cstdint>

namespace liveTransport
{
namespace internal
{

// -1 means "wait indefinitely" to both `poll` and `WSAPoll`, so it is a value
// only a negative request may produce. A large *finite* request is clamped to
// the largest wait either will accept instead — around 24.8 days, which no
// caller distinguishes from the 35 it asked for, where an indefinite wait is
// the exact opposite of what it asked for and leaves it with no way to stop.
inline constexpr int kPollForever = -1;
inline constexpr int kPollMaxMilliseconds = 2147483647;

inline int
TimeoutToMilliseconds(double seconds)
{
    if (seconds < 0.0) {
        return kPollForever;
    }
    const double milliseconds = seconds * 1000.0;
    if (milliseconds >= static_cast<double>(kPollMaxMilliseconds)) {
        return kPollMaxMilliseconds;
    }
    // Rounded up, so a timeout smaller than the platform's resolution waits a
    // tick rather than degenerating into a spin.
    return static_cast<int>(milliseconds + 0.999);
}

// What a ready `poll` descriptor actually means.
enum class PollWakeUp : std::uint8_t
{
    // The bit the caller asked for is set: there is a datagram to read.
    Readable,
    // The descriptor reported ready without it. `POLLERR`, `POLLHUP` and
    // `POLLNVAL` are delivered whether or not they were requested, so this is
    // reachable on a poll that asked for `POLLIN` alone.
    ErrorCondition,
};

// The predicate, and it exists to prevent a spin rather than for tidiness. A
// descriptor that reports ready without the readable bit yields nothing to
// `recvfrom`, which then answers "would block"; a caller that asked to wait
// indefinitely has a budget that never runs out, so the loop returns to a poll
// that is still ready, forever, at 100% of a core. Since the readable bit is
// the only event this receiver asks for, a ready descriptor without it is an
// error condition and is reported as one.
constexpr PollWakeUp
ClassifyPollWakeUp(int revents, int readableBit) noexcept
{
    return (revents & readableBit) != 0 ? PollWakeUp::Readable
                                        : PollWakeUp::ErrorCondition;
}

} // namespace internal
} // namespace liveTransport
