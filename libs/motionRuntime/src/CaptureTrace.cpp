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

// The writer emits unit quaternions at six decimals, so a round trip perturbs
// the squared length by at most ~4e-6. Anything past this never came from the
// writer, and passing it downstream would silently skew or scale the joint that
// UsdSkel builds from it.
constexpr float kQuaternionLengthTolerance = 1e-3f;

bool
Fail(CaptureTraceError* error, std::size_t line, std::string message)
{
    if (error) {
        error->line = line;
        error->message = std::move(message);
    }
    return false;
}

// A line is either fully consumed or malformed. Ignoring the tail would let
// `b hips 1 0 0 0 grbage` read as a perfectly good frame -- the same class of
// mistake as an unknown bone name, which this parser already refuses outright
// rather than treating as a missing limb (CaptureTrace.h).
bool
FullyConsumed(std::istringstream& stream, CaptureTraceError* error,
              std::size_t line, const std::string& what)
{
    std::string extra;
    if (stream >> extra) {
        return Fail(error, line, "unexpected '" + extra + "' after " + what);
    }
    return true;
}

// Shared by `b` and `root rot`: a rotation has to be one, or nothing
// downstream of the parse means what it says.
bool
CheckUnitQuaternion(const float* components, CaptureTraceError* error,
                    std::size_t line, const std::string& what)
{
    const float lengthSquared = components[0] * components[0]
        + components[1] * components[1] + components[2] * components[2]
        + components[3] * components[3];
    if (lengthSquared <= 0.0f) {
        return Fail(error, line, what + " has a zero-length rotation");
    }
    if (std::fabs(lengthSquared - 1.0f) > kQuaternionLengthTolerance) {
        return Fail(error, line,
                    what + " rotation is not unit length (|q|^2 = "
                        + std::to_string(lengthSquared) + ")");
    }
    return true;
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

// A name is written as one token, so one that is empty or carries whitespace
// cannot be read back as itself. The alternative is a quoting rule, which needs
// an escaping rule behind it, in a format whose whole value is that a fixture
// diffs and round-trips.
bool
IsWritableExpressionName(const std::string& name) noexcept
{
    return !name.empty()
        && name.find_first_of(" \t\r\n\v\f") == std::string::npos;
}

// Everything left on a header line, with the whitespace either side removed.
// The interior is kept exactly, so "Example Avatar" survives and "Example
// Avatar " does not come back with its trailing space -- which is why the
// writer refuses the latter rather than letting it change on the way through.
std::string
TakeRestOfLine(std::istringstream& stream)
{
    std::string rest;
    std::getline(stream, rest);
    const std::size_t first = rest.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return std::string();
    }
    const std::size_t last = rest.find_last_not_of(" \t");
    return rest.substr(first, last - first + 1);
}

// A provenance value the header can carry and hand back unchanged. A line break
// would end the line early and produce a different file; whitespace at either
// end would be trimmed on the way back in, which is a silent edit to somebody's
// recorded provenance. Interior spaces are fine and are the reason this exists.
bool
IsWritableProvenanceValue(const std::string& value) noexcept
{
    if (value.find_first_of("\r\n\v\f") != std::string::npos) {
        return false;
    }
    if (value.empty()) {
        // Not written at all, so nothing can go wrong with it.
        return true;
    }
    const auto edge = [](char c) { return c == ' ' || c == '\t'; };
    return !edge(value.front()) && !edge(value.back());
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
    int formatVersion = 0;
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
            if (!(stream >> formatVersion)) {
                return Fail(error, lineNumber,
                            "the trace magic carries no format version");
            }
            if (formatVersion < CaptureTraceMinReadableVersion
                || formatVersion > CaptureTraceFormatVersion) {
                return Fail(error, lineNumber,
                            "unsupported capture trace format version "
                                + std::to_string(formatVersion));
            }
            if (!FullyConsumed(stream, error, lineNumber,
                               "the trace magic's format version")) {
                return false;
            }
            sawMagic = true;
            continue;
        }

        if (keyword == "t") {
            double timestamp = 0.0;
            if (!(stream >> timestamp) || !std::isfinite(timestamp)) {
                return Fail(error, lineNumber, "'t' needs a finite timestamp");
            }
            if (!FullyConsumed(stream, error, lineNumber, "the 't' timestamp")) {
                return false;
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
                // The rest of the line, not the next token. These three are
                // free text a producer supplies -- a VMC sender's `sourceId` is
                // the model title it chose to broadcast -- so "one token" was a
                // rule about nothing: the writer emitted "Example Avatar"
                // verbatim and this reader then refused the file it had just
                // written. A frame's keys are still tokens; only the header's
                // free-text values are not.
                value = TakeRestOfLine(stream);
                if (value.empty()) {
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
                if (!FullyConsumed(stream, error, lineNumber,
                                   "the 'frameRate' value")) {
                    return false;
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
            if (!CheckUnitQuaternion(quaternion, error, lineNumber,
                                     "'b " + boneName + "'")) {
                return false;
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
            } else if (!stream.eof()) {
                // The extraction failed on something that is not end of line,
                // so the trailing text is not a confidence at all.
                return Fail(error, lineNumber,
                            "'b " + boneName
                                + "' takes a w x y z rotation and an optional "
                                  "confidence in [0, 1]");
            } else {
                frame->confidence[index] = 1.0f;
            }
            if (!FullyConsumed(stream, error, lineNumber,
                               "the 'b " + boneName + "' confidence")) {
                return false;
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
                if (!CheckUnitQuaternion(values, error, lineNumber, "'root rot'")
                    || !FullyConsumed(stream, error, lineNumber,
                                      "the 'root rot' rotation")) {
                    return false;
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
            if (!FullyConsumed(stream, error, lineNumber,
                               "the 'root " + field + "' vector")) {
                return false;
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
            if (!FullyConsumed(stream, error, lineNumber,
                               "the 'contacts' pair")) {
                return false;
            }
            frame->pose.contacts = state;
            continue;
        }

        if (keyword == "e") {
            if (formatVersion < CaptureTraceExpressionsVersion) {
                return Fail(error, lineNumber,
                            "'e' expression weights need format version "
                                + std::to_string(CaptureTraceExpressionsVersion)
                                + "; this trace declares "
                                + std::to_string(formatVersion));
            }
            std::string name;
            if (!(stream >> name)) {
                return Fail(error, lineNumber,
                            "'e' needs an expression name and a weight");
            }
            float weight = 0.0f;
            if (!ReadFloats(stream, &weight, 1)) {
                return Fail(error, lineNumber,
                            "'e " + name + "' needs a finite weight");
            }
            if (!FullyConsumed(stream, error, lineNumber,
                               "the 'e " + name + "' weight")) {
                return false;
            }
            // The name is the producer's and this layer knows no vocabulary to
            // check it against, so a repeat is the only thing that can be wrong
            // with it here -- and it is the same defect a repeated `b` is: two
            // values for one channel in one frame, with no rule saying which
            // wins.
            if (!frame->pose.expressions.Set(name, weight)) {
                return Fail(error, lineNumber,
                            "expression '" + name
                                + "' appears twice in a frame");
            }
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
    // Checked before a byte is emitted rather than as each frame is reached: a
    // caller that gets `false` back has an untouched stream, instead of a file
    // that is a valid trace of the frames that happened to come first.
    //
    // The provenance strings are checked for the same reason the expression
    // names are, and they were not until a real producer supplied one this
    // format could not spell. They are the only fields here whose content
    // arrives from outside this repository -- a sender's model title is
    // whatever a person typed into an application -- so this is where a file
    // that would read back as something else gets refused instead of written.
    if (!IsWritableProvenanceValue(animation.source.provider)
        || !IsWritableProvenanceValue(animation.source.protocol)
        || !IsWritableProvenanceValue(animation.source.sourceId)) {
        return false;
    }
    for (const HumanoidPose& pose : animation.samples) {
        for (const ExpressionWeight& entry : pose.expressions.entries) {
            if (!IsWritableExpressionName(entry.name)) {
                return false;
            }
        }
    }

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

        // Already in name order: that is the order `ExpressionWeights` keeps,
        // and sorting here instead would let a set that lost the invariant
        // still write a well-formed file.
        for (const ExpressionWeight& entry : pose.expressions.entries) {
            output << "e " << entry.name << ' ' << entry.weight << '\n';
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
