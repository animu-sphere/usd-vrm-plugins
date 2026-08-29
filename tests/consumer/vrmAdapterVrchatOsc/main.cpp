// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `vrmAdapterVrchatOsc` package and
// calls into it. The include proves the package installed its header root; the
// calls prove it installed something to link -- and here the two halves reach
// different edges, which is why this call and not a smaller one.
//
// `AddressInventory.h` reaches this package's other public headers, and through
// one of them the transport leaf's capture format: that edge is answered at the
// first `#include`. The decoder edge is answered only at the link, because
// nothing in this package's interface names a type from it -- `InventoryAddresses`
// decodes inside the archive member this call pulls in. Both were missing a
// `find_dependency` on the day this track was written, so a fixture that
// exercised one of the two would have measured half of the defect.
//
// This is deliberately not a test of the inventory.
// `adapters/liveCapture/vrchatOsc/tests/` owns the address rows, the type-tag
// keying and the refusals; duplicating any of it here would make a packaging
// failure look like an inventory failure the first time this fixture went red.
// What this asks is only: does a one-datagram capture inventory to one row.
#include <vrmAdapterVrchatOsc/AddressInventory.h>

#include <cstdint>
#include <cstdio>
#include <vector>

int
main()
{
    // "/a" with an empty type tag string, hand-assembled so the fixture carries
    // no capture file: OSC pads every string to a four-byte boundary, so this is
    // '/', 'a', '\0', '\0' followed by ',', '\0', '\0', '\0'.
    vrmAdapterVrchatOsc::RecordedDatagram datagram;
    datagram.receiveTime = 0.5;
    datagram.bytes = std::vector<std::uint8_t>{
        0x2f, 0x61, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00,
    };

    vrmAdapterVrchatOsc::PacketCapture capture;
    capture.datagrams.push_back(datagram);

    const vrmAdapterVrchatOsc::AddressInventory inventory =
        vrmAdapterVrchatOsc::InventoryAddresses(capture);

    if (inventory.datagrams != 1 || inventory.decoded != 1
        || inventory.refused != 0) {
        std::fprintf(stderr, "consumer: inventoried %zu datagrams, %zu decoded, "
                             "%zu refused\n",
                     inventory.datagrams, inventory.decoded, inventory.refused);
        return 1;
    }
    if (inventory.rows.size() != 1 || inventory.rows[0].address != "/a"
        || !inventory.rows[0].typeTags.empty()) {
        std::fprintf(stderr, "consumer: inventoried %zu rows\n",
                     inventory.rows.size());
        return 1;
    }

    std::fprintf(stdout, "consumer: inventoried %s through the installed "
                         "package\n",
                 inventory.rows[0].address.c_str());
    return 0;
}
