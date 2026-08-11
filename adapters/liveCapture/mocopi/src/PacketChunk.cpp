// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterMocopi/PacketChunk.h"

#include <cstring>
#include <string>

namespace vrmAdapterMocopi
{
namespace
{

// Assembled byte by byte rather than `memcpy`'d into the integer, so the reading
// is little-endian because this code says so and not because the host happens to
// agree. The wire is little-endian — measured, see the header — and a big-endian
// host must produce the same numbers or the corpus means nothing there.
std::uint64_t ReadLittleEndian(const std::uint8_t* bytes, std::size_t width)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index)
    {
        value |= static_cast<std::uint64_t>(bytes[index])
                 << (8 * static_cast<unsigned>(index));
    }
    return value;
}

bool ReadScalar(const PacketChunk& chunk, std::size_t width,
                std::uint64_t* raw) noexcept
{
    if (chunk.bytes == nullptr || chunk.size != width)
    {
        return false;
    }
    *raw = ReadLittleEndian(chunk.bytes, width);
    return true;
}

Diagnostic Malformed(std::string_view context, std::string detail)
{
    Diagnostic diagnostic =
        MakeDiagnostic(DiagnosticCode::PacketMalformed, std::move(detail));
    diagnostic.subject = std::string(context);
    return diagnostic;
}

} // namespace

std::string PacketChunkTagText(std::string_view tag)
{
    bool printable = !tag.empty();
    for (const char character : tag)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte > 0x7e)
        {
            printable = false;
            break;
        }
    }
    if (printable)
    {
        return std::string(tag);
    }

    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text = "0x";
    for (const char character : tag)
    {
        const auto byte = static_cast<unsigned char>(character);
        text += kDigits[byte >> 4];
        text += kDigits[byte & 0x0f];
    }
    return text;
}

bool DecodePacketChunks(const std::uint8_t* bytes, std::size_t size,
                        std::vector<PacketChunk>* chunks,
                        std::string_view context, Diagnostic* diagnostic)
{
    if (chunks == nullptr)
    {
        return false;
    }
    chunks->clear();
    if (bytes == nullptr && size != 0)
    {
        if (diagnostic != nullptr)
        {
            *diagnostic = Malformed(context, "no bytes to walk");
        }
        return false;
    }

    std::size_t offset = 0;
    while (offset != size)
    {
        if (size - offset < PacketChunkHeaderBytes)
        {
            if (diagnostic != nullptr)
            {
                *diagnostic = Malformed(
                    context,
                    "a chunk header needs "
                        + std::to_string(PacketChunkHeaderBytes)
                        + " bytes and only " + std::to_string(size - offset)
                        + " remain at offset " + std::to_string(offset));
            }
            chunks->clear();
            return false;
        }

        const std::uint64_t length =
            ReadLittleEndian(bytes + offset, sizeof(std::uint32_t));
        const std::size_t available = size - offset - PacketChunkHeaderBytes;
        std::string_view tag(
            reinterpret_cast<const char*>(bytes + offset
                                          + sizeof(std::uint32_t)),
            PacketChunkTagBytes);

        if (length > available)
        {
            if (diagnostic != nullptr)
            {
                *diagnostic = Malformed(
                    context, "chunk " + PacketChunkTagText(tag) + " at offset "
                                 + std::to_string(offset) + " declares "
                                 + std::to_string(length) + " payload byte(s) "
                                 + "and only " + std::to_string(available)
                                 + " remain");
            }
            chunks->clear();
            return false;
        }

        PacketChunk chunk;
        chunk.tag = tag;
        chunk.bytes = bytes + offset + PacketChunkHeaderBytes;
        chunk.size = static_cast<std::size_t>(length);
        chunk.offset = offset;
        chunks->push_back(chunk);

        offset += PacketChunkHeaderBytes + static_cast<std::size_t>(length);
    }

    return true;
}

const PacketChunk* FindPacketChunk(const std::vector<PacketChunk>& chunks,
                                   std::string_view tag) noexcept
{
    for (const PacketChunk& chunk : chunks)
    {
        if (chunk.tag == tag)
        {
            return &chunk;
        }
    }
    return nullptr;
}

std::size_t CountPacketChunks(const std::vector<PacketChunk>& chunks,
                              std::string_view tag) noexcept
{
    std::size_t count = 0;
    for (const PacketChunk& chunk : chunks)
    {
        if (chunk.tag == tag)
        {
            ++count;
        }
    }
    return count;
}

bool ReadPacketChunkU16(const PacketChunk& chunk,
                        std::uint16_t* value) noexcept
{
    std::uint64_t raw = 0;
    if (value == nullptr || !ReadScalar(chunk, sizeof(std::uint16_t), &raw))
    {
        return false;
    }
    *value = static_cast<std::uint16_t>(raw);
    return true;
}

bool ReadPacketChunkI16(const PacketChunk& chunk, std::int16_t* value) noexcept
{
    std::uint16_t unsignedValue = 0;
    if (value == nullptr || !ReadPacketChunkU16(chunk, &unsignedValue))
    {
        return false;
    }
    // The measured parent-bone column carries -1 for the root, so this field is
    // signed on the wire. Converted through `memcpy` rather than by a cast:
    // narrowing an out-of-range unsigned to a signed type is implementation
    // defined before C++20, and the root's sentinel is exactly the value that
    // would go through it.
    std::memcpy(value, &unsignedValue, sizeof(unsignedValue));
    return true;
}

bool ReadPacketChunkU32(const PacketChunk& chunk,
                        std::uint32_t* value) noexcept
{
    std::uint64_t raw = 0;
    if (value == nullptr || !ReadScalar(chunk, sizeof(std::uint32_t), &raw))
    {
        return false;
    }
    *value = static_cast<std::uint32_t>(raw);
    return true;
}

bool ReadPacketChunkF32(const PacketChunk& chunk, float* value) noexcept
{
    std::uint64_t raw = 0;
    if (value == nullptr || !ReadScalar(chunk, sizeof(float), &raw))
    {
        return false;
    }
    const auto bits = static_cast<std::uint32_t>(raw);
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "the wire carries IEEE-754 binary32");
    std::memcpy(value, &bits, sizeof(bits));
    return true;
}

bool ReadPacketChunkF64(const PacketChunk& chunk, double* value) noexcept
{
    std::uint64_t raw = 0;
    if (value == nullptr || !ReadScalar(chunk, sizeof(double), &raw))
    {
        return false;
    }
    static_assert(sizeof(double) == sizeof(std::uint64_t),
                  "the wire carries IEEE-754 binary64");
    std::memcpy(value, &raw, sizeof(raw));
    return true;
}

} // namespace vrmAdapterMocopi
