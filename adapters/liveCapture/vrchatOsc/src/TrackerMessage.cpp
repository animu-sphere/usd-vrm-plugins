// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVrchatOsc/TrackerMessage.h"

#include <cmath>
#include <string>
#include <utility>

namespace vrmAdapterVrchatOsc
{

namespace
{

constexpr std::string_view kChannelNames[TrackerChannelCount] = {
    "position",
    "rotation",
};

// The one form every address in this family takes. Written once, quoted into
// every `ArgumentMismatch` detail, and compared against exactly — see the
// header on why the sibling's "read the known prefix, count the rest" rule is
// reversed here.
constexpr std::string_view kTrackerTypeTags = "fff";

// A tracker index, or nothing when the segment is not one.
//
// "Is this a number" and "is this number in range" are deliberately two
// questions with two answers: a caller that conflated them could not tell
// `/tracking/trackers/9/position` — a legal spelling of an identity outside the
// surface — from `/tracking/trackers/head/position`, and the second is the row
// this whole adapter was reordered around.
enum class IndexReading
{
    NotANumber,
    // A decimal, and not one the surface defines.
    OutOfRange,
    // A decimal in range, spelled a way the surface does not: a leading zero.
    // Held apart from `OutOfRange` because a diagnostic saying "01 is outside
    // 1-8" would be false, and a false detail is worse than a vague one.
    NotCanonical,
    Index,
};

IndexReading
ReadTrackerIndex(std::string_view segment, std::uint8_t* index)
{
    if (segment.empty()) {
        return IndexReading::NotANumber;
    }
    for (const char character : segment) {
        if (character < '0' || character > '9') {
            return IndexReading::NotANumber;
        }
    }
    // A leading zero is refused rather than stripped, and the reason is
    // identity rather than parsing: this adapter keys a tracker on the segment
    // text, so accepting "01" would give one tracker two identities that
    // compare unequal everywhere downstream. No sender spells an index that
    // way; one that did would be reported rather than silently merged.
    if (segment.size() > 1 && segment.front() == '0') {
        return IndexReading::NotCanonical;
    }
    // Three digits is more than the surface's range can spell, and stopping
    // here keeps the accumulation below inside a `std::uint32_t` for any input.
    if (segment.size() > 3) {
        return IndexReading::OutOfRange;
    }

    std::uint32_t value = 0;
    for (const char character : segment) {
        value = value * 10 + static_cast<std::uint32_t>(character - '0');
    }
    if (value < MinTrackerIndex || value > MaxTrackerIndex) {
        return IndexReading::OutOfRange;
    }
    *index = static_cast<std::uint8_t>(value);
    return IndexReading::Index;
}

bool
Refuse(Diagnostic* error, DiagnosticCode code, std::string_view subject,
       std::string detail)
{
    if (error != nullptr) {
        *error = MakeDiagnostic(code, std::move(detail));
        error->subject = std::string(subject);
    }
    return false;
}

std::string
Quoted(std::string_view text)
{
    return "\"" + std::string(text) + "\"";
}

} // namespace

std::string_view
TrackerChannelString(TrackerChannel channel) noexcept
{
    const auto slot = static_cast<std::size_t>(channel);
    return slot < TrackerChannelCount ? kChannelNames[slot] : std::string_view();
}

std::optional<TrackerChannel>
FindTrackerChannel(std::string_view segment) noexcept
{
    for (std::size_t slot = 0; slot < TrackerChannelCount; ++slot) {
        if (kChannelNames[slot] == segment) {
            return static_cast<TrackerChannel>(slot);
        }
    }
    return std::nullopt;
}

bool
DecodeTrackerMessage(const osc::OscMessage& message, TrackerMessage* out,
                     Diagnostic* error)
{
    const std::string_view address = message.address;

    // Two guards before anything about this protocol is read, both refusing a
    // *caller's* mistake rather than a sender's. They come first because
    // neither has anything to do with the address they carry, and they raise
    // `PacketMalformed` because that is the code this adapter has for "these
    // bytes are not a message" — the sibling decoder answers both the same way
    // (vmc/src/VmcMessage.cpp).
    if (!out) {
        return Refuse(error, DiagnosticCode::PacketMalformed, address,
                      "no output message was provided");
    }
    // The OSC layer emits one argument per type tag, including the zero-width
    // ones, so a message where the two disagree did not come from it. Reading
    // by tag index would then run off the end of `arguments` — the type tags
    // are checked against `"fff"` below and the vector is indexed on the
    // strength of that check, which is only sound while the two agree.
    if (message.arguments.size() != message.typeTags.size()) {
        return Refuse(error, DiagnosticCode::PacketMalformed, address,
                      "the type tags describe "
                          + std::to_string(message.typeTags.size())
                          + " argument(s) and "
                          + std::to_string(message.arguments.size())
                          + " were given");
    }

    // The address family. A prefix test rather than a pattern match, because
    // the family's shape is fixed and everything after it is what varies.
    if (address.size() <= TrackerAddressPrefix.size()
        || address.compare(0, TrackerAddressPrefix.size(),
                           TrackerAddressPrefix)
               != 0) {
        return Refuse(error, DiagnosticCode::UnsupportedAddress, address,
                      "not a VRChat OSC tracker address");
    }

    const std::string_view rest = address.substr(TrackerAddressPrefix.size());
    const std::size_t separator = rest.find('/');
    if (separator == std::string_view::npos) {
        // `/tracking/trackers/1` — an identity with no channel. Unsupported
        // rather than malformed: it is a well-formed address this adapter maps
        // to nothing, which is exactly what that code says.
        return Refuse(error, DiagnosticCode::UnsupportedAddress, address,
                      "a tracker address with no channel");
    }

    const std::string_view segment = rest.substr(0, separator);
    const std::string_view channelText = rest.substr(separator + 1);
    if (channelText.find('/') != std::string_view::npos) {
        return Refuse(error, DiagnosticCode::UnsupportedAddress, address,
                      "a tracker address with a channel this adapter does not "
                      "read: " + Quoted(channelText));
    }

    // The channel before the identity, which is the order the failures are
    // useful in: `/tracking/trackers/hip/velocity` is not a tracker channel at
    // all, so blaming its identity would name the wrong half of an address that
    // is wrong in both.
    const std::optional<TrackerChannel> channel =
        FindTrackerChannel(channelText);
    if (!channel) {
        return Refuse(error, DiagnosticCode::UnsupportedAddress, address,
                      "a tracker address with a channel this adapter does not "
                      "read: " + Quoted(channelText));
    }

    TrackerId tracker;
    tracker.segment = segment;
    std::uint8_t index = 0;
    switch (ReadTrackerIndex(segment, &index)) {
    case IndexReading::Index:
        tracker.index = index;
        break;
    case IndexReading::OutOfRange:
        return Refuse(error, DiagnosticCode::TrackerIdInvalid, address,
                      "tracker index " + Quoted(segment) + " is outside "
                      + std::to_string(MinTrackerIndex) + "-"
                      + std::to_string(MaxTrackerIndex));
    case IndexReading::NotCanonical:
        return Refuse(error, DiagnosticCode::TrackerIdInvalid, address,
                      "tracker index " + Quoted(segment)
                          + " has a leading zero, which would give one tracker "
                            "two identities");
    case IndexReading::NotANumber:
        if (segment != HeadTrackerSegment) {
            return Refuse(error, DiagnosticCode::TrackerIdInvalid, address,
                          "tracker identity " + Quoted(segment)
                              + " is neither a decimal index nor "
                              + Quoted(HeadTrackerSegment));
        }
        break;
    }

    // Both tag strings are quoted, with their leading commas, so that the
    // diagnostic reads the way an operator sees a type tag string written
    // anywhere else — and so that the count and the types are both legible in
    // one field: ",ff" and ",ddd" are different failures and this says which.
    if (message.typeTags != kTrackerTypeTags) {
        return Refuse(error, DiagnosticCode::ArgumentMismatch, address,
                      "type tags " + Quoted("," + std::string(message.typeTags))
                          + " where " + Quoted("," + std::string(kTrackerTypeTags))
                          + " is this address's only form");
    }

    std::array<float, 3> values{{0.0f, 0.0f, 0.0f}};
    for (std::size_t slot = 0; slot < values.size(); ++slot) {
        const double value = message.arguments[slot].real;
        // Finiteness is checked and nothing else is. A component's magnitude,
        // its sign and its unit are all claims about a space this layer has not
        // established (VRC-3), but a NaN is unusable in any space: every
        // comparison against it is false, so a value that reached a solve would
        // make a tracker silently disappear from every test that had one.
        if (!std::isfinite(value)) {
            return Refuse(error, DiagnosticCode::CoordinateInvalid, address,
                          "component " + std::to_string(slot)
                              + " is not a finite number");
        }
        values[slot] = static_cast<float>(value);
    }

    out->tracker = tracker;
    out->channel = *channel;
    out->values = values;
    return true;
}

TrackerPacket
DecodeTrackerPacket(const osc::OscPacket& packet)
{
    TrackerPacket decoded;
    decoded.bundled = packet.bundled;
    decoded.messagesSeen = packet.messages.size();

    for (const osc::OscMessage& message : packet.messages) {
        TrackerMessage tracker;
        Diagnostic error;
        if (DecodeTrackerMessage(message, &tracker, &error)) {
            decoded.messages.push_back(tracker);
            continue;
        }
        if (error.code == DiagnosticCode::UnsupportedAddress) {
            ++decoded.unsupported;
        }
        decoded.diagnostics.push_back(std::move(error));
    }
    return decoded;
}

TrackerPacket
DecodeTrackerDatagram(const std::uint8_t* bytes, std::size_t size)
{
    osc::OscPacket packet;
    osc::OscDecodeError error;
    if (!osc::DecodeOscPacket(bytes, size, &packet, &error)) {
        // OSC-3's split, from this side: the shared decoder names the byte and
        // the address and carries no code, and this adapter — the one layer
        // that knows whose wire this is — supplies the code.
        TrackerPacket refused;
        refused.refused = true;
        Diagnostic diagnostic = MakeDiagnostic(DiagnosticCode::PacketMalformed,
                                               std::move(error.detail));
        diagnostic.subject = std::move(error.subject);
        refused.diagnostics.push_back(std::move(diagnostic));
        return refused;
    }
    return DecodeTrackerPacket(packet);
}

} // namespace vrmAdapterVrchatOsc
