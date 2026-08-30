// SPDX-License-Identifier: Apache-2.0
//
// The basis: which way this sender's numbers point, and what they become.
//
// **The constants below are readings of one recorded session**, not values this
// repository chose, and each is quoted with the take it came from
// ([report 03](../../../../docs/reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md)).
// That is the whole method of VRC-3: a documented basis is a hypothesis, and
// the way this adapter avoids the mirrored-body failure its sibling paid for is
// by asserting against a person who was recorded turning a labelled direction.
//
// So the rotation half is checked **physically** — a direction is rotated and
// the answer compared against where a body that did that has to end up — rather
// than component against component. A component test agrees with a mirrored
// conversion in every axis-aligned pose, which is exactly the failure that
// stays invisible until something turns
// ([MOTION_CONTRACT.md](../../../../docs/design/MOTION_CONTRACT.md), "The
// canonical basis, stated").
//
// The corpus half asserts one thing and states it narrowly: every message the
// decoder accepts, this layer converts. It carries no expected canonical values
// per capture, because the generated corpus's numbers are this repository's own
// invention and a fixture cannot verify a basis — only a labelled session can,
// and that is what the constants above it are.
#include "vrmAdapterVrchatOsc/TrackingSpace.h"

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/PacketCapture.h"
#include "vrmAdapterVrchatOsc/TrackerMessage.h"

#include "motionCore/Compare.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using vrmAdapterVrchatOsc::Diagnostic;
using vrmAdapterVrchatOsc::DiagnosticCode;
using vrmAdapterVrchatOsc::MapTrackerPosition;
using vrmAdapterVrchatOsc::MapTrackerRotation;
using vrmAdapterVrchatOsc::PacketCapture;
using vrmAdapterVrchatOsc::ToCanonicalPosition;
using vrmAdapterVrchatOsc::ToCanonicalRotation;
using vrmAdapterVrchatOsc::TrackerChannel;
using vrmAdapterVrchatOsc::TrackerMessage;

// ---------------------------------------------------------------------------
// What the 2026-08-30 session reported. Every number here was measured; none
// was chosen.
// ---------------------------------------------------------------------------

// `neutral-standing`, the 20-30 s window, one operator standing still. Four
// positions in the sender's own space, which is what makes the unit and the up
// axis readable: no other unit puts a person's head at 1.5 and their feet at
// 0.09, and no other component separates the three heights.
constexpr std::array<float, 3> kRestHead = {{0.0168f, 1.5178f, -0.0854f}};
constexpr std::array<float, 3> kRestHips = {{0.0046f, 0.8922f, -0.1284f}};
constexpr std::array<float, 3> kRestFootA = {{-0.1599f, 0.0921f, -0.1030f}};
constexpr std::array<float, 3> kRestFootB = {{0.0915f, 0.0918f, -0.2467f}};

// `head-turn`, whose label is the evidence: "5 s still, head left, centre,
// right, centre, holding 1 s at each". These are the head's rotation at the
// four holds, in the order they were performed. **Nothing in the numbers says
// which way the operator turned** — the take's own note does, and a session
// that had not written it down could not answer the handedness question at all.
constexpr std::array<float, 3> kHeadStill = {{-6.66f, -1.14f, -1.55f}};
constexpr std::array<float, 3> kHeadTurnedLeft = {{-11.97f, -77.48f, 1.89f}};
constexpr std::array<float, 3> kHeadCentred = {{-12.68f, -1.77f, 0.90f}};
constexpr std::array<float, 3> kHeadTurnedRight = {{-18.36f, 82.53f, -2.59f}};

// `walk-root-motion`: "4 steps forward". The hips before and after, in the
// sender's space. 1.401 m of horizontal travel at a yaw of +8.9 deg, which is
// what says the forward axis is +Z rather than -Z or +X.
constexpr std::array<float, 3> kWalkFrom = {{0.224f, 0.897f, -1.185f}};
constexpr std::array<float, 3> kWalkTo = {{0.190f, 0.893f, 0.216f}};

// The canonical axes, named so the assertions below read as claims about a body
// rather than about component indices. Canonical space is right-handed, +Y up,
// +Z forward, and the avatar faces +Z — so +X is the avatar's **left**.
const pxr::GfVec3f kCanonicalLeft(1.0f, 0.0f, 0.0f);
const pxr::GfVec3f kCanonicalUp(0.0f, 1.0f, 0.0f);
const pxr::GfVec3f kCanonicalForward(0.0f, 0.0f, 1.0f);

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

bool
NearlyEqual(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

bool
NearVector(const pxr::GfVec3f& a, const pxr::GfVec3f& b, float tolerance)
{
    return NearlyEqual(a[0], b[0], tolerance) && NearlyEqual(a[1], b[1], tolerance)
           && NearlyEqual(a[2], b[2], tolerance);
}

// How far a rotated body's lateral axis leaves the horizontal plane, in
// degrees. This is the quantity the Euler order is measured by: a head turning
// left and right does not roll, so a composition that puts the yaw anywhere but
// outermost drags the head's pitch into apparent roll as the turn grows.
//
// The mirror does not change it. `R_canonical = M R_sender M` for
// `M = diag(-1, 1, 1)`, so the canonical lateral axis is the negated mirror of
// the sender's and its vertical component keeps its magnitude — which is why
// this one number can be read on either side of the change of basis.
float
LateralTiltDegrees(const pxr::GfQuatf& rotation)
{
    const pxr::GfVec3f lateral = rotation.Transform(kCanonicalLeft);
    const float clamped = std::fmin(1.0f, std::fmax(-1.0f, lateral[1]));
    return std::fabs(std::asin(clamped)) / kDegreesToRadians;
}

pxr::GfQuatf
AxisRotation(std::size_t axis, float degrees)
{
    const float half = 0.5f * degrees * kDegreesToRadians;
    pxr::GfVec3f imaginary(0.0f);
    imaginary[axis] = std::sin(half);
    return pxr::GfQuatf(std::cos(half), imaginary);
}

pxr::GfQuatf
Mirrored(const pxr::GfQuatf& sender)
{
    const pxr::GfVec3f imaginary = sender.GetImaginary();
    pxr::GfQuatf canonical(
        sender.GetReal(),
        pxr::GfVec3f(imaginary[0], -imaginary[1], -imaginary[2]));
    canonical.Normalize();
    return canonical;
}

// One of the three orders this session **refuses**: the yaw applied first, so
// the pitch and roll turn about axes the yaw has already moved. Built here so
// that the measurement is in the suite rather than only in the report — the
// test below shows what this composition does to a labelled head turn, which is
// why the shipped one does not use it.
pxr::GfQuatf
YawInnermostRotation(const std::array<float, 3>& eulerDegrees)
{
    return Mirrored(AxisRotation(2, eulerDegrees[2])
                    * AxisRotation(0, eulerDegrees[0])
                    * AxisRotation(1, eulerDegrees[1]));
}

TrackerMessage
MakeMessage(std::string_view segment, TrackerChannel channel,
            const std::array<float, 3>& values)
{
    TrackerMessage message;
    message.tracker.segment = segment;
    message.channel = channel;
    message.values = values;
    return message;
}

// ---------------------------------------------------------------------------
// The basis, one claim per take
// ---------------------------------------------------------------------------

// Metres, +Y up, and a floor at zero: the three heights a standing person
// produces survive the conversion unchanged, because canonical space and this
// sender agree about the up axis and about the unit. If either disagreed, this
// is where a head would arrive at 151.78 or a foot below the floor.
void
TestTheUnitAndTheUpAxisSurviveTheConversion()
{
    const pxr::GfVec3f head = ToCanonicalPosition(kRestHead);
    const pxr::GfVec3f hips = ToCanonicalPosition(kRestHips);
    const pxr::GfVec3f footA = ToCanonicalPosition(kRestFootA);
    const pxr::GfVec3f footB = ToCanonicalPosition(kRestFootB);

    assert(NearlyEqual(head[1], 1.5178f, 1e-6f));
    assert(NearlyEqual(hips[1], 0.8922f, 1e-6f));
    assert(NearlyEqual(footA[1], 0.0921f, 1e-6f));
    assert(NearlyEqual(footB[1], 0.0918f, 1e-6f));

    // A person, in metres: the head is above the hips, the hips above the feet,
    // and the two feet are within a centimetre of each other.
    assert(head[1] > hips[1] && hips[1] > footA[1]);
    assert(NearlyEqual(footA[1], footB[1], 0.01f));
}

// The change of basis touches X and only X. The two axes canonical space and
// this sender agree about are carried verbatim, which is the half of the
// conversion that must *not* do anything.
void
TestOnlyTheFirstComponentIsMirrored()
{
    for (const std::array<float, 3>& sample :
         {kRestHead, kRestHips, kRestFootA, kRestFootB, kWalkFrom, kWalkTo}) {
        const pxr::GfVec3f canonical = ToCanonicalPosition(sample);
        assert(NearlyEqual(canonical[0], -sample[0], 1e-6f));
        assert(NearlyEqual(canonical[1], sample[1], 1e-6f));
        assert(NearlyEqual(canonical[2], sample[2], 1e-6f));
    }
}

// Walking forward travels canonical forward. The avatar this motion is aimed at
// faces +Z by its own specification, so a sign error here is a character
// walking backwards — which is the one basis failure that would be noticed
// immediately, and the reason the forward axis is the cheapest of the four to
// measure.
void
TestWalkingForwardTravelsCanonicalForward()
{
    const pxr::GfVec3f from = ToCanonicalPosition(kWalkFrom);
    const pxr::GfVec3f to = ToCanonicalPosition(kWalkTo);
    const pxr::GfVec3f travel = to - from;

    assert(travel[2] > 1.3f);
    // Four steps in a straight line: the sideways and vertical components are
    // an order of magnitude below the forward one.
    assert(std::fabs(travel[0]) < 0.1f);
    assert(std::fabs(travel[1]) < 0.1f);
}

// **The handedness claim, and the one that needed a labelled take.** The
// operator turned their head to their own left; the canonical rotation must
// therefore carry the head's forward axis toward the avatar's left, which is
// +X in a right-handed, +Z-facing space. A conversion that skipped the mirror —
// or applied it twice — passes every test above this one and fails here.
void
TestALabelledLeftTurnFacesTheAvatarsLeft()
{
    const pxr::GfVec3f facingLeft =
        ToCanonicalRotation(kHeadTurnedLeft).Transform(kCanonicalForward);
    const pxr::GfVec3f facingRight =
        ToCanonicalRotation(kHeadTurnedRight).Transform(kCanonicalForward);

    assert(facingLeft[0] > 0.9f);
    assert(facingRight[0] < -0.9f);

    // And the two holds either side of them are the same head, facing forward:
    // the still and centred readings stay within 15 degrees of +Z, which is the
    // pitch the operator held throughout rather than any yaw.
    for (const std::array<float, 3>& sample : {kHeadStill, kHeadCentred}) {
        const pxr::GfVec3f facing =
            ToCanonicalRotation(sample).Transform(kCanonicalForward);
        assert(facing[2] > 0.95f);
        assert(std::fabs(facing[0]) < 0.1f);
    }
}

// The mirror is a mirror rather than a rotation, stated as its own claim
// because the two are indistinguishable on the axis they share. A sender
// rotation about the vertical by +90 degrees is a canonical rotation about the
// vertical by -90: same axis, opposite sense, which is what `det(M) = -1` does
// to the imaginary part.
void
TestASenderYawBecomesTheOppositeCanonicalYaw()
{
    const pxr::GfQuatf canonical = ToCanonicalRotation({{0.0f, 90.0f, 0.0f}});
    const pxr::GfVec3f facing = canonical.Transform(kCanonicalForward);

    // +90 in a left-handed space turns the body to its right, which is -X here.
    assert(NearVector(facing, pxr::GfVec3f(-1.0f, 0.0f, 0.0f), 1e-5f));
    // The vertical axis is the mirror's fixed axis, so it is untouched.
    assert(NearVector(canonical.Transform(kCanonicalUp), kCanonicalUp, 1e-5f));
    assert(motion::AngleBetween(canonical,
                                AxisRotation(1, -90.0f))
           < motion::MotionTolerance{}.angle);
}

// **The Euler order, as far as this session measures it.** The head does not
// roll through a turn it was labelled as making, so the composition that keeps
// the head's lateral axis horizontal is the sender's — and the one that puts
// the yaw innermost drags 12 to 18 degrees of pitch into apparent roll at the
// ends of an 80 degree turn.
//
// The still and centred holds are in here to show what the labelled turn buys:
// with a yaw near zero the two compositions agree to within a degree, so a
// session of a person standing still could not have separated them at all.
void
TestTheYawIsOutermost()
{
    for (const std::array<float, 3>& sample : {kHeadStill, kHeadCentred}) {
        assert(LateralTiltDegrees(ToCanonicalRotation(sample)) < 2.0f);
        assert(std::fabs(LateralTiltDegrees(ToCanonicalRotation(sample))
                         - LateralTiltDegrees(YawInnermostRotation(sample)))
               < 1.0f);
    }

    for (const std::array<float, 3>& sample :
         {kHeadTurnedLeft, kHeadTurnedRight}) {
        assert(LateralTiltDegrees(ToCanonicalRotation(sample)) < 3.0f);
        assert(LateralTiltDegrees(YawInnermostRotation(sample)) > 10.0f);
    }
}

// Degrees, and a wrap this sender really emits: the session's angles run from
// -0.0053 to 359.9942, so a tracker at rest reports its components on both
// sides of the wrap within one take. The two readings are the same orientation
// and the conversion has to say so.
void
TestTheAngleUnitIsDegreesAndTheWrapIsNotADiscontinuity()
{
    const pxr::GfQuatf identity = ToCanonicalRotation({{0.0f, 0.0f, 0.0f}});
    assert(motion::AngleBetween(identity, pxr::GfQuatf::GetIdentity())
           < motion::MotionTolerance{}.angle);

    const pxr::GfQuatf justUnder =
        ToCanonicalRotation({{359.9942f, 359.9942f, 359.9942f}});
    const pxr::GfQuatf justOver =
        ToCanonicalRotation({{-0.0058f, -0.0058f, -0.0058f}});
    assert(motion::AngleBetween(justUnder, justOver)
           < motion::MotionTolerance{}.angle);

    // A radian reading of the same numbers would put this sample most of a full
    // turn away from where a degree reading puts it. 30 degrees is 0.52 rad, so
    // the two readings differ by 29.5 degrees of yaw and the check is that the
    // conversion lands on the first.
    const pxr::GfQuatf thirtyDegrees = ToCanonicalRotation({{0.0f, 30.0f, 0.0f}});
    assert(motion::AngleBetween(thirtyDegrees, AxisRotation(1, -30.0f))
           < motion::MotionTolerance{}.angle);
}

// Every rotation this layer produces is an orientation: unit length, for any
// three finite angles including the wrapped ones the wire carries.
void
TestEveryRotationIsUnitLength()
{
    for (const std::array<float, 3>& sample :
         {kHeadStill, kHeadTurnedLeft, kHeadCentred, kHeadTurnedRight,
          std::array<float, 3>{{359.9942f, 180.0f, -0.0053f}},
          std::array<float, 3>{{720.0f, -540.0f, 45.0f}}}) {
        const pxr::GfQuatf canonical = ToCanonicalRotation(sample);
        assert(NearlyEqual(canonical.GetLength(), 1.0f, 1e-5f));
    }
}

// The header publishes four constants as the reader-checkable statement of what
// the conversion does — the unit, the angle unit, which component is mirrored,
// and the determinant that mirror has. A constant nothing asserts is a comment
// with a type, so this is where they are made load-bearing: each one *derives*
// what the arithmetic below must produce, and a change to either side alone
// fails here.
void
TestThePublishedConstantsDescribeTheArithmetic()
{
    using vrmAdapterVrchatOsc::TrackingSpaceAngleUnitInDegrees;
    using vrmAdapterVrchatOsc::TrackingSpaceDeterminant;
    using vrmAdapterVrchatOsc::TrackingSpaceMirroredComponent;
    using vrmAdapterVrchatOsc::TrackingSpaceUnitInMeters;

    // One unit along each axis converts to that many metres, and exactly the
    // named component comes back negated.
    for (int axis = 0; axis < 3; ++axis) {
        std::array<float, 3> unit = {{0.0f, 0.0f, 0.0f}};
        unit[static_cast<std::size_t>(axis)] = 1.0f;
        const pxr::GfVec3f canonical = ToCanonicalPosition(unit);

        const float sign = axis == TrackingSpaceMirroredComponent ? -1.0f : 1.0f;
        const float metres = sign * static_cast<float>(TrackingSpaceUnitInMeters);
        for (int slot = 0; slot < 3; ++slot) {
            const float expected = slot == axis ? metres : 0.0f;
            assert(NearlyEqual(canonical[slot], expected, 1e-6f));
        }
    }

    // A quarter turn about each axis, in the sender's angle unit. `(w, det(M) M
    // v)` leaves the mirrored axis's rotation alone and reverses the other two,
    // so the sign of each is the determinant times the mirror's own sign —
    // written from the constants rather than from the answer.
    const float quarterTurn = static_cast<float>(90.0 / TrackingSpaceAngleUnitInDegrees);
    for (int axis = 0; axis < 3; ++axis) {
        std::array<float, 3> angles = {{0.0f, 0.0f, 0.0f}};
        angles[static_cast<std::size_t>(axis)] = quarterTurn;
        const pxr::GfQuatf canonical = ToCanonicalRotation(angles);

        const int mirror = axis == TrackingSpaceMirroredComponent ? -1 : 1;
        const float sign = static_cast<float>(TrackingSpaceDeterminant * mirror);
        const float half = std::sin(45.0f * kDegreesToRadians);

        assert(NearlyEqual(canonical.GetReal(), std::cos(45.0f * kDegreesToRadians),
                           1e-5f));
        for (int slot = 0; slot < 3; ++slot) {
            const float expected = slot == axis ? sign * half : 0.0f;
            assert(NearlyEqual(canonical.GetImaginary()[slot], expected, 1e-5f));
        }
    }
}

// ---------------------------------------------------------------------------
// The boundary
// ---------------------------------------------------------------------------

// The address is rebuilt from what the message carries, because a refusal at
// this layer has to name what the wire said. `head` and `1` occupy the same
// path position, and the rebuilt address is the one place that shows it.
void
TestTheSubjectIsTheAddressTheMessageCameFrom()
{
    assert(vrmAdapterVrchatOsc::TrackerMessageAddress(
               MakeMessage("head", TrackerChannel::Rotation, {{0, 0, 0}}))
           == "/tracking/trackers/head/rotation");
    assert(vrmAdapterVrchatOsc::TrackerMessageAddress(
               MakeMessage("3", TrackerChannel::Position, {{0, 0, 0}}))
           == "/tracking/trackers/3/position");
}

// A position handed to the rotation conversion is a caller's mistake and not a
// sender's, so it raises this adapter's "these are not the bytes I was
// promised" code. It matters because both channels are three floats: the
// arithmetic cannot tell them apart, and a position read as a rotation is a
// fraction of a degree from identity — indistinguishable from a tracker at
// rest.
void
TestTheChannelGuardRefusesAValueItCouldNotTellApart()
{
    const TrackerMessage position =
        MakeMessage("1", TrackerChannel::Position, kRestHips);

    pxr::GfQuatf rotation = pxr::GfQuatf::GetIdentity();
    Diagnostic diagnostic;
    assert(!MapTrackerRotation(position, &rotation, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(diagnostic.subject == "/tracking/trackers/1/position");
    // Untouched on refusal, so a caller that ignored the return value gets the
    // value it already had rather than a plausible new one.
    assert(rotation == pxr::GfQuatf::GetIdentity());

    const TrackerMessage rotationMessage =
        MakeMessage("1", TrackerChannel::Rotation, kHeadTurnedLeft);
    pxr::GfVec3f point(7.0f, 7.0f, 7.0f);
    assert(!MapTrackerPosition(rotationMessage, &point, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(NearVector(point, pxr::GfVec3f(7.0f, 7.0f, 7.0f), 0.0f));

    // And the right channel converts, so the guard is a guard rather than a
    // refusal of everything.
    assert(MapTrackerPosition(position, &point));
    assert(NearVector(point, ToCanonicalPosition(kRestHips), 1e-6f));
    assert(MapTrackerRotation(rotationMessage, &rotation));
    assert(motion::AngleBetween(rotation, ToCanonicalRotation(kHeadTurnedLeft))
           < motion::MotionTolerance{}.angle);
}

// The one refusal about a *value*, which the wire path cannot produce because
// the message decoder already refuses it — and which a caller constructing a
// `TrackerMessage` by hand can.
void
TestANonFiniteComponentIsRefusedHereToo()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    for (const std::array<float, 3>& values :
         {std::array<float, 3>{{nan, 0.0f, 0.0f}},
          std::array<float, 3>{{0.0f, infinity, 0.0f}},
          std::array<float, 3>{{0.0f, 0.0f, -infinity}}}) {
        pxr::GfVec3f point(0.0f);
        Diagnostic diagnostic;
        assert(!MapTrackerPosition(
            MakeMessage("2", TrackerChannel::Position, values), &point,
            &diagnostic));
        assert(diagnostic.code == DiagnosticCode::CoordinateInvalid);

        pxr::GfQuatf rotation = pxr::GfQuatf::GetIdentity();
        assert(!MapTrackerRotation(
            MakeMessage("2", TrackerChannel::Rotation, values), &rotation,
            &diagnostic));
        assert(diagnostic.code == DiagnosticCode::CoordinateInvalid);
    }
}

// A null output is refused rather than dereferenced, and it is refused before
// the values are read — the same order the message decoder's structural guards
// use.
void
TestTheStructuralGuardRefusesRatherThanDereference()
{
    Diagnostic diagnostic;
    assert(!MapTrackerPosition(
        MakeMessage("1", TrackerChannel::Position, kRestHips), nullptr,
        &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(!MapTrackerRotation(
        MakeMessage("1", TrackerChannel::Rotation, kHeadStill), nullptr,
        &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);

    // And a refusal with nowhere to report it still refuses.
    assert(!MapTrackerPosition(
        MakeMessage("1", TrackerChannel::Rotation, kRestHips), nullptr));
}

// ---------------------------------------------------------------------------
// The corpus: every message the decoder accepts, this layer converts
// ---------------------------------------------------------------------------

int
CheckCorpus(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> captures;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()
            && entry.path().extension() == ".vrchatoscpackets") {
            captures.push_back(entry.path());
        }
    }
    if (captures.empty()) {
        std::fprintf(stderr, "no captures under %s\n", root.string().c_str());
        return 1;
    }

    std::size_t converted = 0;
    std::size_t rotations = 0;
    std::size_t positions = 0;
    int failures = 0;

    for (const std::filesystem::path& path : captures) {
        PacketCapture capture;
        vrmAdapterVrchatOsc::PacketCaptureError error;
        if (!vrmAdapterVrchatOsc::ReadPacketCaptureFile(path.string(), &capture,
                                                        &error)) {
            std::fprintf(stderr, "%s:%zu: %s\n", path.string().c_str(),
                         error.line, error.message.c_str());
            ++failures;
            continue;
        }

        for (const vrmAdapterVrchatOsc::RecordedDatagram& datagram :
             capture.datagrams) {
            const vrmAdapterVrchatOsc::TrackerPacket packet =
                vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram.bytes);
            for (const TrackerMessage& message : packet.messages) {
                Diagnostic diagnostic;
                bool ok = false;
                if (message.channel == TrackerChannel::Position) {
                    pxr::GfVec3f point(0.0f);
                    ok = MapTrackerPosition(message, &point, &diagnostic);
                    if (ok && point != pxr::GfVec3f(0.0f)) {
                        ++positions;
                    }
                } else {
                    pxr::GfQuatf rotation = pxr::GfQuatf::GetIdentity();
                    ok = MapTrackerRotation(message, &rotation, &diagnostic);
                    if (ok
                        && motion::AngleBetween(rotation,
                                                pxr::GfQuatf::GetIdentity())
                               > motion::MotionTolerance{}.angle) {
                        ++rotations;
                    }
                }
                if (!ok) {
                    std::fprintf(stderr, "%s: %s refused a decoded message\n",
                                 path.string().c_str(),
                                 vrmAdapterVrchatOsc::TrackerMessageAddress(
                                     message)
                                     .c_str());
                    ++failures;
                    continue;
                }
                ++converted;
            }
        }
    }

    // A conversion that returned identity and zero for everything would pass
    // every loop above, so the corpus has to have moved something.
    if (rotations == 0 || positions == 0) {
        std::fprintf(stderr,
                     "the corpus produced %zu non-identity rotation(s) and %zu "
                     "non-zero position(s); the conversion would pass this "
                     "test by returning neither\n",
                     rotations, positions);
        ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus failure(s)\n", failures);
        return 1;
    }
    std::printf(
        "VRChat OSC tracking space: %zu message(s) converted from %zu "
        "capture(s)\n",
        converted, captures.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestTheUnitAndTheUpAxisSurviveTheConversion();
    TestOnlyTheFirstComponentIsMirrored();
    TestWalkingForwardTravelsCanonicalForward();
    TestALabelledLeftTurnFacesTheAvatarsLeft();
    TestASenderYawBecomesTheOppositeCanonicalYaw();
    TestTheYawIsOutermost();
    TestTheAngleUnitIsDegreesAndTheWrapIsNotADiscontinuity();
    TestEveryRotationIsUnitLength();
    TestThePublishedConstantsDescribeTheArithmetic();
    TestTheSubjectIsTheAddressTheMessageCameFrom();
    TestTheChannelGuardRefusesAValueItCouldNotTellApart();
    TestANonFiniteComponentIsRefusedHereToo();
    TestTheStructuralGuardRefusesRatherThanDereference();
    std::puts("vrmAdapterVrchatOsc tracking space tests passed");
    return 0;
}
