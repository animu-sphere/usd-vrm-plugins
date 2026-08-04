// SPDX-License-Identifier: Apache-2.0

#include "motionBvh/BvhExtract.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace motionBvh
{

namespace
{

using motionSource::SourceAnimation;
using motionSource::SourceEulerAngles;
using motionSource::SourceEulerOrder;
using motionSource::SourceJoint;
using motionSource::SourceJointTrack;
using motionSource::SourceSkeleton;
using motionSource::SourceVec3;

constexpr std::size_t kNoColumn = static_cast<std::size_t>(-1);

bool
Refuse(Diagnostic* diagnostic, DiagnosticCode code, const std::string& sourceId,
       std::string subject, std::string detail)
{
    if (diagnostic) {
        *diagnostic = MakeDiagnostic(code, std::move(detail));
        diagnostic->source = sourceId;
        diagnostic->subject = std::move(subject);
    }
    return false;
}

// 0 for X, 1 for Y, 2 for Z, whichever half of the channel set the token is
// from. The two halves are asked the same question -- which axis is this -- and
// a second table for the position half would be a table that can disagree.
int
ChannelAxis(BvhChannel channel) noexcept
{
    switch (channel) {
    case BvhChannel::Xposition:
    case BvhChannel::Xrotation:
        return 0;
    case BvhChannel::Yposition:
    case BvhChannel::Yrotation:
        return 1;
    case BvhChannel::Zposition:
    case BvhChannel::Zrotation:
        return 2;
    default:
        return -1;
    }
}

// The declared rotation axes spelled the way `motionSource` spells an order,
// e.g. "ZXY". Routing through that vocabulary rather than through a table here
// is the point: the layer that defines what an Euler order *is* is the layer
// that decides which triples are ones, so a repeated axis comes back as no
// order without this file holding an opinion about it.
std::optional<SourceEulerOrder>
OrderFromAxes(const std::array<int, 3>& axes, std::string* spelling)
{
    constexpr char kAxisLetters[3] = {'X', 'Y', 'Z'};
    spelling->clear();
    for (const int axis : axes) {
        if (axis < 0 || axis > 2) {
            return std::nullopt;
        }
        spelling->push_back(kAxisLetters[axis]);
    }
    return motionSource::FindSourceEulerOrder(*spelling);
}

// Where one joint's values sit in a motion row, worked out once for the whole
// clip rather than per frame: the channel declaration is fixed for the file and
// re-deriving it 853 times would be the same answer 853 times.
struct JointLayout
{
    // Column of each position component in a motion row, or `kNoColumn` when
    // the joint did not animate that component.
    std::array<std::size_t, 3> position = {kNoColumn, kNoColumn, kNoColumn};
    // Columns of the three rotation channels in **declaration** order, which is
    // the order `SourceEulerAngles` stores its three angles in.
    std::array<std::size_t, 3> rotation = {kNoColumn, kNoColumn, kNoColumn};
    bool hasPosition = false;
    bool hasRotation = false;
    SourceEulerOrder order = SourceEulerOrder::XYZ;
};

bool
BuildLayout(const BvhJoint& joint, JointLayout* layout, Diagnostic* diagnostic,
            const std::string& sourceId)
{
    std::array<int, 3> rotationAxes = {-1, -1, -1};
    std::size_t rotationCount = 0;

    for (std::size_t index = 0; index < joint.channels.size(); ++index) {
        const BvhChannel channel = joint.channels[index];
        const int axis = ChannelAxis(channel);
        const std::size_t column = joint.channelOffset + index;
        if (axis < 0) {
            return Refuse(diagnostic, DiagnosticCode::UnsupportedChannel,
                          sourceId, joint.name,
                          "channel is not one this format model represents");
        }
        if (BvhChannelIsPosition(channel)) {
            if (layout->position[static_cast<std::size_t>(axis)] != kNoColumn) {
                // Not in the frozen set, so it is `VRM_BVH_PARSE_FAILED` with a
                // precise detail rather than a new code (Diagnostics.h). Two
                // columns claiming one component is unreadable rather than
                // unmapped: taking either one would be a coin toss nobody is
                // told about.
                return Refuse(
                    diagnostic, DiagnosticCode::ParseFailed, sourceId,
                    joint.name,
                    "joint declares the same position channel twice");
            }
            layout->position[static_cast<std::size_t>(axis)] = column;
            layout->hasPosition = true;
            continue;
        }
        if (rotationCount < rotationAxes.size()) {
            rotationAxes[rotationCount] = axis;
            layout->rotation[rotationCount] = column;
        }
        ++rotationCount;
    }

    if (rotationCount == 0) {
        return true;
    }
    if (rotationCount != rotationAxes.size()) {
        return Refuse(diagnostic, DiagnosticCode::InvalidRotationOrder,
                      sourceId, joint.name,
                      "joint declares " + std::to_string(rotationCount)
                          + " rotation channels; an Euler order needs three");
    }
    std::string spelling;
    const std::optional<SourceEulerOrder> order =
        OrderFromAxes(rotationAxes, &spelling);
    if (!order) {
        return Refuse(diagnostic, DiagnosticCode::InvalidRotationOrder,
                      sourceId, joint.name,
                      "rotation channels declare '" + spelling
                          + "', which is not an axis order");
    }
    layout->order = *order;
    layout->hasRotation = true;
    return true;
}

SourceVec3
ToSourceVec3(const BvhVec3& value) noexcept
{
    SourceVec3 out;
    out.x = value.x;
    out.y = value.y;
    out.z = value.z;
    return out;
}

} // namespace

std::string_view
BvhFormatLabel() noexcept
{
    return "bvh";
}

bool
ExtractBvhSource(const BvhDocument& document, SourceSkeleton* skeleton,
                 SourceAnimation* animation, Diagnostic* diagnostic,
                 const BvhExtractOptions& options)
{
    if (skeleton == nullptr || animation == nullptr) {
        return Refuse(diagnostic, DiagnosticCode::ParseFailed, options.sourceId,
                      {}, "no place to put the extracted rig or animation");
    }
    // The parser produces nothing a `ValidateBvhDocument` refuses, so this is
    // not a re-check of it -- it is the check on the *other* caller, the one
    // that assembled a document by hand (BvhDocument.h). Everything below reads
    // `channelOffset` and indexes `values` directly, and both are invariants
    // that validation is what establishes.
    if (!ValidateBvhDocument(document, diagnostic)) {
        if (diagnostic && diagnostic->source.empty()) {
            diagnostic->source = options.sourceId;
        }
        return false;
    }

    SourceSkeleton rig;
    rig.joints.reserve(document.joints.size());
    for (const BvhJoint& joint : document.joints) {
        SourceJoint out;
        out.name = joint.name;
        out.parent = joint.parent;
        out.restTranslation = ToSourceVec3(joint.offset);
        // A rest rotation stays unset: BVH states none, and an identity default
        // would make a file that says nothing indistinguishable from one that
        // says "identity" (SourceSkeleton.h).
        if (joint.endSiteOffset) {
            out.tipOffset = ToSourceVec3(*joint.endSiteOffset);
        }
        rig.joints.push_back(std::move(out));
    }

    std::vector<JointLayout> layouts(document.joints.size());
    for (std::size_t index = 0; index < document.joints.size(); ++index) {
        if (!BuildLayout(document.joints[index], &layouts[index], diagnostic,
                         options.sourceId)) {
            return false;
        }
    }

    SourceAnimation clip;
    clip.frameCount = document.frameCount;
    clip.frameTime = document.frameTime;
    // BVH states no offset into a longer recording, so frame 0 is the origin of
    // this clip's own timeline. A reader with an offset to keep would say so
    // here; this one has none to invent.
    clip.startTime = 0.0;
    clip.provenance.format = std::string(BvhFormatLabel());
    clip.provenance.sourceId = options.sourceId;
    clip.tracks.resize(document.joints.size());

    for (std::size_t index = 0; index < document.joints.size(); ++index) {
        const BvhJoint& joint = document.joints[index];
        const JointLayout& layout = layouts[index];
        SourceJointTrack& track = clip.tracks[index];
        if (layout.hasPosition) {
            track.translations.reserve(document.frameCount);
        }
        if (layout.hasRotation) {
            track.eulerAngles.reserve(document.frameCount);
            track.eulerOrder = layout.order;
            // The format's answer, not a producer's: see BvhExtract.h.
            track.angleUnit = motionSource::SourceAngleUnit::Degrees;
        }
        for (std::size_t frame = 0; frame < document.frameCount; ++frame) {
            const float* row = document.Frame(frame);
            if (row == nullptr) {
                return Refuse(diagnostic, DiagnosticCode::FrameWidthMismatch,
                              options.sourceId, joint.name,
                              "frame " + std::to_string(frame)
                                  + " carries no row");
            }
            if (layout.hasPosition) {
                // A component the file did not animate is the one the hierarchy
                // stated, not zero. See BvhExtract.h.
                SourceVec3 translation = ToSourceVec3(joint.offset);
                float* const components[3] = {&translation.x, &translation.y,
                                              &translation.z};
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    if (layout.position[axis] != kNoColumn) {
                        *components[axis] = row[layout.position[axis]];
                    }
                }
                track.translations.push_back(translation);
            }
            if (layout.hasRotation) {
                SourceEulerAngles angles;
                angles.first = row[layout.rotation[0]];
                angles.second = row[layout.rotation[1]];
                angles.third = row[layout.rotation[2]];
                track.eulerAngles.push_back(angles);
            }
        }
    }

    // A document `ValidateBvhDocument` accepted can still describe a rig the
    // value model refuses: BVH lets a joint carry both an `End Site` and a
    // child, and a chain that continues has not ended (SourceSkeleton.h). One
    // pass over values already built is what turns that into a refusal instead
    // of an invalid value reaching a converter, and the reason it carries is the
    // validator's own words rather than a guess at which shape was met.
    std::string reason;
    if (!motionSource::ValidateSourceSkeleton(rig, &reason)) {
        return Refuse(diagnostic, DiagnosticCode::ParseFailed, options.sourceId,
                      {}, "the hierarchy is not a source rig: " + reason);
    }
    if (!motionSource::ValidateSourceAnimation(clip, rig, &reason)) {
        return Refuse(diagnostic, DiagnosticCode::ParseFailed, options.sourceId,
                      {}, "the motion is not a source animation: " + reason);
    }

    *skeleton = std::move(rig);
    *animation = std::move(clip);
    return true;
}

} // namespace motionBvh
