// SPDX-License-Identifier: Apache-2.0

#include "vrmContainer/GlbContainer.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

namespace vc = vrmContainer;

namespace
{

int failures = 0;

#define CHECK(expr) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } } while (false)

void AppendU32(std::vector<std::byte>& out, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

std::vector<std::byte> MinimalGlb()
{
    std::vector<std::byte> out;
    AppendU32(out, vc::GlbMagic);
    AppendU32(out, vc::GlbVersion2);
    AppendU32(out, 36);
    AppendU32(out, 4);
    AppendU32(out, vc::GlbJsonChunk);
    out.insert(out.end(), {std::byte{'{'}, std::byte{'}'}, std::byte{' '}, std::byte{' '}});
    AppendU32(out, 4);
    AppendU32(out, vc::GlbBinChunk);
    out.insert(out.end(), {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    return out;
}

vc::ByteView View(const std::vector<std::byte>& bytes)
{
    return {bytes.data(), bytes.size()};
}

void SetU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xffu);
    }
}

void TestParse()
{
    const std::vector<std::byte> bytes = MinimalGlb();
    vc::GlbView glb;
    vc::Error error;
    CHECK(vc::HasGlbMagic(View(bytes)));
    CHECK(vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::None);
    CHECK(glb.version == 2);
    CHECK(glb.declaredLength == bytes.size());
    CHECK(glb.json.size() == 4);
    CHECK(glb.binary.size() == 4);
    CHECK(glb.json.data() == bytes.data() + 20);
    CHECK(glb.binary.data() == bytes.data() + 32);
}

void TestMalformedContainers()
{
    vc::GlbView glb;
    vc::Error error;

    CHECK(!vc::ParseGlb(View(MinimalGlb()), nullptr, &error));
    CHECK(error.code == vc::ErrorCode::InvalidArgument);

    std::vector<std::byte> bytes(11);
    CHECK(!vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::HeaderTooShort);

    bytes = MinimalGlb();
    bytes[0] = std::byte{0};
    CHECK(!vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::InvalidMagic);

    bytes = MinimalGlb();
    SetU32(bytes, 4, 1);
    CHECK(!vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::UnsupportedVersion);

    bytes = MinimalGlb();
    SetU32(bytes, 8, 31);
    CHECK(!vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::LengthMismatch);

    bytes = MinimalGlb();
    SetU32(bytes, 12, 5);
    CHECK(!vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::ChunkLengthUnaligned);

    bytes = MinimalGlb();
    SetU32(bytes, 12, 64);
    CHECK(!vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::ChunkOutOfRange);

    bytes = MinimalGlb();
    bytes.resize(28);
    SetU32(bytes, 8, static_cast<std::uint32_t>(bytes.size()));
    CHECK(!vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::ChunkHeaderTruncated);

    bytes = MinimalGlb();
    SetU32(bytes, 16, vc::GlbBinChunk);
    CHECK(!vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::JsonChunkNotFirst);

    bytes = MinimalGlb();
    AppendU32(bytes, 4);
    AppendU32(bytes, vc::GlbBinChunk);
    bytes.insert(bytes.end(), {std::byte{5}, std::byte{6},
                               std::byte{7}, std::byte{8}});
    SetU32(bytes, 8, static_cast<std::uint32_t>(bytes.size()));
    CHECK(!vc::ParseGlb(View(bytes), &glb, &error));
    CHECK(error.code == vc::ErrorCode::DuplicateBinChunk);
}

void TestBufferViews()
{
    const std::vector<std::byte> bytes = MinimalGlb();
    vc::ByteView slice;
    vc::Error error;
    CHECK(!vc::MakeBufferView(View(bytes), 0, 1, nullptr, &error));
    CHECK(error.code == vc::ErrorCode::InvalidArgument);
    CHECK(vc::MakeBufferView(View(bytes), 20, 4, &slice, &error));
    CHECK(slice.data() == bytes.data() + 20);
    CHECK(slice.size() == 4);
    CHECK(vc::MakeBufferView(View(bytes), bytes.size(), 0, &slice, &error));
    CHECK(!vc::MakeBufferView(View(bytes), bytes.size(), 1, &slice, &error));
    CHECK(error.code == vc::ErrorCode::ByteRangeOutOfRange);
    CHECK(!vc::MakeBufferView(View(bytes), 1,
                              std::numeric_limits<std::size_t>::max(),
                              &slice, &error));
}

void TestResourceNaming()
{
    const std::vector<std::byte> bytes = {
        std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'}};
    CHECK(vc::MakeEmbeddedResourcePath(View(bytes), "png") ==
          "images/dfa761ae3f638411.png");
    CHECK(vc::MakeEmbeddedResourcePath(View(bytes), nullptr).empty());
}

} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(vc::ByteView().data()), const std::byte*>);
    TestParse();
    TestMalformedContainers();
    TestBufferViews();
    TestResourceNaming();
    return failures == 0 ? 0 : 1;
}
