// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/CaptureTrace.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <istream>
#include <locale>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace motion
{

namespace
{

constexpr const char* kMagic = "!motion-capture-trace";
constexpr int kPrecision = 6;

bool
Fail(CaptureTraceError* error, std::size_t line, std::string message)
{
    if (error) {
        error->line = line;
        error->message = std::move(message);
    }
    return false;
}

// Parsing runs in the classic locale throughout: a trace written on a machine
// with a comma decimal separator must still read on one without it.
std::istringstream
Tokenize(const std::string& line)
{
    std::istringstream stream(line);
    stream.imbue(std::locale::classic());
    return stream;
}

bool
ReadFloats(std::istringstream& stream, float* values, std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index) {
        if (!(stream >> values[index]) || !std::isfinite(values[index])) {
            return false;
        }
    }
    return true;
}

const char*
ContactName(FootContact contact)
{
    switch (contact) {
    case FootContact::InContact:
        return "contact";
    case FootContact::NotInContact:
        return "free";
    case FootContact::Unknown:
        break;
    }
    return "unknown";
}

bool
ParseContact(const std::string& token, FootContact* contact)
{
    if (token == "contact") {
        *contact = FootContact::InContact;
    } else if (token == "free") {
        *contact = FootContact::NotInContact;
    } else if (token == "unknown") {
        *contact = FootContact::Unknown;
    } else {
        return false;
    }
    return true;
}

struct FrameBuilder
{
    HumanoidPose pose;
    std::array<float, HumanBoneCount> confidence{};
    bool anyConfidence = false;

    HumanoidPose Build() const
    {
        HumanoidPose built = pose;
        if (anyConfidence) {
            built.confidence = confidence;
        }
        return built;
    }
};

} // namespace

bool
ReadCaptureTrace(std::istream& input, HumanoidAnimation* animation,
                 CaptureTraceError* error)
{
    if (!animation) {
        return Fail(error, 0, "no output animation was provided");
    }

    HumanoidAnimation result;
    result.source.kind = MotionSourceKind::LiveCapture;

    bool sawMagic = false;
    std::optional<FrameBuilder> frame;
    std::optional<double> frameRate;

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
            if (keyword != kMagic) {
                return Fail(error, lineNumber,
                            std::string("expected the trace magic '") + kMagic
                                + "'");
            }
            int version = 0;
            if (!(stream >> version)) {
                return Fail(error, lineNumber,
                            "the trace magic carries no format version");
            }
            if (version != CaptureTraceFormatVersion) {
                return Fail(error, lineNumber,
                            "unsupported capture trace format version "
                                + std::to_string(version));
            }
            sawMagic = true;
            continue;
        }

        if (keyword == "t") {
            double timestamp = 0.0;
            if (!(stream >> timestamp) || !std::isfinite(timestamp)) {
                return Fail(error, lineNumber, "'t' needs a finite timestamp");
            }
            if (frame) {
                if (timestamp <= frame->pose.timestamp) {
                    return Fail(error, lineNumber,
                                "frame timestamps must strictly increase");
                }
                result.samples.push_back(frame->Build());
            }
            frame.emplace();
            frame->pose.timestamp = timestamp;
            continue;
        }

        if (!frame) {
            // Still in the header block.
            std::string value;
            if (keyword == "provider" || keyword == "protocol"
                || keyword == "sourceId") {
                if (!(stream >> value)) {
                    return Fail(error, lineNumber,
                                "'" + keyword + "' needs a value");
                }
                if (keyword == "provider") {
                    result.source.provider = value;
                } else if (keyword == "protocol") {
                    result.source.protocol = value;
                } else {
                    result.source.sourceId = value;
                }
                continue;
            }
            if (keyword == "frameRate") {
                double rate = 0.0;
                if (!(stream >> rate) || !std::isfinite(rate) || rate <= 0.0) {
                    return Fail(error, lineNumber,
                                "'frameRate' needs a positive number");
                }
                frameRate = rate;
                continue;
            }
            return Fail(error, lineNumber,
                        "unknown header key '" + keyword + "'");
        }

        if (keyword == "b") {
            std::string boneName;
            if (!(stream >> boneName)) {
                return Fail(error, lineNumber, "'b' needs a bone name");
            }
            const std::optional<HumanBone> bone = FindHumanBone(boneName);
            if (!bone) {
                return Fail(error, lineNumber,
                            "unknown humanoid bone '" + boneName + "'");
            }
            float quaternion[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (!ReadFloats(stream, quaternion, 4)) {
                return Fail(error, lineNumber,
                            "'b " + boneName + "' needs a w x y z rotation");
            }
            const float lengthSquared = quaternion[0] * quaternion[0]
                + quaternion[1] * quaternion[1] + quaternion[2] * quaternion[2]
                + quaternion[3] * quaternion[3];
            if (lengthSquared <= 0.0f) {
                return Fail(error, lineNumber,
                            "'b " + boneName + "' has a zero-length rotation");
            }
            const std::size_t index = static_cast<std::size_t>(*bone);
            if (frame->pose.validRotations.test(index)) {
                return Fail(error, lineNumber,
                            "bone '" + boneName + "' appears twice in a frame");
            }
            frame->pose.localRotations[index] = pxr::GfQuatf(
                quaternion[0],
                pxr::GfVec3f(quaternion[1], quaternion[2], quaternion[3]));
            frame->pose.validRotations.set(index);

            float score = 0.0f;
            if (stream >> score) {
                if (!std::isfinite(score) || score < 0.0f || score > 1.0f) {
                    return Fail(error, lineNumber,
                                "confidence must lie in [0, 1]");
                }
                frame->confidence[index] = score;
                frame->anyConfidence = true;
            } else {
                frame->confidence[index] = 1.0f;
            }
            continue;
        }

        if (keyword == "root") {
            std::string field;
            if (!(stream >> field)) {
                return Fail(error, lineNumber, "'root' needs a field name");
            }
            float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (field == "rot") {
                if (!ReadFloats(stream, values, 4)) {
                    return Fail(error, lineNumber,
                                "'root rot' needs a w x y z rotation");
                }
                frame->pose.root.worldOrientation = pxr::GfQuatf(
                    values[0], pxr::GfVec3f(values[1], values[2], values[3]));
                frame->pose.root.hasOrientation = true;
                continue;
            }
            if (!ReadFloats(stream, values, 3)) {
                return Fail(error, lineNumber,
                            "'root " + field + "' needs an x y z vector");
            }
            const pxr::GfVec3f vector(values[0], values[1], values[2]);
            if (field == "pos") {
                frame->pose.root.worldPosition = vector;
                frame->pose.root.hasPosition = true;
            } else if (field == "vel") {
                frame->pose.root.linearVelocity = vector;
                frame->pose.root.hasLinearVelocity = true;
            } else if (field == "angvel") {
                frame->pose.root.angularVelocity = vector;
                frame->pose.root.hasAngularVelocity = true;
            } else {
                return Fail(error, lineNumber,
                            "unknown root field '" + field + "'");
            }
            continue;
        }

        if (keyword == "contacts") {
            std::string left;
            std::string right;
            if (!(stream >> left) || !(stream >> right)) {
                return Fail(error, lineNumber,
                            "'contacts' needs a left and a right value");
            }
            ContactState state;
            if (!ParseContact(left, &state.leftFoot)
                || !ParseContact(right, &state.rightFoot)) {
                return Fail(error, lineNumber,
                            "contact values must be unknown, contact, or free");
            }
            frame->pose.contacts = state;
            continue;
        }

        return Fail(error, lineNumber, "unknown keyword '" + keyword + "'");
    }

    if (!sawMagic) {
        return Fail(error, lineNumber,
                    std::string("the trace is empty or has no '") + kMagic
                        + "' line");
    }
    if (frame) {
        result.samples.push_back(frame->Build());
    }
    if (result.samples.empty()) {
        return Fail(error, lineNumber, "the trace carries no frames");
    }

    result.startTime = result.samples.front().timestamp;
    result.endTime = result.samples.back().timestamp;
    if (frameRate) {
        result.nominalFrameRate = *frameRate;
    } else {
        // Not declared: derive it from the recording rather than assume 30 Hz,
        // so a resample of the trace matches what was captured.
        const double span = result.endTime - result.startTime;
        const std::size_t intervals = result.samples.size() - 1;
        result.nominalFrameRate =
            (span > 0.0 && intervals > 0)
            ? static_cast<double>(intervals) / span
            : 30.0;
    }
    for (HumanoidPose& sample : result.samples) {
        sample.source = result.source;
    }

    *animation = std::move(result);
    return true;
}

bool
ReadCaptureTraceFile(const std::string& path, HumanoidAnimation* animation,
                     CaptureTraceError* error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Fail(error, 0, "could not open capture trace '" + path + "'");
    }
    return ReadCaptureTrace(input, animation, error);
}

bool
WriteCaptureTrace(std::ostream& output, const HumanoidAnimation& animation)
{
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(kPrecision);

    output << kMagic << ' ' << CaptureTraceFormatVersion << '\n';
    if (!animation.source.provider.empty()) {
        output << "provider " << animation.source.provider << '\n';
    }
    if (!animation.source.protocol.empty()) {
        output << "protocol " << animation.source.protocol << '\n';
    }
    if (!animation.source.sourceId.empty()) {
        output << "sourceId " << animation.source.sourceId << '\n';
    }
    output << "frameRate " << animation.nominalFrameRate << '\n';

    for (const HumanoidPose& pose : animation.samples) {
        output << '\n' << "t " << pose.timestamp << '\n';

        if (pose.root.hasPosition) {
            const pxr::GfVec3f& p = pose.root.worldPosition;
            output << "root pos " << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
        }
        if (pose.root.hasOrientation) {
            const pxr::GfQuatf& q = pose.root.worldOrientation;
            const pxr::GfVec3f& i = q.GetImaginary();
            output << "root rot " << q.GetReal() << ' ' << i[0] << ' ' << i[1]
                   << ' ' << i[2] << '\n';
        }
        if (pose.root.hasLinearVelocity) {
            const pxr::GfVec3f& v = pose.root.linearVelocity;
            output << "root vel " << v[0] << ' ' << v[1] << ' ' << v[2] << '\n';
        }
        if (pose.root.hasAngularVelocity) {
            const pxr::GfVec3f& a = pose.root.angularVelocity;
            output << "root angvel " << a[0] << ' ' << a[1] << ' ' << a[2]
                   << '\n';
        }
        if (pose.contacts) {
            output << "contacts " << ContactName(pose.contacts->leftFoot) << ' '
                   << ContactName(pose.contacts->rightFoot) << '\n';
        }

        for (std::size_t index = 0; index < HumanBoneCount; ++index) {
            if (!pose.validRotations.test(index)) {
                continue;
            }
            const pxr::GfQuatf& q = pose.localRotations[index];
            const pxr::GfVec3f& i = q.GetImaginary();
            output << "b "
                   << HumanBoneName(static_cast<HumanBone>(index)) << ' '
                   << q.GetReal() << ' ' << i[0] << ' ' << i[1] << ' ' << i[2];
            if (pose.confidence) {
                output << ' ' << (*pose.confidence)[index];
            }
            output << '\n';
        }
    }

    return static_cast<bool>(output);
}

bool
WriteCaptureTraceFile(const std::string& path,
                      const HumanoidAnimation& animation)
{
    // Binary mode with explicit '\n': a trace written on Windows must be byte
    // identical to one written on Linux, or a golden fixture cannot be shared
    // across the three OS cells.
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    return WriteCaptureTrace(output, animation) && output.flush().good();
}

} // namespace motion
