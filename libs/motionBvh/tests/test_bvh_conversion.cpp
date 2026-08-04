// SPDX-License-Identifier: Apache-2.0
//
// The whole recorded path, over a file this repository did not write: bytes ->
// document -> source rig and animation -> a shipped profile -> canonical
// humanoid motion.
//
// This is a *test* holding a reader and a profile at once, which neither library
// may (WORKSPACE.md §2) — and until `motion_bvh_convert` lands it is the only
// caller in C++ that does. That is why it is here rather than in either
// library's own suite: the boundary rule is about `include/` and `src/`, and the
// thing being checked is exactly that the two halves compose.
//
// Every expected number below was measured out of the `.bvh` text and out of the
// profile, not read back out of the conversion. A corpus expectation derived
// from the code under test pins nothing, and the whole reason a real export is
// committed here is that it can be surprising — the generated fixtures are
// shapes this repository wrote and can only confirm what it already believed.
//
// It takes both directories as arguments and is registered only where they
// exist, so a standalone configure of this library still generates.
#include "motionBvh/BvhExtract.h"
#include "motionBvh/BvhParser.h"

#include "motionSource/CanonicalConversion.h"
#include "motionSource/SourceProfileFile.h"

#include "motionCore/Compare.h"
#include "motionCore/Humanoid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using motionBvh::BvhDocument;
using motionBvh::BvhExtractOptions;
using motionSource::SourceAnimation;
using motionSource::SourceConversion;
using motionSource::SourceProfile;
using motionSource::SourceSkeleton;

const motion::MotionTolerance kTolerance;

int failures = 0;

bool
Check(bool condition, const std::string& what)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", what.c_str());
        ++failures;
    }
    return condition;
}

bool
NearVector(const pxr::GfVec3f& actual, const pxr::GfVec3f& expected)
{
    return (actual - expected).GetLength() <= kTolerance.distance;
}

// The one committed real export, and the profile written from it. Both are
// named here rather than discovered, because a test that scanned a directory
// would pass on the day the file it is about stopped being there.
constexpr const char* kRecordedFile = "mocopi-mobile-arm-raise-turn.bvh";
constexpr const char* kProfileFile = "mocopi-mobile-bvh-default-v1.yaml";

// Measured from the `.bvh` text: 27 joints, 162 columns, 853 rows at 50 Hz.
constexpr std::size_t kJointCount = 27;
constexpr std::size_t kFrameCount = 853;
constexpr double kFrameTime = 0.02;
// 22 of the 27 joints carry a canonical bone; the other five are the profile's
// `ignoredJoints`.
constexpr std::size_t kBoundBones = 22;
// The root's rest offset, in the file's centimetres.
constexpr float kHipHeightCm = 95.9893f;

int
Run(const std::filesystem::path& recordedDir,
    const std::filesystem::path& profileDir)
{
    const std::filesystem::path bvhPath = recordedDir / kRecordedFile;
    const std::filesystem::path profilePath = profileDir / kProfileFile;

    BvhDocument document;
    motionBvh::Diagnostic diagnostic;
    if (!Check(motionBvh::ParseBvhFile(bvhPath, &document, &diagnostic),
               "the recorded export did not parse: "
                   + motionBvh::FormatDiagnostic(diagnostic))) {
        return 1;
    }
    Check(document.joints.size() == kJointCount, "joint count moved");
    Check(document.frameCount == kFrameCount, "frame count moved");

    SourceSkeleton skeleton;
    SourceAnimation animation;
    BvhExtractOptions options;
    options.sourceId = kRecordedFile;
    if (!Check(motionBvh::ExtractBvhSource(document, &skeleton, &animation,
                                           &diagnostic, options),
               "extraction refused the recorded export: "
                   + motionBvh::FormatDiagnostic(diagnostic))) {
        return 1;
    }
    Check(skeleton.joints.size() == kJointCount, "extracted joint count moved");
    Check(animation.frameCount == kFrameCount, "extracted frame count moved");
    Check(std::abs(animation.frameTime - kFrameTime) <= kTolerance.time,
          "frame time moved");

    SourceProfile profile;
    motionSource::SourceProfileParseError profileError;
    if (!Check(motionSource::ParseSourceProfileFile(profilePath, &profile,
                                                    &profileError),
               "the shipped profile did not load: " + profileError.reason)) {
        return 1;
    }

    const SourceConversion result =
        motionSource::ConvertSourceToCanonical(skeleton, animation, profile);
    if (!Check(result.Converted(),
               "the profile did not convert its own producer's export: "
                   + std::string(motionSource::ConversionRefusalName(
                         result.refusal))
                   + " " + result.detail)) {
        return 1;
    }

    Check(result.match.bound.size() == kBoundBones, "bound bone count moved");
    Check(result.rest.present.count() == kBoundBones,
          "rest pose covers a different set of bones than the match bound");
    Check(result.animation.samples.size() == kFrameCount,
          "the clip lost or gained frames");

    // --- the rest pose, against the hierarchy's own numbers -----------------
    //
    // `rest-offsets`, so every rest rotation is identity and every rest
    // translation is the sum of the offsets from the nearest bound ancestor
    // down. The hips are the root's own offset, in metres.
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    Check(NearVector(result.rest.localTranslations[hips],
                     pxr::GfVec3f(0.0f, kHipHeightCm / 100.0f, 0.0f)),
          "the hips rest is not the root's offset in metres");
    Check(motion::AngleBetween(result.rest.localRotations[hips],
                               pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)))
              <= kTolerance.angle,
          "a rest-offsets profile produced a non-identity rest rotation");
    // The upper arm is one joint below a bound one, so its rest is that joint's
    // own offset: 13.3291, 3.34342, -3.36319 centimetres.
    const auto leftUpperArm =
        static_cast<std::size_t>(motion::HumanBone::LeftUpperArm);
    Check(NearVector(result.rest.localTranslations[leftUpperArm],
                     pxr::GfVec3f(0.133291f, 0.0334342f, -0.0336319f)),
          "the left upper arm's rest moved");
    // The spine is two joints below the hips, and the segment nothing maps is on
    // the path: 5.22546 + 5.77894 up, -1.18466 + 1.10239 forward.
    const auto spine = static_cast<std::size_t>(motion::HumanBone::Spine);
    Check(NearVector(result.rest.localTranslations[spine],
                     pxr::GfVec3f(0.0f, 0.110044f, -0.0008227f)),
          "the spine's rest is not the sum along its path");

    // --- what the path rule absorbed ---------------------------------------
    //
    // Four bones sit below an unmapped segment: three spine segments and the
    // second of two neck ones. Measured from the hierarchy, not from the
    // conversion.
    const std::vector<motion::HumanBone> expectedComposed = {
        motion::HumanBone::Spine, motion::HumanBone::Chest,
        motion::HumanBone::UpperChest, motion::HumanBone::Head,
    };
    std::vector<motion::HumanBone> composed = result.report.composedBones;
    std::sort(composed.begin(), composed.end());
    std::vector<motion::HumanBone> expected = expectedComposed;
    std::sort(expected.begin(), expected.end());
    Check(composed == expected,
          "a different set of bones absorbed a chain of source joints");

    // --- what the clip could not carry -------------------------------------
    //
    // Every one of the 26 non-root joints declares position channels and every
    // one of them restates its own `OFFSET` in all 853 rows. So the conversion
    // drops 26 joints' worth of translation and loses nothing by it, and this
    // is the assertion that says the two are different events.
    Check(result.report.restatedTranslationJoints.size() == kJointCount - 1,
          "a non-root joint stopped restating its rest geometry");
    Check(result.report.droppedTranslationJoints.empty(),
          "translation that actually varied was dropped");

    // --- root motion --------------------------------------------------------
    //
    // The profile reads the root's samples as absolute positions, and the first
    // one is the root's own offset. Under the other reading this character
    // would stand 0.96 metres off the floor for the whole clip.
    Check(result.animation.samples.front().root.hasPosition,
          "the first sample carries no root position");
    Check(NearVector(result.animation.samples.front().root.worldPosition,
                     pxr::GfVec3f(0.0f, kHipHeightCm / 100.0f, 0.0f)),
          "the first root sample is not the root's own rest position");

    // The session turns about the vertical, and this is the one assertion here
    // that reaches motion rather than geometry. It is tied back to the source
    // numbers rather than to a band chosen by looking at the answer: the root's
    // channels are declared ZXY, so its Y angle is the third of the three, and
    // the two frames where that angle is extreme are the two the canonical clip
    // has to hold furthest apart.
    //
    // The comparison allows ten degrees because the root's other two channels
    // move a few degrees across the clip as well, so the angle between the two
    // rotations is not exactly the Y difference. It is nowhere near loose
    // enough to survive a dropped turn or a doubled one, which are the two
    // failures a mishandled basis actually produces.
    const std::vector<motionSource::SourceEulerAngles>& rootAngles =
        animation.tracks[0].eulerAngles;
    if (Check(rootAngles.size() == kFrameCount, "the root carries no angles")) {
        std::size_t lowest = 0;
        std::size_t highest = 0;
        for (std::size_t index = 0; index < rootAngles.size(); ++index) {
            if (rootAngles[index].third < rootAngles[lowest].third) {
                lowest = index;
            }
            if (rootAngles[index].third > rootAngles[highest].third) {
                highest = index;
            }
        }
        const double span = static_cast<double>(rootAngles[highest].third)
                            - static_cast<double>(rootAngles[lowest].third);
        const float carried = motion::AngleBetween(
            result.animation.samples[lowest].localRotations[hips],
            result.animation.samples[highest].localRotations[hips]);
        const double expectedRadians = span * 3.14159265358979323846 / 180.0;
        Check(span > 90.0, "the session no longer turns");
        Check(std::abs(carried - expectedRadians) < 10.0 * 3.14159265358979323846
                                                        / 180.0,
              "the turn the hips carry is not the turn the root states");
    }

    // --- provenance ---------------------------------------------------------
    Check(result.provenance.profileId == profile.id,
          "the conversion did not record which profile it was read under");
    Check(result.provenance.producer == profile.producer,
          "the conversion did not take the producer from the profile");
    Check(result.provenance.format == motionBvh::BvhFormatLabel(),
          "the reader's format label did not survive the conversion");
    Check(result.animation.source.kind == motion::MotionSourceKind::Clip,
          "a recorded file did not become a clip");

    // --- determinism --------------------------------------------------------
    const SourceConversion again =
        motionSource::ConvertSourceToCanonical(skeleton, animation, profile);
    Check(again.animation == result.animation,
          "two conversions of one input differ");

    if (failures > 0) {
        std::fprintf(stderr, "%d recorded-conversion check(s) failed\n",
                     failures);
        return 1;
    }
    std::printf("motionBvh conversion: %zu frames over %zu bones verified\n",
                result.animation.samples.size(), result.rest.present.count());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <recorded-corpus-dir> <profiles-dir>\n",
                     argv[0]);
        return 2;
    }
    return Run(argv[1], argv[2]);
}
