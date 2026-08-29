// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `vrmContainer` package and calls
// into it. The include proves the package installed its header root; the call
// proves it installed something to link -- and, because this is the first
// `SHARED` package measured here, that the call *returns* proves it installed
// the half a consumer needs at load time rather than only at link time.
//
// `GlbContainer.h` is the header with something to ask. It pulls the package's
// other public header in with it, so a prefix that installed one and not the
// other fails at the first `#include` instead of at the first use.
//
// This is deliberately not a test of the parser. `libs/vrmContainer/tests/`
// owns the malformed-chunk corpus and every refusal in it; duplicating any of
// it here would make a packaging failure look like a parser failure the first
// time this fixture went red. What this asks is only: does a container parse,
// and does the view it hands back point into the bytes that went in.
#include <vrmContainer/GlbContainer.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

// The smallest well-formed GLB there is, hand-assembled so the fixture carries
// no corpus file: a twelve-byte header, then one JSON chunk whose payload is
// `{}` padded with spaces to the four-byte boundary the format requires.
std::vector<std::byte>
MinimalGlb()
{
    const unsigned char bytes[] = {
        'g',  'l',  'T',  'F',  0x02, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 'J',  'S',  'O',  'N',  '{',  '}',  ' ',  ' ',
    };
    std::vector<std::byte> out(sizeof(bytes));
    std::memcpy(out.data(), bytes, sizeof(bytes));
    return out;
}

} // namespace

int
main()
{
    const std::vector<std::byte> glb = MinimalGlb();
    const vrmContainer::ByteView view(glb.data(), glb.size());

    if (!vrmContainer::HasGlbMagic(view)) {
        std::fprintf(stderr, "consumer: the installed package does not "
                             "recognise a glTF magic\n");
        return 1;
    }

    vrmContainer::GlbView parsed;
    vrmContainer::Error error;
    if (!vrmContainer::ParseGlb(view, &parsed, &error)) {
        std::fprintf(stderr, "consumer: parse refused: %s (offset %zu)\n",
                     vrmContainer::ErrorMessage(error.code), error.offset);
        return 1;
    }

    // A view, not a copy: the JSON chunk must point into the buffer that was
    // passed in. That is the property the header states, and it is the one a
    // consumer would lose to a package that shipped a differently built binary
    // behind a matching header.
    if (parsed.version != vrmContainer::GlbVersion2 || parsed.json.size() != 4
        || parsed.json.data() < glb.data()
        || parsed.json.data() >= glb.data() + glb.size()) {
        std::fprintf(stderr,
                     "consumer: parsed version %u with a %zu-byte JSON chunk\n",
                     parsed.version, parsed.json.size());
        return 1;
    }

    std::fprintf(stdout, "consumer: parsed a version %u container through the "
                         "installed package\n",
                 parsed.version);
    return 0;
}
