// SPDX-License-Identifier: Apache-2.0

#include "motionBvh/BvhDocument.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_set>

namespace motionBvh
{

namespace
{

constexpr std::array<std::string_view, BvhChannelCount> kChannelNames = {
    "Xposition", "Yposition", "Zposition",
    "Xrotation", "Yrotation", "Zrotation",
};

// ASCII only, and deliberately not `std::tolower`: that one consults the global
// C locale, which the host application owns and can change under us — the
// Turkish dotless 'i' turns `Xrotation` into a token this table no longer
// matches. A file's keywords are ASCII by definition.
constexpr char ToLowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool
EqualsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (ToLowerAscii(lhs[index]) != ToLowerAscii(rhs[index])) {
            return false;
        }
    }
    return true;
}

bool
Fail(Diagnostic* diagnostic, std::string detail, std::string subject = {})
{
    if (diagnostic) {
        *diagnostic =
            MakeDiagnostic(DiagnosticCode::ParseFailed, std::move(detail));
        diagnostic->subject = std::move(subject);
    }
    return false;
}

std::string
Count(std::size_t value)
{
    return std::to_string(value);
}

} // namespace

std::string_view
BvhChannelName(BvhChannel channel) noexcept
{
    const auto index = static_cast<std::size_t>(channel);
    if (index >= BvhChannelCount) {
        return {};
    }
    return kChannelNames[index];
}

std::optional<BvhChannel>
FindBvhChannel(std::string_view token) noexcept
{
    for (std::size_t index = 0; index < BvhChannelCount; ++index) {
        if (EqualsIgnoreCaseAscii(token, kChannelNames[index])) {
            return static_cast<BvhChannel>(index);
        }
    }
    return std::nullopt;
}

bool
BvhChannelIsPosition(BvhChannel channel) noexcept
{
    return channel == BvhChannel::Xposition || channel == BvhChannel::Yposition
           || channel == BvhChannel::Zposition;
}

bool
BvhChannelIsRotation(BvhChannel channel) noexcept
{
    return channel == BvhChannel::Xrotation || channel == BvhChannel::Yrotation
           || channel == BvhChannel::Zrotation;
}

const float*
BvhDocument::Frame(std::size_t frameIndex) const noexcept
{
    if (frameIndex >= frameCount || channelCount == 0) {
        return nullptr;
    }
    const std::size_t offset = frameIndex * channelCount;
    if (offset + channelCount > values.size()) {
        return nullptr;
    }
    return values.data() + offset;
}

std::optional<float>
BvhDocument::ChannelValue(std::size_t frameIndex, std::size_t jointIndex,
                          std::size_t channelIndex) const noexcept
{
    if (jointIndex >= joints.size()) {
        return std::nullopt;
    }
    const BvhJoint& joint = joints[jointIndex];
    if (channelIndex >= joint.channels.size()) {
        return std::nullopt;
    }
    const float* row = Frame(frameIndex);
    if (!row) {
        return std::nullopt;
    }
    const std::size_t column = joint.channelOffset + channelIndex;
    if (column >= channelCount) {
        return std::nullopt;
    }
    return row[column];
}

std::optional<std::size_t>
BvhDocument::FindJoint(std::string_view name) const
{
    for (std::size_t index = 0; index < joints.size(); ++index) {
        if (joints[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

std::vector<std::size_t>
BvhDocument::FindJoints(std::string_view name) const
{
    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index < joints.size(); ++index) {
        if (joints[index].name == name) {
            matches.push_back(index);
        }
    }
    return matches;
}

bool
BvhDocument::HasUniqueJointNames() const
{
    std::unordered_set<std::string_view> seen;
    seen.reserve(joints.size());
    for (const BvhJoint& joint : joints) {
        if (!seen.insert(joint.name).second) {
            return false;
        }
    }
    return true;
}

std::size_t
BvhDocument::Depth(std::size_t jointIndex) const noexcept
{
    if (jointIndex >= joints.size()) {
        return 0;
    }
    std::size_t depth = 0;
    int parent = joints[jointIndex].parent;
    // Parents are stored before their children, so this terminates on any
    // document `ValidateBvhDocument` accepts -- and on one it would reject,
    // the strictly decreasing index still bounds the walk.
    while (parent >= 0 && static_cast<std::size_t>(parent) < joints.size()) {
        ++depth;
        parent = joints[static_cast<std::size_t>(parent)].parent;
    }
    return depth;
}

std::size_t
BvhDocument::MaxDepth() const noexcept
{
    std::size_t deepest = 0;
    for (std::size_t index = 0; index < joints.size(); ++index) {
        const std::size_t depth = Depth(index);
        if (depth > deepest) {
            deepest = depth;
        }
    }
    return deepest;
}

std::size_t
BvhDocument::TotalDeclaredChannels() const noexcept
{
    std::size_t total = 0;
    for (const BvhJoint& joint : joints) {
        total += joint.channels.size();
    }
    return total;
}

bool
ValidateBvhDocument(const BvhDocument& document, Diagnostic* diagnostic)
{
    if (document.joints.empty()) {
        return Fail(diagnostic, "document carries no joints");
    }
    if (document.joints[0].parent != -1) {
        return Fail(diagnostic, "joint 0 is not the root", document.joints[0].name);
    }

    std::size_t offset = 0;
    for (std::size_t index = 0; index < document.joints.size(); ++index) {
        const BvhJoint& joint = document.joints[index];
        if (index > 0) {
            if (joint.parent < 0) {
                return Fail(diagnostic,
                            "joint " + Count(index) + " has no parent; a BVH "
                            "document has exactly one root",
                            joint.name);
            }
            if (static_cast<std::size_t>(joint.parent) >= index) {
                return Fail(diagnostic,
                            "joint " + Count(index) + " is stored before its "
                            "parent " + Count(static_cast<std::size_t>(joint.parent)),
                            joint.name);
            }
        }
        if (joint.channelOffset != offset) {
            return Fail(diagnostic,
                        "joint " + Count(index) + " starts at column "
                        + Count(joint.channelOffset) + ", declaration order puts "
                        "it at " + Count(offset),
                        joint.name);
        }
        offset += joint.channels.size();

        for (const BvhChannel channel : joint.channels) {
            if (static_cast<std::size_t>(channel) >= BvhChannelCount) {
                return Fail(diagnostic, "joint " + Count(index)
                                            + " carries an unknown channel",
                            joint.name);
            }
        }
        for (const float component :
             {joint.offset.x, joint.offset.y, joint.offset.z}) {
            if (!std::isfinite(component)) {
                if (diagnostic) {
                    *diagnostic = MakeDiagnostic(DiagnosticCode::NonFiniteValue,
                                                 "offset is not finite");
                    diagnostic->subject = joint.name;
                }
                return false;
            }
        }
        if (joint.endSiteOffset) {
            for (const float component : {joint.endSiteOffset->x,
                                          joint.endSiteOffset->y,
                                          joint.endSiteOffset->z}) {
                if (!std::isfinite(component)) {
                    if (diagnostic) {
                        *diagnostic =
                            MakeDiagnostic(DiagnosticCode::NonFiniteValue,
                                           "End Site offset is not finite");
                        diagnostic->subject = joint.name;
                    }
                    return false;
                }
            }
        }
    }

    if (document.channelCount != offset) {
        return Fail(diagnostic, "channelCount is " + Count(document.channelCount)
                                    + ", the joints declare " + Count(offset));
    }
    if (document.values.size() != document.frameCount * document.channelCount) {
        return Fail(diagnostic,
                    "values holds " + Count(document.values.size())
                        + " numbers, " + Count(document.frameCount) + " frames x "
                        + Count(document.channelCount) + " channels is "
                        + Count(document.frameCount * document.channelCount));
    }
    // A frame time of zero is legal below two frames and meaningless above it;
    // BvhParser.h carries the argument.
    if (!std::isfinite(document.frameTime) || document.frameTime < 0.0
        || (document.frameCount > 1 && document.frameTime <= 0.0)) {
        if (diagnostic) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.9g", document.frameTime);
            *diagnostic = MakeDiagnostic(
                DiagnosticCode::InvalidFrameTime,
                std::string("frame time is ") + buffer + " across "
                    + Count(document.frameCount) + " frame(s)");
        }
        return false;
    }
    for (std::size_t index = 0; index < document.values.size(); ++index) {
        if (!std::isfinite(document.values[index])) {
            if (diagnostic) {
                *diagnostic = MakeDiagnostic(
                    DiagnosticCode::NonFiniteValue,
                    "value " + Count(index) + " is not finite");
            }
            return false;
        }
    }
    return true;
}

} // namespace motionBvh
