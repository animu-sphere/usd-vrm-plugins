// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVrchatOsc/AddressInventory.h"

#include "osc/OscPacket.h"

#include <algorithm>
#include <map>
#include <utility>

namespace vrmAdapterVrchatOsc
{

namespace
{

// The key is the pair, for the reason the header gives: a sender that spells
// one address two ways is a fact a decoder has to be built around, and a table
// keyed on the address alone would average the two.
using RowKey = std::pair<std::string, std::string>;

} // namespace

AddressInventory
InventoryAddresses(const PacketCapture& capture)
{
    AddressInventory inventory;
    inventory.datagrams = capture.datagrams.size();

    std::map<RowKey, AddressRow> rows;

    for (const RecordedDatagram& datagram : capture.datagrams) {
        osc::OscPacket packet;
        osc::OscDecodeError error;
        if (!osc::DecodeOscPacket(datagram.bytes, &packet, &error)) {
            ++inventory.refused;
            // The one thing this adapter adds to the shared decoder, and the
            // half of OSC-3's evidence that the inventory itself cannot supply:
            // a neutral refusal becomes this adapter's frozen code here, and
            // `vrmAdapterVmc` turns the same refusal into a different one.
            Diagnostic diagnostic = MakeDiagnostic(
                DiagnosticCode::PacketMalformed, std::move(error.detail));
            diagnostic.subject = std::move(error.subject);
            diagnostic.source = capture.peerEndpoint;
            diagnostic.timestamp = datagram.receiveTime;
            inventory.diagnostics.push_back(std::move(diagnostic));
            continue;
        }

        ++inventory.decoded;
        inventory.bundled += packet.bundled ? 1 : 0;
        inventory.messages += packet.messages.size();

        // Which keys this datagram touched, so that `datagrams` counts
        // datagrams rather than messages when a bundle repeats an address.
        std::vector<RowKey> touched;
        for (const osc::OscMessage& message : packet.messages) {
            RowKey key(std::string(message.address),
                       std::string(message.typeTags));
            AddressRow& row = rows[key];
            if (row.messages == 0) {
                row.address = key.first;
                row.typeTags = key.second;
                row.firstTime = datagram.receiveTime;
            }
            ++row.messages;
            row.lastTime = datagram.receiveTime;
            touched.push_back(std::move(key));
        }
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()),
                      touched.end());
        for (const RowKey& key : touched) {
            ++rows[key].datagrams;
        }
    }

    // std::map is already ordered by address then tags, which is the order the
    // header promises.
    inventory.rows.reserve(rows.size());
    for (auto& entry : rows) {
        inventory.rows.push_back(std::move(entry.second));
    }
    return inventory;
}

} // namespace vrmAdapterVrchatOsc
