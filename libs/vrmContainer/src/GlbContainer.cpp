// SPDX-License-Identifier: Apache-2.0

#include "vrmContainer/GlbContainer.h"

#include <cstdio>

namespace vrmContainer
{
namespace
{

constexpr std::size_t HeaderSize = 12;
constexpr std::size_t ChunkHeaderSize = 8;

std::uint32_t ReadU32(const std::byte* bytes) noexcept
{
    const auto* p = reinterpret_cast<const unsigned char*>(bytes);
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8u) |
        (static_cast<std::uint32_t>(p[2]) << 16u) |
        (static_cast<std::uint32_t>(p[3]) << 24u);
}

bool Fail(ErrorCode code, std::size_t offset, Error* error) noexcept
{
    if (error) {
        error->code = code;
        error->offset = offset;
    }
    return false;
}

} // namespace

bool HasGlbMagic(ByteView bytes) noexcept
{
    return bytes.valid() && bytes.size() >= 4 && ReadU32(bytes.data()) == GlbMagic;
}

bool MakeBufferView(ByteView buffer,
                    std::size_t offset,
                    std::size_t length,
                    ByteView* out,
                    Error* error) noexcept
{
    if (error) *error = {};
    if (out) *out = {};
    if (!out) return Fail(ErrorCode::InvalidArgument, 0, error);
    if (!buffer.valid()) return Fail(ErrorCode::NullData, 0, error);
    if (offset > buffer.size() || length > buffer.size() - offset) {
        return Fail(ErrorCode::ByteRangeOutOfRange, offset, error);
    }

    const std::byte* data = buffer.data();
    if (data) data += offset;
    *out = ByteView(data, length);
    return true;
}

bool ParseGlb(ByteView bytes, GlbView* out, Error* error) noexcept
{
    if (error) *error = {};
    if (out) *out = {};
    if (!out) return Fail(ErrorCode::InvalidArgument, 0, error);
    if (!bytes.valid()) return Fail(ErrorCode::NullData, 0, error);
    if (bytes.size() < HeaderSize) {
        return Fail(ErrorCode::HeaderTooShort, bytes.size(), error);
    }
    if (ReadU32(bytes.data()) != GlbMagic) {
        return Fail(ErrorCode::InvalidMagic, 0, error);
    }

    const std::uint32_t version = ReadU32(bytes.data() + 4);
    if (version != GlbVersion2) {
        return Fail(ErrorCode::UnsupportedVersion, 4, error);
    }

    const std::uint32_t declaredLength = ReadU32(bytes.data() + 8);
    if (declaredLength != bytes.size()) {
        return Fail(ErrorCode::LengthMismatch, 8, error);
    }

    GlbView parsed;
    parsed.version = version;
    parsed.declaredLength = declaredLength;

    bool firstChunk = true;
    bool sawJson = false;
    bool sawBin = false;
    std::size_t cursor = HeaderSize;
    while (cursor < bytes.size()) {
        if (bytes.size() - cursor < ChunkHeaderSize) {
            return Fail(ErrorCode::ChunkHeaderTruncated, cursor, error);
        }

        const std::uint32_t chunkLength = ReadU32(bytes.data() + cursor);
        const std::uint32_t chunkType = ReadU32(bytes.data() + cursor + 4);
        if ((chunkLength & 3u) != 0) {
            return Fail(ErrorCode::ChunkLengthUnaligned, cursor, error);
        }

        const std::size_t payloadOffset = cursor + ChunkHeaderSize;
        ByteView payload;
        if (!MakeBufferView(bytes, payloadOffset, chunkLength, &payload)) {
            return Fail(ErrorCode::ChunkOutOfRange, cursor, error);
        }

        if (firstChunk && chunkType != GlbJsonChunk) {
            return Fail(ErrorCode::JsonChunkNotFirst, cursor + 4, error);
        }
        if (chunkType == GlbJsonChunk) {
            if (!firstChunk || sawJson) {
                return Fail(ErrorCode::JsonChunkNotFirst, cursor + 4, error);
            }
            parsed.json = payload;
            sawJson = true;
        } else if (chunkType == GlbBinChunk) {
            if (sawBin) {
                return Fail(ErrorCode::DuplicateBinChunk, cursor + 4, error);
            }
            parsed.binary = payload;
            sawBin = true;
        }

        firstChunk = false;
        cursor = payloadOffset + chunkLength;
    }

    if (!sawJson || parsed.json.empty()) {
        return Fail(ErrorCode::JsonChunkMissing, HeaderSize, error);
    }

    *out = parsed;
    return true;
}

const char* ErrorMessage(ErrorCode code) noexcept
{
    switch (code) {
        case ErrorCode::None: return "no error";
        case ErrorCode::InvalidArgument: return "required output pointer is null";
        case ErrorCode::NullData: return "non-empty byte view has null data";
        case ErrorCode::HeaderTooShort: return "GLB header is truncated";
        case ErrorCode::InvalidMagic: return "GLB magic is not glTF";
        case ErrorCode::UnsupportedVersion: return "GLB version is not 2";
        case ErrorCode::LengthMismatch: return "GLB declared length does not match input";
        case ErrorCode::ChunkHeaderTruncated: return "GLB chunk header is truncated";
        case ErrorCode::ChunkLengthUnaligned: return "GLB chunk length is not 4-byte aligned";
        case ErrorCode::ChunkOutOfRange: return "GLB chunk extends beyond the container";
        case ErrorCode::JsonChunkMissing: return "GLB JSON chunk is missing";
        case ErrorCode::JsonChunkNotFirst: return "GLB JSON chunk is not first";
        case ErrorCode::DuplicateBinChunk: return "GLB has more than one BIN chunk";
        case ErrorCode::ByteRangeOutOfRange: return "byte range extends beyond its buffer";
    }
    return "unknown vrmContainer error";
}

std::uint64_t HashBytes(ByteView bytes) noexcept
{
    if (!bytes.valid()) return 0;
    std::uint64_t hash = 1469598103934665603ull;
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string MakeEmbeddedResourcePath(ByteView bytes, const char* extension)
{
    if (!bytes.valid() || !extension || !*extension) return {};
    char hash[17];
    std::snprintf(hash, sizeof(hash), "%016llx",
                  static_cast<unsigned long long>(HashBytes(bytes)));
    return std::string("images/") + hash + '.' + extension;
}

} // namespace vrmContainer
