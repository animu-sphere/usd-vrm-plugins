// SPDX-License-Identifier: Apache-2.0
//
// motion_bvh_convert — a recorded file, read the way its producer meant it.
//
// This is the composition point, not the algorithm. `motionBvh` turns bytes
// into a document and a document into source values, `motionSource` matches a
// profile and changes the basis, `ClipWriter` owns everything that touches a
// stage, and this file wires them together and reports what happened.
//
// **It is the first program that holds a reader and a profile at once**, which
// neither library may (WORKSPACE.md §2) — `motionBvh` is forbidden to know a
// producer exists and `motionSource` to know a reader does. Two consequences
// land here and nowhere else:
//
// *The six semantic diagnostics are raised here.* The frozen set lives in the
// reader and names the format, but its semantic half is about a document
// meeting a profile, so `MatchSourceProfile` returns a typed
// `SourceProfileRefusal` naming the **event** and this file maps it onto the
// code (roadmap/recorded-motion-sources.md §10). The mapping is deliberately
// not one-to-one: an ambiguous joint name has no code of its own and is a
// profile mismatch, which is exactly why the refusal names the event instead of
// the code.
//
// *There is no default profile and no fallback.* A missing `--profile` is
// `VRM_BVH_PROFILE_REQUIRED` and stops the run. A BVH file states no producer,
// so the alternative is concluding one from joint names — and a near-miss
// profile produces motion that is subtly misassembled rather than absent, which
// is worse than a refusal because it looks like a result (§3.1).
//
// Exit status is three-valued and the split is *whose input was wrong*: 0 a
// clip was written, 1 the recorded file was refused, 2 the command or something
// it named was wrong. A profile that will not load is a 2 even though it is
// nobody's typo — the `.bvh` is fine, and sending whoever ran this to look at
// their capture would be the wrong place.
#include "ClipWriter.h"
#include "ConvertOptions.h"
#include "ProfileLocator.h"

#include "motionBvh/BvhDocument.h"
#include "motionBvh/BvhExtract.h"
#include "motionBvh/BvhParser.h"
#include "motionBvh/Diagnostics.h"

#include "motionSource/CanonicalConversion.h"
#include "motionSource/SourceAnimation.h"
#include "motionSource/SourceProfile.h"
#include "motionSource/SourceProfileFile.h"
#include "motionSource/SourceSkeleton.h"

#include "motionCore/Humanoid.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{

using motionBvh::DiagnosticCode;

void
PrintDiagnostic(const motionBvh::Diagnostic& diagnostic)
{
    std::cerr << "motion_bvh_convert: " << motionBvh::FormatDiagnostic(diagnostic)
              << "\n";
}

motionBvh::Diagnostic
Raise(DiagnosticCode code, const std::string& source, std::string detail,
      std::string subject = {})
{
    motionBvh::Diagnostic diagnostic =
        motionBvh::MakeDiagnostic(code, std::move(detail));
    diagnostic.source = source;
    diagnostic.subject = std::move(subject);
    return diagnostic;
}

// A profile's typed refusal onto the reader's frozen code.
//
// Three of the seven collapse onto `VRM_BVH_PROFILE_MISMATCH` and that is the
// mapping working rather than information lost: a rig that roots elsewhere, one
// whose hierarchy disagrees, and one that repeats a mapped name are all "this
// profile does not describe this file", and the refusal's own `detail` carries
// which. The two that do not collapse have codes of their own because a caller
// acts differently on them — a missing required joint is a file from a
// different export, an unmapped joint is a policy question the profile answers.
DiagnosticCode
CodeForProfileRefusal(motionSource::SourceProfileRefusal refusal)
{
    switch (refusal) {
    case motionSource::SourceProfileRefusal::RequiredJointMissing:
        return DiagnosticCode::RequiredJointMissing;
    case motionSource::SourceProfileRefusal::UnmappedJointRefused:
        return DiagnosticCode::UnmappedJoint;
    case motionSource::SourceProfileRefusal::SkeletonInvalid:
        // The rig came out of the document, so a rig that is not a rig is a
        // fact about the file. The set is closed and names no code for it,
        // which is what `VRM_BVH_PARSE_FAILED` with a precise detail is for
        // (Diagnostics.h).
        return DiagnosticCode::ParseFailed;
    case motionSource::SourceProfileRefusal::ProfileInvalid:
    case motionSource::SourceProfileRefusal::RootJointMismatch:
    case motionSource::SourceProfileRefusal::AmbiguousJointName:
    case motionSource::SourceProfileRefusal::HierarchyMismatch:
    case motionSource::SourceProfileRefusal::None:
    case motionSource::SourceProfileRefusal::Count:
        break;
    }
    return DiagnosticCode::ProfileMismatch;
}

std::string
Number(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

// The bones a conversion composed a chain into, in `HumanBone` order, as the
// report prints them and as the clip records them.
std::string
BoneList(const std::vector<motion::HumanBone>& bones)
{
    std::string text;
    for (const motion::HumanBone bone : bones) {
        if (!text.empty()) {
            text += ", ";
        }
        text += std::string(motion::HumanBoneName(bone));
    }
    return text;
}

void
PrintReport(const motionSource::SourceConversion& conversion,
            const motionSource::SourceProfile& profile,
            const motionBvh::BvhDocument& document, const std::string& sourceId,
            const std::string& outputPath)
{
    const std::size_t bound = conversion.match.bound.size();
    std::printf("source:   %s\n", sourceId.c_str());
    std::printf("profile:  %s (%s)\n", profile.id.c_str(),
                profile.producer.c_str());
    std::printf("joints:   %zu read, %zu bound, %zu ignored\n",
                document.joints.size(), bound, profile.ignoredJoints.size());
    const double rate = conversion.animation.nominalFrameRate;
    std::printf("frames:   %zu at %s Hz (%s s)\n",
                conversion.animation.samples.size(), Number(rate).c_str(),
                Number(conversion.animation.endTime
                       - conversion.animation.startTime)
                    .c_str());

    // Both halves of what the conversion could not carry, never one word for
    // the two: a rig restating its rest geometry every frame lost nothing,
    // and a rig whose elbow actually translates lost motion
    // (CanonicalConversion.h).
    const motionSource::ConversionReport& report = conversion.report;
    std::printf("dropped:  %zu joint(s) whose translation varied\n",
                report.droppedTranslationJoints.size());
    std::printf("restated: %zu joint(s) restating rest geometry\n",
                report.restatedTranslationJoints.size());
    if (!report.composedBones.empty()) {
        std::printf("composed: %s\n", BoneList(report.composedBones).c_str());
    }
    std::printf("output:   %s\n", outputPath.c_str());
}

} // namespace

int
main(int argc, char** argv)
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    motionBvhTool::ConvertOptions options;
    bool showHelp = false;
    std::string error;
    if (!motionBvhTool::ParseConvertOptions(arguments, &options, &showHelp,
                                            &error)) {
        std::cerr << "motion_bvh_convert: " << error << "\n\n"
                  << motionBvhTool::GetConvertUsage();
        return 2;
    }
    if (showHelp) {
        std::fputs(motionBvhTool::GetConvertUsage(), stdout);
        return 0;
    }

    // --- the profile, before the file --------------------------------------
    //
    // Deliberately first. A conversion with no profile refuses whatever the
    // file turns out to be, and parsing a 60 MB recording to then say "name a
    // profile" would be work done to reach an answer that was already known.
    if (options.profile.empty()) {
        PrintDiagnostic(Raise(DiagnosticCode::ProfileRequired,
                              options.inputPath,
                              "no profile was named. A BVH file states no "
                              "producer, and there is no default profile and "
                              "no automatic fallback; pass --profile <id>"));
        return 2;
    }

    std::filesystem::path profilePath;
    if (!motionBvhTool::ResolveProfilePath(options.profile,
                                           options.profileDirs, &profilePath,
                                           &error)) {
        std::cerr << "motion_bvh_convert: " << error << "\n";
        return 2;
    }

    motionSource::SourceProfile profile;
    motionSource::SourceProfileParseError profileError;
    if (!motionSource::ParseSourceProfileFile(profilePath, &profile,
                                              &profileError)) {
        // A malformed profile file is not an event in the reader's diagnostic
        // set (SourceProfileFile.h), and it has exactly one candidate there: a
        // profile nobody could read is a conversion with no profile, which is
        // the state `VRM_BVH_PROFILE_REQUIRED` names.
        std::string detail = profilePath.string() + ": " + profileError.reason;
        if (profileError.line != 0) {
            detail += " (line " + std::to_string(profileError.line) + ")";
        }
        PrintDiagnostic(Raise(DiagnosticCode::ProfileRequired,
                              options.inputPath, std::move(detail)));
        return 2;
    }

    // The id a file states must be the id that was asked for. See
    // ProfileLocator.h: a renamed file would otherwise let this conversion
    // record a profile id it never read.
    if (!motionBvhTool::ProfileRequestIsPath(options.profile)
        && profile.id != options.profile) {
        std::cerr << "motion_bvh_convert: " << profilePath.string()
                  << " states id '" << profile.id << "', not '"
                  << options.profile << "' as asked for\n";
        return 2;
    }

    // Unreachable through the loader, which ends in `ValidateSourceProfile` and
    // refuses an unspecified convention. Checked on the typed values rather
    // than by reading that validator's prose, because picking a code out of a
    // sentence is the thing roadmap §10 rejected -- and because this is the one
    // raiser `VRM_BVH_INVALID_ROOT_POLICY` has.
    if (profile.rootTranslation == motionSource::RootTranslationPolicy::Unspecified
        || profile.rootRotation == motionSource::RootRotationPolicy::Unspecified) {
        PrintDiagnostic(Raise(DiagnosticCode::InvalidRootPolicy,
                              options.inputPath,
                              profilePath.string()
                                  + " states no root translation or rotation "
                                    "policy",
                              profile.id));
        return 2;
    }

    // --- the file ----------------------------------------------------------
    motionBvh::BvhParseOptions parseOptions;
    parseOptions.limits = options.limits;
    // Left empty on purpose: `ParseBvhFile` fills it with the path it opened,
    // so a diagnostic names the file the parser actually read.

    motionBvh::BvhDocument document;
    motionBvh::Diagnostic diagnostic;
    if (!motionBvh::ParseBvhFile(options.inputPath, &document, &diagnostic,
                                 parseOptions)) {
        PrintDiagnostic(diagnostic);
        return 1;
    }

    // The identity this conversion records is the file's name, not the path it
    // was read from. A clip is a deliverable that gets compared, and an
    // absolute path would make the same conversion of the same bytes differ
    // between two machines in its provenance and nowhere else.
    const std::string sourceId =
        std::filesystem::path(options.inputPath).filename().string();

    motionSource::SourceSkeleton skeleton;
    motionSource::SourceAnimation animation;
    motionBvh::BvhExtractOptions extractOptions;
    extractOptions.sourceId = sourceId;
    if (!motionBvh::ExtractBvhSource(document, &skeleton, &animation,
                                     &diagnostic, extractOptions)) {
        PrintDiagnostic(diagnostic);
        return 1;
    }

    // --- the crossing ------------------------------------------------------
    const motionSource::SourceConversion conversion =
        motionSource::ConvertSourceToCanonical(skeleton, animation, profile);
    if (!conversion.Converted()) {
        DiagnosticCode code = DiagnosticCode::ParseFailed;
        std::string subject;
        switch (conversion.refusal) {
        case motionSource::ConversionRefusal::ProfileMismatch:
            code = CodeForProfileRefusal(conversion.match.refusal);
            subject = profile.id;
            break;
        case motionSource::ConversionRefusal::AnimationInvalid:
            // The animation came out of the document, so this is a fact about
            // the file and takes the closed set's catch-all.
            code = DiagnosticCode::ParseFailed;
            break;
        case motionSource::ConversionRefusal::UnsupportedRotationForm:
            // Unreachable from this reader -- `ExtractBvhSource` writes angles
            // with an order and never quaternions -- and handled anyway, so
            // that the day a second reader lands behind this same CLI it
            // reports rather than falls through to a wrong code.
            code = DiagnosticCode::ParseFailed;
            break;
        case motionSource::ConversionRefusal::None:
        case motionSource::ConversionRefusal::Count:
            break;
        }
        PrintDiagnostic(Raise(code, options.inputPath, conversion.detail,
                              std::move(subject)));
        return 1;
    }

    // Recoverable, one line per joint, and only under the policy that asks for
    // it: `Ignore` is silent and `Refuse` already stopped the conversion above
    // (SourceProfile.h). These go to stderr while the report goes to stdout,
    // so a run that is piped somewhere keeps the two apart.
    if (profile.unmappedJoints == motionSource::UnmappedJointPolicy::Report) {
        for (const std::size_t index : conversion.match.unmappedJoints) {
            PrintDiagnostic(Raise(DiagnosticCode::UnmappedJoint,
                                  options.inputPath,
                                  "the profile maps and ignores neither",
                                  skeleton.joints[index].name));
        }
    }

    // --- the clip ----------------------------------------------------------
    std::map<std::string, std::string> provenance;
    provenance["kind"] = "recordedClip";
    provenance["producer"] = conversion.provenance.producer;
    if (!conversion.provenance.producerVersion.empty()) {
        provenance["producerVersion"] = conversion.provenance.producerVersion;
    }
    provenance["profileId"] = conversion.provenance.profileId;
    provenance["format"] = conversion.provenance.format;
    provenance["sourceId"] = conversion.provenance.sourceId;
    provenance["frames"] =
        std::to_string(conversion.animation.samples.size());
    provenance["frameRate"] = Number(conversion.animation.nominalFrameRate);
    provenance["boundBones"] = std::to_string(conversion.match.bound.size());
    // Which bones absorbed a chain of unmapped joints. Recorded rather than
    // only printed, because it is what a cross-source comparison against the
    // same session over a live protocol needs in order to tell a composition
    // residual apart from a real disagreement (CanonicalConversion.h).
    provenance["composedBones"] = BoneList(conversion.report.composedBones);
    provenance["droppedTranslationJoints"] =
        std::to_string(conversion.report.droppedTranslationJoints.size());

    if (!motionBvhTool::WriteSemanticClip(options.outputPath,
                                          conversion.animation, conversion.rest,
                                          options.clipName, provenance,
                                          &error)) {
        std::cerr << "motion_bvh_convert: " << error << "\n";
        return 1;
    }

    if (!options.quiet) {
        PrintReport(conversion, profile, document, sourceId,
                    options.outputPath);
    }
    return 0;
}
