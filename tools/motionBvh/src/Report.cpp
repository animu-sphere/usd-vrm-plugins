// SPDX-License-Identifier: Apache-2.0
#include "Report.h"

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace motionBvhTool
{
namespace
{

// `%.7g` is the shortest form that round-trips a float. Widen it and the frame
// times real writers emit come back as `0.0333333015`, which reads as a
// difference between this report and the file when there is none.
std::string
Value(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.7g", static_cast<double>(value));
    return buffer;
}

// The frame time is the file's own double, so it gets the double's digits.
std::string
Seconds(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// Deliberately coarse, and printed with a `~`: a file declaring 0.0166667 is a
// 60 Hz file whose frame time was written to six places, and reporting
// "59.9999 Hz" would invite a reader to go looking for the missing hundredth.
std::string
Rate(double frameTime)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4g", 1.0 / frameTime);
    return buffer;
}

std::string
Offset(const motionBvh::BvhVec3& offset)
{
    return "(" + Value(offset.x) + ", " + Value(offset.y) + ", "
        + Value(offset.z) + ")";
}

std::string
Indent(std::size_t levels)
{
    return std::string(levels * 2, ' ');
}

std::string
RightAligned(std::size_t value, std::size_t width)
{
    std::string text = std::to_string(value);
    if (text.size() < width) {
        text.insert(text.begin(), width - text.size(), ' ');
    }
    return text;
}

std::size_t
IndexWidth(std::size_t count)
{
    return std::to_string(count == 0 ? 0 : count - 1).size();
}

// "[2] Hand.Xrotation" — the index is not decoration. Two joints may share a
// name, and a column that said only `Hand.Xrotation` would leave a profile
// author unable to tell which one it belongs to.
std::string
ChannelLabel(const motionBvh::BvhDocument& document, std::size_t jointIndex,
             motionBvh::BvhChannel channel)
{
    return "[" + std::to_string(jointIndex) + "] "
        + document.joints[jointIndex].name + "."
        + std::string(motionBvh::BvhChannelName(channel));
}

} // namespace

void
PrintSummary(std::ostream& out, const motionBvh::BvhDocument& document,
             const std::string& source)
{
    const std::size_t levels =
        document.joints.empty() ? 0 : document.MaxDepth() + 1;

    out << "source:    " << source << "\n";
    out << "joints:    " << document.joints.size() << "\n";
    out << "depth:     " << levels << " level(s)\n";
    out << "channels:  " << document.channelCount << " per frame\n";
    out << "frames:    " << document.frameCount << "\n";
    out << "frameTime: " << Seconds(document.frameTime) << " s";
    if (document.frameTime > 0.0) {
        out << " (~" << Rate(document.frameTime) << " Hz)";
    }
    out << "\n";

    // Printed only when there are repetitions, the way a diagnostic prints
    // `recoverable` only when it is: a line saying "all names distinct" on
    // every ordinary file is where the one file that matters stops standing
    // out.
    if (!document.HasUniqueJointNames()) {
        std::map<std::string, std::size_t> counts;
        std::vector<std::string> order;
        for (const motionBvh::BvhJoint& joint : document.joints) {
            if (++counts[joint.name] == 2) {
                order.push_back(joint.name);
            }
        }
        out << "repeated:  ";
        for (std::size_t i = 0; i < order.size(); ++i) {
            out << (i == 0 ? "" : ", ") << order[i] << " ("
                << counts[order[i]] << ")";
        }
        out << "\n";
    }
}

void
PrintHierarchy(std::ostream& out, const motionBvh::BvhDocument& document)
{
    out << "hierarchy (declaration order; parent before child)\n";
    for (std::size_t index = 0; index < document.joints.size(); ++index) {
        const motionBvh::BvhJoint& joint = document.joints[index];
        const std::size_t depth = document.Depth(index);

        out << Indent(depth + 1) << "[" << index << "] " << joint.name
            << "  offset=" << Offset(joint.offset)
            << "  channels=" << joint.channels.size();
        if (!joint.channels.empty()) {
            out << " ";
            for (std::size_t i = 0; i < joint.channels.size(); ++i) {
                out << (i == 0 ? "" : " ")
                    << motionBvh::BvhChannelName(joint.channels[i]);
            }
            out << "  column=" << joint.channelOffset;
        }
        out << "\n";

        // A terminator rather than a joint: it has an offset and no name, no
        // channels and no children, so it is printed under its parent rather
        // than given an index of its own (BvhDocument.h).
        if (joint.endSiteOffset) {
            out << Indent(depth + 2) << "end site  offset="
                << Offset(*joint.endSiteOffset) << "\n";
        }
    }
}

void
PrintChannelMap(std::ostream& out, const motionBvh::BvhDocument& document)
{
    out << "channel map (row column -> joint.channel)\n";
    const std::size_t width = IndexWidth(document.channelCount);
    for (std::size_t index = 0; index < document.joints.size(); ++index) {
        const motionBvh::BvhJoint& joint = document.joints[index];
        for (std::size_t i = 0; i < joint.channels.size(); ++i) {
            out << "  " << RightAligned(joint.channelOffset + i, width) << "  "
                << ChannelLabel(document, index, joint.channels[i]) << "\n";
        }
    }
}

void
PrintFrame(std::ostream& out, const motionBvh::BvhDocument& document,
           std::size_t frameIndex)
{
    out << "frame " << frameIndex << " of " << document.frameCount << " (t="
        << Seconds(static_cast<double>(frameIndex) * document.frameTime)
        << " s)\n";
    for (std::size_t index = 0; index < document.joints.size(); ++index) {
        const motionBvh::BvhJoint& joint = document.joints[index];
        out << "  [" << index << "] " << joint.name;
        if (joint.channels.empty()) {
            // `CHANNELS 0` is legal and means the file animates nothing about
            // this joint. Omitting the joint entirely would read as the file
            // not carrying it.
            out << "  (no channels)";
        }
        for (std::size_t i = 0; i < joint.channels.size(); ++i) {
            const std::optional<float> value =
                document.ChannelValue(frameIndex, index, i);
            out << "  " << motionBvh::BvhChannelName(joint.channels[i]) << "="
                << (value ? Value(*value) : std::string("?"));
        }
        out << "\n";
    }
}

void
PrintChannelRanges(std::ostream& out, const motionBvh::BvhDocument& document)
{
    out << "channel ranges over " << document.frameCount << " frame(s)\n";
    if (document.frameCount == 0 || document.channelCount == 0) {
        out << "  (nothing to measure)\n";
        return;
    }

    // One pass over the rows rather than one pass per column: a real session is
    // tens of thousands of rows wide enough that the difference is the tool
    // feeling instant or not.
    //
    // `seeded` rather than `frame == 0`, and the difference is not cosmetic: a
    // row this document cannot supply is skipped, and if that were ever the
    // *first* row then seeding on the frame index would be skipped with it —
    // leaving every column compared against a default-constructed 0.0f and a
    // plausible-looking table of wrong ranges. The parser cannot produce such a
    // document, which is precisely why the failure would be silent.
    std::vector<float> smallest(document.channelCount);
    std::vector<float> largest(document.channelCount);
    bool seeded = false;
    for (std::size_t frame = 0; frame < document.frameCount; ++frame) {
        const float* row = document.Frame(frame);
        if (!row) {
            continue;
        }
        for (std::size_t column = 0; column < document.channelCount; ++column) {
            if (!seeded || row[column] < smallest[column]) {
                smallest[column] = row[column];
            }
            if (!seeded || row[column] > largest[column]) {
                largest[column] = row[column];
            }
        }
        seeded = true;
    }
    if (!seeded) {
        out << "  (no readable rows)\n";
        return;
    }

    const std::size_t width = IndexWidth(document.channelCount);
    for (std::size_t index = 0; index < document.joints.size(); ++index) {
        const motionBvh::BvhJoint& joint = document.joints[index];
        for (std::size_t i = 0; i < joint.channels.size(); ++i) {
            const std::size_t column = joint.channelOffset + i;
            out << "  " << RightAligned(column, width) << "  "
                << ChannelLabel(document, index, joint.channels[i])
                << "  min=" << Value(smallest[column])
                << "  max=" << Value(largest[column]) << "\n";
        }
    }
}

} // namespace motionBvhTool
