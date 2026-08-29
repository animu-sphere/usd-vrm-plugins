// SPDX-License-Identifier: Apache-2.0
//
// The address inventory: what a session contains, counted from bytes.
//
// Every datagram here is built byte by byte, because that is the only way to
// test a measurement — a fixture written by this repository's own encoder would
// let the inventory agree with itself. The addresses below are *data*. Some of
// them look like the VRChat OSC surface, one looks like a different part of it,
// and one is deliberately something no document predicts, because the claim
// this file makes is that the inventory has no list of addresses it expects.
// Delete every plausible-looking address from these payloads and the tests
// would still say the same thing.
//
// The suite that describes OSC itself is `libs/osc`'s. What is checked here is
// this adapter's two contributions: the grouping, and the map from a neutral
// refusal onto `VRM_VRCHAT_OSC_PACKET_MALFORMED`.
#include "vrmAdapterVrchatOsc/AddressInventory.h"

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/PacketCapture.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using vrmAdapterVrchatOsc::AddressInventory;
using vrmAdapterVrchatOsc::AddressRow;
using vrmAdapterVrchatOsc::DiagnosticCode;
using vrmAdapterVrchatOsc::PacketCapture;
using vrmAdapterVrchatOsc::RecordedDatagram;

// ---------------------------------------------------------------------------
// Byte assembly. Big-endian throughout, like the wire.
// ---------------------------------------------------------------------------

struct Bytes
{
    std::vector<std::uint8_t> data;

    Bytes& Str(std::string_view text)
    {
        data.insert(data.end(), text.begin(), text.end());
        data.push_back(0);
        while (data.size() % 4 != 0) {
            data.push_back(0);
        }
        return *this;
    }

    Bytes& U32(std::uint32_t value)
    {
        data.push_back(static_cast<std::uint8_t>(value >> 24));
        data.push_back(static_cast<std::uint8_t>(value >> 16));
        data.push_back(static_cast<std::uint8_t>(value >> 8));
        data.push_back(static_cast<std::uint8_t>(value));
        return *this;
    }

    Bytes& U64(std::uint64_t value)
    {
        U32(static_cast<std::uint32_t>(value >> 32));
        return U32(static_cast<std::uint32_t>(value));
    }

    Bytes& F32(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return U32(bits);
    }

    Bytes& Append(const std::vector<std::uint8_t>& values)
    {
        data.insert(data.end(), values.begin(), values.end());
        return *this;
    }
};

std::vector<std::uint8_t>
Message(std::string_view address, const std::vector<float>& values)
{
    Bytes out;
    out.Str(address);
    out.Str("," + std::string(values.size(), 'f'));
    for (float value : values) {
        out.F32(value);
    }
    return out.data;
}

std::vector<std::uint8_t>
Bundle(const std::vector<std::vector<std::uint8_t>>& elements)
{
    Bytes out;
    out.Str("#bundle");
    out.U64(1);
    for (const std::vector<std::uint8_t>& element : elements) {
        out.U32(static_cast<std::uint32_t>(element.size()));
        out.Append(element);
    }
    return out.data;
}

void
Record(PacketCapture* capture, double time, std::vector<std::uint8_t> bytes)
{
    RecordedDatagram datagram;
    datagram.receiveTime = time;
    datagram.bytes = std::move(bytes);
    capture->datagrams.push_back(std::move(datagram));
}

const AddressRow*
Find(const AddressInventory& inventory, std::string_view address,
     std::string_view tags)
{
    for (const AddressRow& row : inventory.rows) {
        if (row.address == address && row.typeTags == tags) {
            return &row;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------

void
TestEveryAddressAppearsWhetherOrNotAnybodyExpectedIt()
{
    PacketCapture capture;
    capture.peerEndpoint = "192.168.0.20:52001";
    for (int frame = 0; frame < 3; ++frame) {
        const double time = 0.1 * frame;
        Record(&capture, time,
               Bundle({Message("/tracking/trackers/1/position",
                               {0.0f, 1.0f, 0.0f}),
                       Message("/tracking/trackers/1/rotation",
                               {0.0f, 0.0f, 0.0f}),
                       // A different part of the same sender's surface.
                       Message("/avatar/parameters/VelocityY", {0.5f}),
                       // And one nothing in any document predicts. This is the
                       // row that makes the point: the inventory reports what
                       // arrived, not what it was looking for.
                       Message("/an/address/nobody/documented", {1.0f})}));
    }

    const AddressInventory inventory =
        vrmAdapterVrchatOsc::InventoryAddresses(capture);

    assert(inventory.datagrams == 3);
    assert(inventory.decoded == 3);
    assert(inventory.refused == 0);
    assert(inventory.bundled == 3);
    assert(inventory.messages == 12);
    assert(inventory.rows.size() == 4);

    const AddressRow* undocumented =
        Find(inventory, "/an/address/nobody/documented", "f");
    assert(undocumented);
    assert(undocumented->messages == 3);
    assert(undocumented->datagrams == 3);

    // The counts are of datagrams and are not derived from the rows, so this
    // addition is a check on the inventory rather than a restatement of it.
    std::size_t summed = 0;
    for (const AddressRow& row : inventory.rows) {
        summed += row.messages;
    }
    assert(summed == inventory.messages);

    // Sorted by address, then by tags. An operator diffs one session's report
    // against the next's, and a report whose order came out of a hash table
    // could not be diffed at all.
    for (std::size_t i = 1; i < inventory.rows.size(); ++i) {
        const AddressRow& previous = inventory.rows[i - 1];
        const AddressRow& current = inventory.rows[i];
        assert(previous.address < current.address
               || (previous.address == current.address
                   && previous.typeTags < current.typeTags));
    }
}

void
TestOneAddressWithTwoTypeTagsIsTwoRows()
{
    // The finding this row shape exists to make visible: a sender that spells
    // one address two ways. A table keyed on the address alone would average
    // them into one row and hide exactly the thing a decoder has to be built
    // around.
    PacketCapture capture;
    Record(&capture, 0.0,
           Message("/tracking/trackers/head/position", {0.0f, 1.7f, 0.0f}));
    Record(&capture, 1.0, Message("/tracking/trackers/head/position", {1.7f}));
    Record(&capture, 2.0,
           Message("/tracking/trackers/head/position", {0.0f, 1.7f, 0.1f}));

    const AddressInventory inventory =
        vrmAdapterVrchatOsc::InventoryAddresses(capture);

    assert(inventory.rows.size() == 2);
    const AddressRow* three =
        Find(inventory, "/tracking/trackers/head/position", "fff");
    const AddressRow* one =
        Find(inventory, "/tracking/trackers/head/position", "f");
    assert(three && one);
    assert(three->messages == 2);
    assert(one->messages == 1);

    // First and last are the row's own, not the session's: the short-form row
    // stops in the middle, and a row that stopped part-way through a session is
    // visible as one.
    assert(three->firstTime == 0.0);
    assert(three->lastTime == 2.0);
    assert(one->firstTime == 1.0);
    assert(one->lastTime == 1.0);
}

void
TestARepeatedAddressInOneBundleCountsOneDatagram()
{
    // `messages` and `datagrams` disagree exactly when a sender repeats an
    // address inside one bundle, and that disagreement is itself the finding --
    // it is what a frame assembler has to have a policy about (VRC-4).
    PacketCapture capture;
    Record(&capture, 0.0,
           Bundle({Message("/tracking/trackers/2/position", {0.0f, 0.0f, 0.0f}),
                   Message("/tracking/trackers/2/position",
                           {0.1f, 0.0f, 0.0f})}));

    const AddressInventory inventory =
        vrmAdapterVrchatOsc::InventoryAddresses(capture);

    assert(inventory.rows.size() == 1);
    assert(inventory.rows.front().messages == 2);
    assert(inventory.rows.front().datagrams == 1);
    assert(inventory.messages == 2);
    assert(inventory.datagrams == 1);
}

void
TestARefusalArrivesAsThisAdaptersCode()
{
    // The other half of what this file exists to prove. `libs/osc` refuses a
    // datagram with a subject and a detail and no code; the code is this
    // adapter's, and `vrmAdapterVmc` turns the same refusal into a different
    // one. Two adapters mapping one neutral refusal onto two frozen sets is
    // what the shared decoder was extracted for.
    PacketCapture capture;
    capture.peerEndpoint = "192.168.0.20:52001";
    Record(&capture, 0.5, {'/', 'a', 'b'});
    // A well-formed one either side, so a refusal is shown not to poison the
    // session: the port is a well-known one and anything on the network may
    // send to it.
    Record(&capture, 1.0,
           Message("/tracking/trackers/1/position", {0.0f, 1.0f, 0.0f}));
    Record(&capture, 1.5, {0xde, 0xad, 0xbe, 0xef});

    const AddressInventory inventory =
        vrmAdapterVrchatOsc::InventoryAddresses(capture);

    assert(inventory.datagrams == 3);
    assert(inventory.decoded == 1);
    assert(inventory.refused == 2);
    assert(inventory.rows.size() == 1);
    assert(inventory.diagnostics.size() == 2);

    for (const auto& diagnostic : inventory.diagnostics) {
        assert(diagnostic.code == DiagnosticCode::PacketMalformed);
        assert(diagnostic.recoverable);
        // The shared decoder's own text, carried across rather than reworded.
        assert(diagnostic.detail.find("at byte") != std::string::npos);
        assert(diagnostic.source == "192.168.0.20:52001");
        assert(diagnostic.timestamp);
    }
    assert(*inventory.diagnostics.front().timestamp == 0.5);
    assert(*inventory.diagnostics.back().timestamp == 1.5);
    assert(vrmAdapterVrchatOsc::FormatDiagnostic(inventory.diagnostics.front())
               .find("[VRM_VRCHAT_OSC_PACKET_MALFORMED]") == 0);
}

void
TestAnEmptyCaptureIsAnInventoryAndNotAnError()
{
    const AddressInventory inventory =
        vrmAdapterVrchatOsc::InventoryAddresses(PacketCapture());
    assert(inventory.datagrams == 0);
    assert(inventory.rows.empty());
    assert(inventory.diagnostics.empty());

    // And a session that carried only traffic this decoder refuses is an
    // inventory with no rows and two diagnostics -- a finding, not a failure.
    PacketCapture noise;
    Record(&noise, 0.0, {1, 2, 3, 4});
    Record(&noise, 0.1, {});
    const AddressInventory refused =
        vrmAdapterVrchatOsc::InventoryAddresses(noise);
    assert(refused.datagrams == 2);
    assert(refused.decoded == 0);
    assert(refused.rows.empty());
    assert(refused.diagnostics.size() == 2);
}

} // namespace

int
main()
{
    TestEveryAddressAppearsWhetherOrNotAnybodyExpectedIt();
    TestOneAddressWithTwoTypeTagsIsTwoRows();
    TestARepeatedAddressInOneBundleCountsOneDatagram();
    TestARefusalArrivesAsThisAdaptersCode();
    TestAnEmptyCaptureIsAnInventoryAndNotAnError();
    std::puts("vrmAdapterVrchatOsc address inventory tests passed");
    return 0;
}
