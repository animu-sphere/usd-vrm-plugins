// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmContainer/ByteView.h"
#include "vrmContainer/api.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace vrmContainer
{

inline constexpr std::uint32_t GlbMagic = 0x46546c67u;
inline constexpr std::uint32_t GlbVersion2 = 2u;
inline constexpr std::uint32_t GlbJsonChunk = 0x4e4f534au;
inline constexpr std::uint32_t GlbBinChunk = 0x004e4942u;

enum class ErrorCode
{
    None,
    InvalidArgument,
    NullData,
    HeaderTooShort,
    InvalidMagic,
    UnsupportedVersion,
    LengthMismatch,
    ChunkHeaderTruncated,
    ChunkLengthUnaligned,
    ChunkOutOfRange,
    JsonChunkMissing,
    JsonChunkNotFirst,
    DuplicateBinChunk,
    ByteRangeOutOfRange,
};

struct Error
{
    ErrorCode code = ErrorCode::None;
    std::size_t offset = 0;
};

// Views point into the input passed to ParseGlb and never own or mutate bytes.
struct GlbView
{
    std::uint32_t version = 0;
    std::uint32_t declaredLength = 0;
    ByteView json;
    ByteView binary;
};

VRMCONTAINER_API bool HasGlbMagic(ByteView bytes) noexcept;
VRMCONTAINER_API bool ParseGlb(
    ByteView bytes, GlbView* out, Error* error = nullptr) noexcept;
VRMCONTAINER_API bool MakeBufferView(
    ByteView buffer,
    std::size_t offset,
    std::size_t length,
    ByteView* out,
    Error* error = nullptr) noexcept;
VRMCONTAINER_API const char* ErrorMessage(ErrorCode code) noexcept;

// Embedded-resource names are content-addressed. This intentionally preserves
// the hash seed used by usdVrm before vrmContainer was extracted.
VRMCONTAINER_API std::uint64_t HashBytes(ByteView bytes) noexcept;
VRMCONTAINER_API std::string MakeEmbeddedResourcePath(
    ByteView bytes, const char* extension);

} // namespace vrmContainer
