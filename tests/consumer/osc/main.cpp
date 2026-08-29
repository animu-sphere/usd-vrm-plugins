// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `osc` package and calls one
// function from it. Both halves matter: the include proves the package
// installed its header root, the call proves it installed something to link.
//
// This is deliberately not a test of the decoder. `libs/osc/tests/` owns that,
// with the characterisation suite OSC-0 froze; duplicating any of it here would
// make a packaging failure look like a decoder failure the first time this
// fixture went red. What this asks is only: does the address come back.
#include <osc/OscPacket.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

int
main()
{
    // "/a" with an empty type tag string, hand-assembled so the fixture carries
    // no corpus file: OSC pads every string to a four-byte boundary, so this is
    // '/', 'a', '\0', '\0' followed by ',', '\0', '\0', '\0'.
    const std::vector<std::uint8_t> datagram{
        0x2f, 0x61, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00,
    };

    osc::OscPacket packet;
    osc::OscDecodeError error;
    if (!osc::DecodeOscPacket(datagram, &packet, &error)) {
        std::fprintf(stderr, "consumer: decode refused: %s (%s)\n",
                     error.detail.c_str(), error.subject.c_str());
        return 1;
    }
    if (packet.messages.size() != 1 || packet.messages[0].address != "/a") {
        std::fprintf(stderr, "consumer: decoded %zu messages\n",
                     packet.messages.size());
        return 1;
    }

    std::fprintf(stdout, "consumer: decoded %s through the installed package\n",
                 std::string(packet.messages[0].address).c_str());
    return 0;
}
