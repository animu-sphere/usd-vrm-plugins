// SPDX-License-Identifier: Apache-2.0

#include "liveTransport/PacketCapture.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ios>
#include <istream>
#include <locale>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace liveTransport
{

namespace
{

constexpr int kPrecision = 6;

// Two spaces of indent, the hex column padded to a fixed width, two spaces, the
// gutter. The padding is what keeps the gutters of a short last line and a full
// one in the same column, and it is part of the format rather than a courtesy:
// the writer is a golden emitter, so every character it lays down is specified.
constexpr std::size_t kHexColumnWidth = PacketCaptureBytesPerLine * 3 - 1;

bool
Fail(PacketCaptureError* error, std::size_t line, std::string message)
{
    if (error) {
        error->line = line;
        error->message = std::move(message);
    }
    return false;
}

// A line is either fully consumed or malformed. Ignoring the tail would let
// `d 0.5 24 stray` read as a perfectly good record header -- the same class of
// mistake as an unknown header key, which this parser refuses outright rather
// than skipping.
bool
FullyConsumed(std::istringstream& stream, PacketCaptureError* error,
              std::size_t line, const std::string& what)
{
    std::string extra;
    if (stream >> extra) {
        return Fail(error, line, "unexpected '" + extra + "' after " + what);
    }
    return true;
}

// Parsing runs in the classic locale throughout, for the reason
// Diagnostics.cpp spells out: a capture written on a host whose decimal point
// is a comma must still read on one where it is a period.
std::istringstream
Tokenize(const std::string& line)
{
    std::istringstream stream(line);
    stream.imbue(std::locale::classic());
    return stream;
}

int
HexDigit(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

// Uppercase hex reads, lowercase hex is written. A hand-authored fixture is
// therefore accepted and then canonicalised by the writer, which is what the
// corpus round-trip test turns into a red build rather than a silent variation.
const char*
LowercaseHex()
{
    return "0123456789abcdef";
}

// One hex line, from the raw text: the tokens, then the gutter it claims they
// render as.
bool
ReadHexLine(const std::string& line, std::size_t start,
            std::vector<std::uint8_t>* bytes, std::size_t declared,
            PacketCaptureError* error, std::size_t lineNumber)
{
    const std::size_t firstPipe = line.find('|', start);
    std::string hexPart = line.substr(start);
    std::string gutter;
    bool hasGutter = false;

    if (firstPipe != std::string::npos) {
        // The gutter runs to the *last* '|' on the line, not the second one: a
        // payload byte 0x7c renders as '|' and would otherwise close the gutter
        // early.
        const std::size_t lastPipe = line.rfind('|');
        if (lastPipe == firstPipe) {
            return Fail(error, lineNumber,
                        "the ASCII gutter is not closed with a second '|'");
        }
        if (line.find_first_not_of(" \t", lastPipe + 1) != std::string::npos) {
            return Fail(error, lineNumber,
                        "unexpected text after the ASCII gutter");
        }
        hexPart = line.substr(start, firstPipe - start);
        gutter = line.substr(firstPipe + 1, lastPipe - firstPipe - 1);
        hasGutter = true;
    }

    std::vector<std::uint8_t> lineBytes;
    std::istringstream stream(hexPart);
    std::string token;
    while (stream >> token) {
        if (token.size() != 2 || HexDigit(token[0]) < 0
            || HexDigit(token[1]) < 0) {
            return Fail(error, lineNumber,
                        "'" + token + "' is not a two-digit hex byte");
        }
        lineBytes.push_back(static_cast<std::uint8_t>(
            HexDigit(token[0]) * 16 + HexDigit(token[1])));
    }

    if (lineBytes.empty()) {
        return Fail(error, lineNumber, "a hex line carries no bytes");
    }
    if (bytes->size() + lineBytes.size() > declared) {
        return Fail(error, lineNumber,
                    "the record declared " + std::to_string(declared)
                        + " bytes and the hex lines carry at least "
                        + std::to_string(bytes->size() + lineBytes.size()));
    }
    if (hasGutter) {
        const std::string expected =
            PacketCaptureGutter(lineBytes.data(), lineBytes.size());
        if (gutter != expected) {
            return Fail(error, lineNumber,
                        "the ASCII gutter reads '" + gutter
                            + "' but these bytes render as '" + expected + "'");
        }
    }

    bytes->insert(bytes->end(), lineBytes.begin(), lineBytes.end());
    return true;
}

} // namespace

std::string
PacketCaptureGutter(const std::uint8_t* bytes, std::size_t count)
{
    std::string gutter;
    gutter.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t byte = bytes[index];
        gutter += (byte >= 0x20 && byte <= 0x7e) ? static_cast<char>(byte) : '.';
    }
    return gutter;
}

bool
ReadPacketCapture(std::string_view magic, std::istream& input,
                  PacketCapture* capture, PacketCaptureError* error)
{
    if (!capture) {
        return Fail(error, 0, "no output capture was provided");
    }

    PacketCapture result;
    bool sawMagic = false;

    // The record being filled, and the length it declared. A record stays open
    // until its hex lines have delivered exactly that many bytes.
    std::optional<RecordedDatagram> open;
    std::size_t declared = 0;
    std::optional<double> previousReceiveTime;
    // The peer in force, from the last 'p' line. Empty means the capture has
    // not named one -- either it carries no 'p' at all, or the last one was
    // the explicit unknown. `sawPeerLine` is what ends the header: a 'p'
    // belongs to the record stream, so a header key after one is as late as
    // a header key after a datagram.
    std::string peer;
    bool sawPeerLine = false;

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') {
            continue;
        }

        std::istringstream stream = Tokenize(line);
        std::string keyword;
        stream >> keyword;

        if (!sawMagic) {
            if (keyword != magic) {
                return Fail(error, lineNumber,
                            std::string("expected the capture magic '")
                                + std::string(magic) + "'");
            }
            int version = 0;
            if (!(stream >> version)) {
                return Fail(error, lineNumber,
                            "the capture magic carries no format version");
            }
            if (version != PacketCaptureFormatVersion) {
                return Fail(error, lineNumber,
                            "unsupported packet capture format version "
                                + std::to_string(version));
            }
            if (!FullyConsumed(stream, error, lineNumber,
                               "the capture magic's format version")) {
                return false;
            }
            sawMagic = true;
            continue;
        }

        if (keyword == "p") {
            // Inside a record this would otherwise reach ReadHexLine and be
            // refused as a bad hex token, which names the symptom rather
            // than the mistake: a record's bytes are contiguous by
            // construction, and nothing may sit between them.
            if (open) {
                return Fail(error, lineNumber,
                            "a 'p' line cannot appear inside a record");
            }
            std::string value;
            if (!(stream >> value)) {
                return Fail(error, lineNumber, "'p' needs a peer");
            }
            if (!FullyConsumed(stream, error, lineNumber, "the 'p' peer")) {
                return false;
            }
            // '-' is the one spelling an endpoint cannot have, and it says
            // the one thing an omission cannot once a peer has been named:
            // from here on the capture does not know who sent these.
            peer = (value == "-") ? std::string() : value;
            sawPeerLine = true;
            continue;
        }

        if (keyword == "d") {
            if (open) {
                return Fail(error, lineNumber,
                            "the previous record declared "
                                + std::to_string(declared) + " bytes and its "
                                  "hex lines carried "
                                + std::to_string(open->bytes.size()));
            }

            double receiveTime = 0.0;
            if (!(stream >> receiveTime) || !std::isfinite(receiveTime)) {
                return Fail(error, lineNumber,
                            "'d' needs a finite receive time");
            }
            if (receiveTime < 0.0) {
                return Fail(error, lineNumber,
                            "a receive time cannot be negative");
            }
            // Non-decreasing, not strictly increasing: two datagrams can land
            // in the same clock tick, and a recorder that reports them as
            // simultaneous is telling the truth. Backwards is a different
            // matter -- a receive clock does not run backwards, so a capture
            // that says one did is a recorder defect being passed off as a
            // phenomenon.
            if (previousReceiveTime && receiveTime < *previousReceiveTime) {
                return Fail(error, lineNumber,
                            "receive times must not go backwards");
            }

            long long declaredLength = 0;
            if (!(stream >> declaredLength)) {
                return Fail(error, lineNumber, "'d' needs a byte length");
            }
            if (declaredLength < 0) {
                return Fail(error, lineNumber,
                            "a byte length cannot be negative");
            }
            if (static_cast<unsigned long long>(declaredLength)
                > MaxDatagramBytes) {
                return Fail(error, lineNumber,
                            "a datagram of " + std::to_string(declaredLength)
                                + " bytes exceeds the largest UDP payload ("
                                + std::to_string(MaxDatagramBytes) + ")");
            }
            if (!FullyConsumed(stream, error, lineNumber,
                               "the 'd' record's byte length")) {
                return false;
            }

            previousReceiveTime = receiveTime;
            declared = static_cast<std::size_t>(declaredLength);

            RecordedDatagram datagram;
            datagram.receiveTime = receiveTime;
            datagram.peer = peer;
            datagram.bytes.reserve(declared);
            if (declared == 0) {
                // A zero-length datagram is complete the moment it is declared.
                // Leaving it open would make the next line read as its payload.
                result.datagrams.push_back(std::move(datagram));
            } else {
                open = std::move(datagram);
            }
            continue;
        }

        if (open) {
            if (!ReadHexLine(line, start, &open->bytes, declared, error,
                             lineNumber)) {
                return false;
            }
            if (open->bytes.size() == declared) {
                result.datagrams.push_back(std::move(*open));
                open.reset();
            }
            continue;
        }

        // The header describes the whole capture, so it precedes every record.
        // A header key after the first record is refused rather than applied
        // retroactively to datagrams already read -- and a 'p' line is part
        // of the record stream, so it closes the header just as a record does.
        if (result.datagrams.empty() && !sawPeerLine) {
            std::string* field = nullptr;
            if (keyword == "sender") {
                field = &result.sender;
            } else if (keyword == "device") {
                field = &result.device;
            } else if (keyword == "sourceId") {
                field = &result.sourceId;
            } else if (keyword == "listen") {
                field = &result.listenEndpoint;
            } else if (keyword == "peer") {
                field = &result.peerEndpoint;
            }
            if (!field) {
                return Fail(error, lineNumber,
                            "unknown header key '" + keyword + "'");
            }
            std::string value;
            if (!(stream >> value)) {
                return Fail(error, lineNumber,
                            "'" + keyword + "' needs a value");
            }
            if (!FullyConsumed(stream, error, lineNumber,
                               "the '" + keyword + "' value")) {
                return false;
            }
            if (!field->empty()) {
                return Fail(error, lineNumber,
                            "header key '" + keyword + "' appears twice");
            }
            *field = value;
            continue;
        }

        return Fail(error, lineNumber,
                    "expected a 'd' record, found '" + keyword + "'");
    }

    if (!sawMagic) {
        return Fail(error, lineNumber,
                    std::string("the capture is empty or has no '")
                        + std::string(magic) + "' line");
    }
    if (open) {
        return Fail(error, lineNumber,
                    "the capture ends inside a record: "
                        + std::to_string(declared) + " bytes declared, "
                        + std::to_string(open->bytes.size()) + " carried");
    }
    if (result.datagrams.empty()) {
        return Fail(error, lineNumber, "the capture carries no datagrams");
    }

    *capture = std::move(result);
    return true;
}

bool
ReadPacketCaptureFile(std::string_view magic, const std::string& path,
                      PacketCapture* capture, PacketCaptureError* error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Fail(error, 0, "could not open packet capture '" + path + "'");
    }
    return ReadPacketCapture(magic, input, capture, error);
}

bool
WritePacketCapture(std::string_view magic, std::ostream& output,
                   const PacketCapture& capture)
{
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(kPrecision);

    output << magic << ' ' << PacketCaptureFormatVersion << '\n';
    if (!capture.sender.empty()) {
        output << "sender " << capture.sender << '\n';
    }
    if (!capture.device.empty()) {
        output << "device " << capture.device << '\n';
    }
    if (!capture.sourceId.empty()) {
        output << "sourceId " << capture.sourceId << '\n';
    }
    if (!capture.listenEndpoint.empty()) {
        output << "listen " << capture.listenEndpoint << '\n';
    }
    if (!capture.peerEndpoint.empty()) {
        output << "peer " << capture.peerEndpoint << '\n';
    }

    // The peer already emitted. A 'p' line is written only where a record's
    // peer differs from the one before it, which is what keeps a capture
    // whose records carry no peer byte-identical to one written before this
    // line existed -- and what makes a change of peer one line in a diff
    // rather than one line per datagram.
    std::string emittedPeer;
    bool emittedAnyPeer = false;

    for (const RecordedDatagram& datagram : capture.datagrams) {
        // The leading records of a capture that names no peer emit nothing:
        // an empty peer is worth the explicit '-' only once a peer has been
        // named and has then gone away.
        const bool peerChanged =
            datagram.peer != emittedPeer
            && (emittedAnyPeer || !datagram.peer.empty());

        output << '\n';
        if (peerChanged) {
            // The record follows immediately, so the 'p' reads as opening
            // the run it governs rather than as closing the one above it.
            output << "p " << (datagram.peer.empty() ? "-" : datagram.peer)
                   << '\n';
            emittedPeer = datagram.peer;
            emittedAnyPeer = true;
        }
        output << "d " << datagram.receiveTime << ' ' << datagram.bytes.size()
               << '\n';

        const std::size_t size = datagram.bytes.size();
        for (std::size_t offset = 0; offset < size;
             offset += PacketCaptureBytesPerLine) {
            const std::size_t count =
                std::min(PacketCaptureBytesPerLine, size - offset);

            std::string hex;
            hex.reserve(kHexColumnWidth);
            for (std::size_t index = 0; index < count; ++index) {
                if (index != 0) {
                    hex += ' ';
                }
                const std::uint8_t byte = datagram.bytes[offset + index];
                hex += LowercaseHex()[byte >> 4];
                hex += LowercaseHex()[byte & 0x0f];
            }
            hex.resize(kHexColumnWidth, ' ');

            output << "  " << hex << "  |"
                   << PacketCaptureGutter(datagram.bytes.data() + offset, count)
                   << "|\n";
        }
    }

    return static_cast<bool>(output);
}

bool
WritePacketCaptureFile(std::string_view magic, const std::string& path,
                       const PacketCapture& capture)
{
    // Binary mode with explicit '\n': a capture written on Windows must be byte
    // identical to one written on Linux, or a golden fixture cannot be shared
    // across the three OS cells.
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    return WritePacketCapture(magic, output, capture)
           && output.flush().good();
}

} // namespace liveTransport
