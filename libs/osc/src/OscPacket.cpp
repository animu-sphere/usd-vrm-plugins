// SPDX-License-Identifier: Apache-2.0

#include "osc/OscPacket.h"

#include <cstring>
#include <string>
#include <utility>

namespace osc
{

namespace
{

// Eight bytes including the terminator, which is also why a bundle is
// recognisable before anything else is parsed.
constexpr char kBundleMagic[8] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0'};
constexpr std::size_t kBundleHeaderSize = 16; // magic + time tag

std::uint32_t
ReadUInt32(const std::uint8_t* bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24)
        | (static_cast<std::uint32_t>(bytes[1]) << 16)
        | (static_cast<std::uint32_t>(bytes[2]) << 8)
        | static_cast<std::uint32_t>(bytes[3]);
}

std::uint64_t
ReadUInt64(const std::uint8_t* bytes)
{
    return (static_cast<std::uint64_t>(ReadUInt32(bytes)) << 32)
        | static_cast<std::uint64_t>(ReadUInt32(bytes + 4));
}

// OSC floats are IEEE 754 big-endian on the wire. The bytes are reassembled
// into the integer first and then bit-cast, rather than byte-swapped in place,
// so the decode is correct on a big-endian host too without a second path.
float
ReadFloat32(const std::uint8_t* bytes)
{
    const std::uint32_t bits = ReadUInt32(bytes);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double
ReadFloat64(const std::uint8_t* bytes)
{
    const std::uint64_t bits = ReadUInt64(bytes);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Padding a string to a four-byte boundary always adds at least one NUL, so a
// string whose length is already a multiple of four grows by four.
std::size_t
PaddedSize(std::size_t length)
{
    return (length + 4) & ~static_cast<std::size_t>(3);
}

class Decoder
{
public:
    Decoder(OscPacket* packet, OscDecodeError* error)
        : _packet(packet)
        , _error(error)
    {
    }

    // One OSC packet element: the datagram itself, or an element of a bundle.
    // `base` is the element's offset within the datagram, so a refusal
    // points at a byte the reader can find in the capture rather than at an
    // offset within some nested buffer.
    bool Element(const std::uint8_t* bytes, std::size_t size, std::size_t base,
                 std::size_t depth)
    {
        if (depth > MaxOscBundleDepth) {
            return Fail(base, "bundles are nested more than "
                            + std::to_string(MaxOscBundleDepth) + " deep");
        }
        // Only reachable at the top level: a zero-length bundle element is
        // refused by its size field, below.
        if (size == 0) {
            return Fail(base, "the datagram is empty");
        }
        // OSC requires the contents of a packet to be a multiple of four bytes.
        // A datagram that fails this was truncated in transit or assembled by
        // something that is not writing OSC, and every length below is computed
        // assuming it holds.
        if (size % 4 != 0) {
            return Fail(base, "a packet of " + std::to_string(size)
                            + " bytes is not a multiple of four");
        }

        if (size >= sizeof(kBundleMagic)
            && std::memcmp(bytes, kBundleMagic, sizeof(kBundleMagic)) == 0) {
            return Bundle(bytes, size, base, depth);
        }
        if (bytes[0] == '/') {
            return Message(bytes, size, base);
        }
        return Fail(base, "neither a bundle nor a message: the first byte is "
                          "not '/'");
    }

private:
    bool Fail(std::size_t offset, std::string detail,
              std::string_view subject = {})
    {
        if (_error) {
            _error->detail = std::move(detail) + " (at byte "
                + std::to_string(offset) + ")";
            _error->subject.assign(subject);
        }
        return false;
    }

    bool Bundle(const std::uint8_t* bytes, std::size_t size, std::size_t base,
                std::size_t depth)
    {
        if (size < kBundleHeaderSize) {
            return Fail(base, "a bundle needs its magic and an eight-byte time "
                              "tag");
        }
        if (depth == 0) {
            _packet->bundled = true;
            _packet->timeTag = ReadUInt64(bytes + sizeof(kBundleMagic));
        }

        // An empty bundle is well-formed OSC and yields no messages. Refusing
        // one would blame a sender for a legal, if pointless, packet.
        std::size_t offset = kBundleHeaderSize;
        while (offset < size) {
            // Unreachable while the alignment invariant holds -- `size` is a
            // multiple of four, the header is sixteen bytes, and an element
            // whose own length is not a multiple of four is refused by the
            // recursive call below before `offset` advances past it, so
            // `size - offset` is always a multiple of four and never 1..3. It
            // stays because it is this loop's own bound: reading the invariant
            // as guaranteed *here*, where it is established three frames away,
            // is how a decoder acquires an out-of-bounds read during a later
            // edit.
            if (size - offset < 4) {
                return Fail(base + offset,
                            "a bundle element's size field is truncated");
            }
            const std::int32_t elementSize =
                static_cast<std::int32_t>(ReadUInt32(bytes + offset));
            offset += 4;

            if (elementSize <= 0) {
                return Fail(base + offset - 4,
                            "a bundle element declares "
                                + std::to_string(elementSize) + " bytes");
            }
            const std::size_t length = static_cast<std::size_t>(elementSize);
            if (length > size - offset) {
                return Fail(base + offset - 4,
                            "a bundle element of " + std::to_string(length)
                                + " bytes runs past the end of the bundle, "
                                  "which has "
                                + std::to_string(size - offset) + " left");
            }
            if (!Element(bytes + offset, length, base + offset, depth + 1)) {
                return false;
            }
            offset += length;
        }
        return true;
    }

    bool Message(const std::uint8_t* bytes, std::size_t size, std::size_t base)
    {
        std::size_t offset = 0;
        std::string_view address;
        if (!String(bytes, size, &offset, &address)) {
            return Fail(base, "the address pattern has no terminator");
        }

        std::string_view tags;
        if (!String(bytes, size, &offset, &tags)) {
            return Fail(base + offset, "the type tag string has no terminator",
                        address);
        }
        // OSC 1.0 left the type tag string optional and current practice does
        // not: without it an argument's size is unknowable, so a message that
        // omits it cannot be decoded at all -- only guessed at.
        if (tags.empty() || tags.front() != ',') {
            return Fail(base + offset,
                        "the type tag string does not begin with a comma",
                        address);
        }
        tags.remove_prefix(1);

        OscMessage message;
        message.address = address;
        message.typeTags = tags;
        message.arguments.reserve(tags.size());

        for (const char tag : tags) {
            OscArgument argument;
            argument.tag = tag;

            const std::uint8_t* data = nullptr;
            switch (tag) {
            case 'i':
                if (!Fixed(bytes, size, &offset, 4, base, address, tag,
                           &data)) {
                    return false;
                }
                argument.integer = static_cast<std::int32_t>(ReadUInt32(data));
                break;
            case 'f':
                if (!Fixed(bytes, size, &offset, 4, base, address, tag,
                           &data)) {
                    return false;
                }
                argument.real = ReadFloat32(data);
                break;
            case 'c':
            case 'r':
            case 'm':
                if (!Fixed(bytes, size, &offset, 4, base, address, tag,
                           &data)) {
                    return false;
                }
                argument.integer = ReadUInt32(data);
                break;
            case 'h':
                if (!Fixed(bytes, size, &offset, 8, base, address, tag,
                           &data)) {
                    return false;
                }
                argument.integer =
                    static_cast<std::int64_t>(ReadUInt64(data));
                break;
            // Not `h`'s path, though the wire size is the same: an NTP time tag
            // is unsigned and has had its high bit set since 1968, so sharing
            // the signed field made the normal case the wrong one.
            case 't':
                if (!Fixed(bytes, size, &offset, 8, base, address, tag,
                           &data)) {
                    return false;
                }
                argument.timeTag = ReadUInt64(data);
                break;
            case 'd':
                if (!Fixed(bytes, size, &offset, 8, base, address, tag,
                           &data)) {
                    return false;
                }
                argument.real = ReadFloat64(data);
                break;
            case 's':
            case 'S':
                if (!String(bytes, size, &offset, &argument.text)) {
                    return Fail(base + offset,
                                std::string("a '") + tag
                                    + "' argument has no terminator",
                                address);
                }
                break;
            case 'b':
                if (!Blob(bytes, size, &offset, base, address,
                          &argument.blob)) {
                    return false;
                }
                break;
            case 'T':
                argument.integer = 1;
                break;
            case 'F':
            case 'N':
            case 'I':
                break;
            case '[':
            case ']':
                // Array delimiters. They carry no bytes and are kept as
                // zero-width arguments rather than dropped, so `typeTags` and
                // `arguments` stay index-for-index and a consumer that cares
                // about the grouping can still see it.
                break;
            default:
                // Not "unsupported": every tag OSC defines is handled above, so
                // this one is not OSC. Skipping it is impossible anyway -- an
                // unknown tag has an unknown size, and everything after it in
                // the message would be read at the wrong offset.
                return Fail(base + offset,
                            std::string("unknown type tag '") + tag + "'",
                            address);
            }

            message.arguments.push_back(std::move(argument));
        }

        if (offset != size) {
            // The tags accounted for less than the message carries. Something
            // was appended, or the type tag string does not describe the
            // payload; either way the next argument would be read from a byte
            // nobody meant.
            return Fail(base + offset,
                        std::to_string(size - offset)
                            + " byte(s) follow the arguments the type tags "
                              "describe",
                        address);
        }

        _packet->messages.push_back(std::move(message));
        return true;
    }

    // A NUL-terminated, four-byte-padded string. Advances `offset` past the
    // padding.
    static bool String(const std::uint8_t* bytes, std::size_t size,
                       std::size_t* offset, std::string_view* text)
    {
        const std::size_t start = *offset;
        std::size_t index = start;
        while (index < size && bytes[index] != '\0') {
            ++index;
        }
        if (index >= size) {
            return false;
        }
        const std::size_t length = index - start;
        const std::size_t padded = PaddedSize(length);
        if (padded > size - start) {
            return false;
        }
        *text = std::string_view(
            reinterpret_cast<const char*>(bytes + start), length);
        *offset = start + padded;
        return true;
    }

    // A fixed-width argument. `data` is left pointing at it, so the caller
    // decodes from the argument rather than from `offset` minus a width it has
    // to remember.
    bool Fixed(const std::uint8_t* bytes, std::size_t size,
               std::size_t* offset, std::size_t width, std::size_t base,
               std::string_view address, char tag, const std::uint8_t** data)
    {
        if (size - *offset < width) {
            return Fail(base + *offset,
                        std::string("a '") + tag + "' argument needs "
                            + std::to_string(width) + " bytes and "
                            + std::to_string(size - *offset) + " remain",
                        address);
        }
        *data = bytes + *offset;
        *offset += width;
        return true;
    }

    bool Blob(const std::uint8_t* bytes, std::size_t size, std::size_t* offset,
              std::size_t base, std::string_view address, OscBlob* blob)
    {
        if (size - *offset < 4) {
            return Fail(base + *offset, "a 'b' argument has no size field",
                        address);
        }
        const std::int32_t declared =
            static_cast<std::int32_t>(ReadUInt32(bytes + *offset));
        *offset += 4;
        if (declared < 0) {
            return Fail(base + *offset - 4,
                        "a 'b' argument declares " + std::to_string(declared)
                            + " bytes",
                        address);
        }
        const std::size_t length = static_cast<std::size_t>(declared);
        // The data is padded to a four-byte boundary; the declared size is not.
        const std::size_t padded = (length + 3) & ~static_cast<std::size_t>(3);
        if (padded > size - *offset) {
            return Fail(base + *offset - 4,
                        "a 'b' argument of " + std::to_string(length)
                            + " bytes runs past the end of the message",
                        address);
        }
        blob->bytes = bytes + *offset;
        blob->size = length;
        *offset += padded;
        return true;
    }

    OscPacket* _packet;
    OscDecodeError* _error;
};

} // namespace

bool
DecodeOscPacket(const std::uint8_t* bytes, std::size_t size, OscPacket* packet,
                OscDecodeError* error)
{
    // Both guards below are caller bugs rather than sender bugs, and both
    // overwrite `subject`: a reused error left holding the previous datagram's
    // address would attribute a caller's mistake to a sender that sent nothing
    // wrong.
    if (!packet) {
        if (error) {
            error->subject.clear();
            error->detail = "no output packet was provided";
        }
        return false;
    }
    if (size != 0 && !bytes) {
        if (error) {
            error->subject.clear();
            error->detail = "a datagram of " + std::to_string(size)
                + " bytes has no bytes";
        }
        return false;
    }

    // Decoded into a local: a refused datagram leaves the caller's packet as it
    // was, so a receive loop that reuses one cannot mistake half of a rejected
    // datagram for this frame's messages.
    OscPacket result;
    Decoder decoder(&result, error);
    if (!decoder.Element(bytes, size, 0, 0)) {
        return false;
    }

    *packet = std::move(result);
    return true;
}

} // namespace osc
