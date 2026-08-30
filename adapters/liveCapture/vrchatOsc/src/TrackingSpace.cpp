// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVrchatOsc/TrackingSpace.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace vrmAdapterVrchatOsc
{

namespace
{

// Local rather than from <numbers>: this library is C++17.
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

// A rotation of `degrees` about one basis axis, by the right-hand rule and out
// of the raw number — the sender's handedness is deliberately absent here and
// arrives once, in the mirror below (TrackingSpace.h).
pxr::GfQuatf
AxisRotation(std::size_t axis, float degrees) noexcept
{
    const double half = 0.5 * static_cast<double>(degrees) * kDegreesToRadians;
    pxr::GfVec3f imaginary(0.0f);
    imaginary[axis] = static_cast<float>(std::sin(half));
    return pxr::GfQuatf(static_cast<float>(std::cos(half)), imaginary);
}

// The same refusal helper the message layer uses, with the same shape: the
// diagnostic is filled only when the caller asked for one, and the return is
// always false so a refusal reads as one line at the call site.
bool
Refuse(Diagnostic* error, DiagnosticCode code, const std::string& subject,
       std::string detail)
{
    if (error != nullptr) {
        *error = MakeDiagnostic(code, std::move(detail));
        error->subject = subject;
    }
    return false;
}

// The channel's address segment, or its number when it has none. A
// `TrackerChannel` out of range cannot be decoded from a datagram and can be
// passed by a caller, and a refusal that named it with an empty string would
// read as though the channel were missing rather than wrong.
std::string
ChannelName(TrackerChannel channel)
{
    const std::string_view name = TrackerChannelString(channel);
    return name.empty()
               ? "channel " + std::to_string(static_cast<int>(channel))
               : std::string(name);
}

// Every guard both `Map` functions share: a caller's output, a caller's
// channel, and the sender's three numbers. Returns false with `diagnostic`
// filled, exactly as the public functions do.
bool
CheckMappable(const TrackerMessage& message, TrackerChannel expected,
              const void* out, Diagnostic* diagnostic)
{
    const std::string address = TrackerMessageAddress(message);

    if (out == nullptr) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed, address,
                      "no output value was provided");
    }
    if (message.channel != expected) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed, address,
                      "a " + ChannelName(message.channel)
                          + " message was given to the "
                          + ChannelName(expected) + " conversion");
    }
    for (std::size_t slot = 0; slot < message.values.size(); ++slot) {
        if (!std::isfinite(message.values[slot])) {
            return Refuse(diagnostic, DiagnosticCode::CoordinateInvalid,
                          address,
                          "component " + std::to_string(slot)
                              + " is not a finite number");
        }
    }
    return true;
}

} // namespace

pxr::GfVec3f
ToCanonicalPosition(const std::array<float, 3>& position) noexcept
{
    // The reflection, written the way the header states it rather than as a
    // loop over `TrackingSpaceMirroredComponent`: three components spelled out
    // are checkable by eye, and a reader comparing this against the sibling
    // adapter's line should see the same line.
    return pxr::GfVec3f(-position[0], position[1], position[2]);
}

pxr::GfQuatf
ToCanonicalRotation(const std::array<float, 3>& eulerDegrees) noexcept
{
    // `Ry * Rx * Rz` applied to a column vector: the Z angle turns first and
    // the Y angle last. OpenUSD's quaternion product is the same order as the
    // matrix one -- `a * b` applies `b` first -- so this line reads as the
    // composition it is.
    const pxr::GfQuatf sender = AxisRotation(1, eulerDegrees[1])
                                * AxisRotation(0, eulerDegrees[0])
                                * AxisRotation(2, eulerDegrees[2]);

    // `(w, det(M) * M v)` for `M = diag(-1, 1, 1)`: the mirror negates the
    // first component and the determinant negates all three, which leaves the
    // first alone and flips the other two.
    const pxr::GfVec3f imaginary = sender.GetImaginary();
    pxr::GfQuatf canonical(sender.GetReal(),
                           pxr::GfVec3f(imaginary[0], -imaginary[1],
                                        -imaginary[2]));
    canonical.Normalize();
    return canonical;
}

std::string
TrackerMessageAddress(const TrackerMessage& message)
{
    std::string address(TrackerAddressPrefix);
    address += message.tracker.segment;
    address += '/';
    address += TrackerChannelString(message.channel);
    return address;
}

bool
MapTrackerPosition(const TrackerMessage& message, pxr::GfVec3f* out,
                   Diagnostic* diagnostic)
{
    if (!CheckMappable(message, TrackerChannel::Position, out, diagnostic)) {
        return false;
    }
    *out = ToCanonicalPosition(message.values);
    return true;
}

bool
MapTrackerRotation(const TrackerMessage& message, pxr::GfQuatf* out,
                   Diagnostic* diagnostic)
{
    if (!CheckMappable(message, TrackerChannel::Rotation, out, diagnostic)) {
        return false;
    }
    *out = ToCanonicalRotation(message.values);
    return true;
}

} // namespace vrmAdapterVrchatOsc
