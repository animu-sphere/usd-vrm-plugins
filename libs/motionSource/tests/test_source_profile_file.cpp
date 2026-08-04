// SPDX-License-Identifier: Apache-2.0
//
// A profile as a file: the keys, what a file that gets one wrong is told, and
// the line it is told about.
//
// Two things are checked that a parser test usually is not. Every refusal is
// asserted **with its line**, because a reader who is told a profile is wrong
// and not where has to bisect the file by hand -- and the keys are nested, so an
// off-by-one is easy to write and invisible without the assertion. And a refused
// file must leave the caller's profile untouched: a half-built profile is the
// one outcome worse than a refusal, since it is a profile nobody wrote.
//
// No producer appears here, in the fixtures or the names. The profile below is
// a plausible shape for an ordinary rig -- a test written against a real
// product's export would be the first place this layer learned one, and the file
// this reader was written for is a *data* file, in `profiles/motion/`, where a
// product name is allowed precisely because no code has a name for it.
#include "motionSource/SourceProfileFile.h"

#include "motionSource/SourceProfile.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using motionSource::MatchSourceProfile;
using motionSource::ParseSourceProfileFile;
using motionSource::ParseSourceProfileText;
using motionSource::RestPoseSource;
using motionSource::RootRotationPolicy;
using motionSource::RootTranslationPolicy;
using motionSource::SourceAxis;
using motionSource::SourceHandedness;
using motionSource::SourceJoint;
using motionSource::SourceJointMapping;
using motionSource::SourceLengthUnit;
using motionSource::SourceProfile;
using motionSource::SourceProfileParseError;
using motionSource::SourceProfileRefusal;
using motionSource::SourceSkeleton;
using motionSource::UnmappedJointPolicy;
using motionSource::ValidateSourceProfile;

using Bone = motion::HumanBone;

// The whole vocabulary in one file, in the shape `SourceProfileFile.h` states:
// a block mapping, two nested ones, a joint map in flow form and a flow
// sequence.
constexpr std::string_view kProfileText = R"(# A profile, and a comment above it.
schemaVersion: 1
id: example-recorder-bvh-neutral-v1
producer: Example Recorder

coordinates:
  handedness: right
  upAxis: +Y
  forwardAxis: +Z
  translationUnit: centimeters

root:
  joint: reference
  translation: absolute-position
  rotation: body-orientation

restPose: rest-offsets
unmappedJoints: report

joints:
  hip:      { bone: hips, required: true }
  spine:    { bone: spine, required: true }
  chest:    { bone: chest }
  neck:     { bone: neck, required: false }
  head:     { bone: head, required: true }

ignoredJoints: [reference, propHandle]
)";

// The whole joint map, for the two tests that have to replace it rather than a
// line of it: a `joints:` key with a value on its line leaves its former
// children indented under nothing, and the refusal that produces is about the
// indentation rather than about the key.
constexpr std::string_view kJointsBlock =
    "joints:\n"
    "  hip:      { bone: hips, required: true }\n"
    "  spine:    { bone: spine, required: true }\n"
    "  chest:    { bone: chest }\n"
    "  neck:     { bone: neck, required: false }\n"
    "  head:     { bone: head, required: true }";

constexpr std::string_view kCoordinatesBlock =
    "coordinates:\n"
    "  handedness: right\n"
    "  upAxis: +Y\n"
    "  forwardAxis: +Z\n"
    "  translationUnit: centimeters";

SourceJointMapping
Map(std::string sourceName, Bone bone, bool required)
{
    SourceJointMapping mapping;
    mapping.sourceName = std::move(sourceName);
    mapping.bone = bone;
    mapping.required = required;
    return mapping;
}

// What `kProfileText` says, written the way a caller would have written it by
// hand. Compared with `operator==`, so every field the file states is pinned --
// including the two `required` flags it leaves to the default.
SourceProfile
ExpectedProfile()
{
    SourceProfile profile;
    profile.id = "example-recorder-bvh-neutral-v1";
    profile.producer = "Example Recorder";
    profile.rootJoint = "reference";
    profile.handedness = SourceHandedness::Right;
    profile.upAxis = SourceAxis::PlusY;
    profile.forwardAxis = SourceAxis::PlusZ;
    profile.translationUnit = SourceLengthUnit::Centimeters;
    profile.rootTranslation = RootTranslationPolicy::AbsolutePosition;
    profile.rootRotation = RootRotationPolicy::BodyOrientation;
    profile.restPose = RestPoseSource::RestOffsets;
    profile.unmappedJoints = UnmappedJointPolicy::Report;
    profile.joints = {
        Map("hip", Bone::Hips, true),
        Map("spine", Bone::Spine, true),
        Map("chest", Bone::Chest, false),
        Map("neck", Bone::Neck, false),
        Map("head", Bone::Head, true),
    };
    profile.ignoredJoints = {"reference", "propHandle"};
    return profile;
}

SourceProfile
Parse(std::string_view text)
{
    SourceProfile profile;
    SourceProfileParseError error;
    if (!ParseSourceProfileText(text, &profile, &error)) {
        std::printf("unexpected refusal at line %zu: %s\n", error.line,
                    error.reason.c_str());
        assert(false);
    }
    return profile;
}

// Every refusal is asserted with its line and with a fragment of its reason. The
// fragment is deliberately a phrase rather than the whole sentence: a test that
// pinned the wording would fail on a clearer message, and a test that pinned
// nothing would pass on the wrong refusal.
void
Refuses(std::string_view text, std::size_t line, std::string_view fragment)
{
    // Not default-constructed: a refused parse must leave what the caller
    // already had, and only a profile with something in it can show that.
    SourceProfile profile = ExpectedProfile();
    SourceProfileParseError error;
    const bool parsed = ParseSourceProfileText(text, &profile, &error);
    if (parsed) {
        std::printf("expected a refusal naming '%s'\n",
                    std::string(fragment).c_str());
        assert(false);
    }
    if (error.line != line
        || error.reason.find(std::string(fragment)) == std::string::npos) {
        std::printf("line %zu: %s (wanted line %zu naming '%s')\n", error.line,
                    error.reason.c_str(), line, std::string(fragment).c_str());
        assert(false);
    }
    assert(profile == ExpectedProfile());
}

// Replaces the first occurrence of `from` in the valid file. Every refusal below
// is therefore one edit away from a file that parses, which is what makes it a
// test of that key rather than of some other mistake made while retyping the
// document.
std::string
With(std::string_view from, std::string_view to)
{
    std::string text(kProfileText);
    const std::size_t at = text.find(from);
    assert(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

void
TestTheKeys()
{
    const SourceProfile profile = Parse(kProfileText);
    assert(profile == ExpectedProfile());
    assert(ValidateSourceProfile(profile));

    // The joint map's order is the file's, because a match reports in it.
    assert(profile.joints.size() == 5);
    assert(profile.joints.front().sourceName == "hip");
    assert(profile.joints.back().sourceName == "head");
    // Stated `required: false` and an omitted `required` are the same answer,
    // and neither is the same as the flag being lost.
    assert(profile.joints[2].required == false);
    assert(profile.joints[3].required == false);
    assert(profile.joints[4].required == true);
}

// The same document written the other way round: joint entries as nested
// mappings, the ignore list as a block sequence, keys quoted, CRLF endings and a
// byte-order mark. All four are shapes a text editor produces without being
// asked, and a reader that refused any of them would be refusing a file with
// nothing wrong in it.
void
TestTheOtherShapes()
{
    const std::string text =
        "\xEF\xBB\xBF"
        "schemaVersion: 1\r\n"
        "id: example-recorder-bvh-neutral-v1\r\n"
        "producer: Example Recorder\r\n"
        "coordinates:\r\n"
        "  handedness: right\r\n"
        "  upAxis: Y\r\n"          // the unsigned spelling is the positive one
        "  forwardAxis: +Z\r\n"
        "  translationUnit: centimetres\r\n" // the other spelling of one unit
        "root:\r\n"
        "  joint: reference\r\n"
        "  translation: absolute-position\r\n"
        "  rotation: body-orientation\r\n"
        "restPose: rest-offsets\r\n"
        "unmappedJoints: report\r\n"
        "joints:\r\n"
        "  hip:\r\n"
        "    bone: hips\r\n"
        "    required: TRUE\r\n"
        "  spine:\r\n"
        "    bone: spine\r\n"
        "    required: true\r\n"
        "  chest:\r\n"
        "    bone: chest\r\n"
        "  neck:\r\n"
        "    bone: neck\r\n"
        "  \"head\":\r\n"
        "    bone: head\r\n"
        "    required: true\r\n"
        "ignoredJoints:\r\n"
        "  - reference\r\n"
        "  - propHandle\r\n";
    assert(Parse(text) == ExpectedProfile());
}

// A value carrying a `#`, a comma or a colon is written quoted, and a joint name
// is the writer's word rather than a set of characters this reader reserves.
void
TestQuotingAndComments()
{
    const std::string text =
        With("producer: Example Recorder",
             "producer: \"Example Recorder #2, \\\"studio\\\" build\"");
    const SourceProfile profile = Parse(text);
    assert(profile.producer == "Example Recorder #2, \"studio\" build");

    // Not a comment: a `#` that opens no word belongs to the value.
    assert(Parse(With("id: example-recorder-bvh-neutral-v1",
                      "id: example#2-bvh-neutral-v1"))
               .id
           == "example#2-bvh-neutral-v1");

    // An unquoted name may carry a colon: only a colon that ends a word ends a
    // key.
    const SourceProfile colons =
        Parse(With("  chest:    { bone: chest }", "  rig:chest: { bone: chest }"));
    assert(colons.joints[2].sourceName == "rig:chest");
    // ... and a quoted one may carry anything.
    const SourceProfile quoted =
        Parse(With("  chest:    { bone: chest }",
                   "  \"chest [1]\": { bone: chest }"));
    assert(quoted.joints[2].sourceName == "chest [1]");
}

// The keys are closed. A misspelling is the specific failure this reader exists
// to refuse: a `requred:` a permissive reader dropped would bind an arm the
// profile called mandatory and report nothing about it.
void
TestUnknownKeysAreRefused()
{
    Refuses(With("restPose: rest-offsets", "restpose: rest-offsets"), 17,
            "'restpose'");
    Refuses(With("  handedness: right", "  handednes: right"), 7,
            "'handednes'");
    Refuses(With("  joint: reference", "  jointName: reference"), 13,
            "'jointName'");
    Refuses(With("{ bone: hips, required: true }", "{ bone: hips, requred: true }"),
            21, "'requred'");
    Refuses(With("unmappedJoints: report",
                 "unmappedJoints: report\nunmappedJointPolicy: report"),
            19, "'unmappedJointPolicy'");
}

void
TestMissingKeys()
{
    // Line 0: a key the document does not state is about the document. Pointing
    // at a line would send a reader to one with nothing wrong on it.
    Refuses(With("producer: Example Recorder\n", ""), 0, "states no 'producer'");
    Refuses(With("unmappedJoints: report\n", ""), 0,
            "states no 'unmappedJoints'");
    // A key missing from a nested mapping is about the key that opened it.
    Refuses(With("  forwardAxis: +Z\n", ""), 6, "states no 'forwardAxis'");
    Refuses(With("  rotation: body-orientation\n", ""), 12,
            "states no 'rotation'");
    Refuses(With("{ bone: chest }", "{ required: true }"), 23, "states no 'bone'");
}

void
TestDuplicateKeys()
{
    Refuses(With("restPose: rest-offsets",
                 "restPose: rest-offsets\nrestPose: first-frame"),
            18, "stated twice");
    Refuses(With("  spine:    { bone: spine, required: true }",
                 "  spine:    { bone: spine, required: true }\n"
                 "  spine:    { bone: chest }"),
            23, "stated twice");
    Refuses(With("{ bone: hips, required: true }",
                 "{ bone: hips, bone: spine }"),
            21, "stated twice");
}

// Every word on a right-hand side is one `SourceProfile.h` defines, and the
// refusal lists the ones that exist -- from the vocabulary itself, so a word
// added there cannot go missing from this message.
void
TestUnknownVocabulary()
{
    Refuses(With("  handedness: right", "  handedness: clockwise"), 7,
            "'clockwise' is not one of right, left");
    Refuses(With("  upAxis: +Y", "  upAxis: +W"), 8, "+X, -X, +Y, -Y, +Z, -Z");
    Refuses(With("  translationUnit: centimeters", "  translationUnit: furlongs"),
            10, "meters, centimeters, millimeters, inches");
    Refuses(With("  translation: absolute-position", "  translation: root-motion"),
            14, "absolute-position, rest-relative, none");
    Refuses(With("restPose: rest-offsets", "restPose: t-pose"), 17,
            "rest-offsets, stated-rest-rotations, first-frame");
    Refuses(With("unmappedJoints: report", "unmappedJoints: warn"), 18,
            "ignore, report, refuse");

    // `unspecified` is a word the vocabulary has and no profile may state: it is
    // what a profile nobody finished carries, so a file writing it is refused
    // where it was written rather than as a missing convention three keys later.
    Refuses(With("  handedness: right", "  handedness: unspecified"), 7,
            "is not one of right, left");

    // The one vocabulary not listed back: fifty-five bone names would be a wall
    // of text in a refusal.
    Refuses(With("{ bone: chest }", "{ bone: torso }"), 23,
            "'torso' is not a canonical humanoid bone");
}

void
TestValueShapes()
{
    Refuses(With("required: true }", "required: yes }"), 21,
            "'yes' is not true or false");
    Refuses(With(kCoordinatesBlock, "coordinates: right"), 6,
            "coordinates must be a mapping");
    Refuses(With("ignoredJoints: [reference, propHandle]",
                 "ignoredJoints: reference"),
            27, "ignoredJoints must be a sequence");
    Refuses(With("  hip:      { bone: hips, required: true }",
                 "  hip:      hips"),
            21, "must be a mapping");
    Refuses(With(kJointsBlock, "joints: none"), 20,
            "joints must be a mapping");
}

void
TestSchemaVersion()
{
    Refuses(With("schemaVersion: 1", "schemaVersion: 2"), 2,
            "schemaVersion '2' is not 1");
    // Compared as text: `1.0` is a file written against a different idea of this
    // key, and reading it as the number 1 would make the version the one field
    // this reader guesses at.
    Refuses(With("schemaVersion: 1", "schemaVersion: 1.0"), 2, "is not 1");
    Refuses(With("schemaVersion: 1\n", ""), 0, "states no 'schemaVersion'");
}

// The small language's own refusals, each about a line.
void
TestMalformedText()
{
    Refuses(With("  upAxis: +Y", "\tupAxis: +Y"), 8, "indentation carries a tab");
    Refuses(With("  upAxis: +Y", "    upAxis: +Y"), 8, "unexpected indentation");
    Refuses(With("restPose: rest-offsets", "restPose"), 17, "expected 'key: value'");
    Refuses(With("coordinates:\n", "coordinates:\nrestPose: first-frame\n"), 6,
            "states no value");
    Refuses(With("producer: Example Recorder", "producer: \"Example Recorder"),
            4, "no closing '\"'");
    Refuses(With("{ bone: hips, required: true }", "{ bone: hips"), 21,
            "expected a closing '}'");
    Refuses(With("ignoredJoints: [reference, propHandle]",
                 "ignoredJoints: [reference, ]"),
            27, "is empty");
    Refuses(With("schemaVersion: 1", "  schemaVersion: 1"), 2,
            "the first line is indented");
    Refuses("", 0, "states nothing");
    Refuses("# nothing but a comment\n", 0, "states nothing");
}

// A file can be a document this reader understands completely and still not be a
// profile. That refusal is `ValidateSourceProfile`'s, reported at line 0 in its
// own words rather than restated here -- one statement of what a profile is.
void
TestAWellFormedFileThatIsNotAProfile()
{
    Refuses(With("  hip:      { bone: hips, required: true }\n", ""), 0,
            "maps no hips");
    Refuses(With("{ bone: hips, required: true }", "{ bone: hips }"), 0,
            "maps the canonical root and is not required");
    Refuses(With("  chest:    { bone: chest }", "  chest:    { bone: spine }"), 0,
            "map the same bone spine");
    Refuses(With("ignoredJoints: [reference, propHandle]",
                 "ignoredJoints: [reference, head]"),
            0, "is both mapped and ignored");
    Refuses(With("  forwardAxis: +Z", "  forwardAxis: -Y"), 0,
            "are the same axis");
}

// The loaded value is the one the rest of this layer takes, not a lookalike:
// what a file produces is matched against a rig and binds.
void
TestALoadedProfileMatchesARig()
{
    SourceSkeleton skeleton;
    const auto joint = [&skeleton](std::string name, int parent) {
        SourceJoint added;
        added.name = std::move(name);
        added.parent = parent;
        skeleton.joints.push_back(std::move(added));
    };
    joint("reference", -1);
    joint("hip", 0);
    joint("spine", 1);
    joint("chest", 2);
    joint("neck", 3);
    joint("head", 4);
    joint("propHandle", 0);

    const SourceProfile profile = Parse(kProfileText);
    const motionSource::SourceProfileMatch match =
        MatchSourceProfile(profile, skeleton);
    assert(match.refusal == SourceProfileRefusal::None);
    assert(match.bound.size() == 5);
    assert(match.BoundRequiredCount() == profile.RequiredMappingCount());
    assert(match.JointFor(Bone::Head) == 5u);
    assert(match.unmappedJoints.empty());
}

void
TestFiles()
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / "motionSource_profile_file_test.yaml";
    {
        std::ofstream file(path, std::ios::binary);
        file << kProfileText;
    }
    SourceProfile profile;
    SourceProfileParseError error;
    assert(ParseSourceProfileFile(path, &profile, &error));
    assert(profile == ExpectedProfile());
    std::filesystem::remove(path);

    // A path that is not a file is a refusal about no line, in the words the
    // reader below this layer uses for the same failure -- this layer has no
    // vocabulary for I/O and does not invent one.
    SourceProfile untouched = ExpectedProfile();
    assert(!ParseSourceProfileFile(path / "absent.yaml", &untouched, &error));
    assert(error.line == 0);
    assert(error.reason.find("could not be opened") != std::string::npos);
    assert(untouched == ExpectedProfile());
}

// Every profile file this repository ships, read by the library that defines
// what a profile is. It asserts nothing about *which* profiles exist or what
// they say -- that would be this layer learning a producer through its test
// suite. What it asserts is the property no individual profile can: that a file
// in the shipped directory is one this reader accepts, so a profile added there
// cannot be unloadable and unnoticed until a conversion asks for it.
int
CheckShippedProfiles(const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory)) {
        std::printf("not a directory: %s\n", directory.string().c_str());
        return 1;
    }
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".yaml") {
            files.push_back(entry.path());
        }
    }
    // Sorted, so the report is the same on every filesystem.
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::printf("no profile in %s\n", directory.string().c_str());
        return 1;
    }

    int failures = 0;
    for (const std::filesystem::path& file : files) {
        SourceProfile profile;
        SourceProfileParseError error;
        if (!ParseSourceProfileFile(file, &profile, &error)) {
            std::printf("%s:%zu: %s\n", file.filename().string().c_str(),
                        error.line, error.reason.c_str());
            ++failures;
            continue;
        }
        // The id is the name a conversion records and a caller asks for, so a
        // file whose name and id disagree is a profile nobody can reach twice.
        if (profile.id != file.stem().string()) {
            std::printf("%s: states id '%s'\n", file.filename().string().c_str(),
                        profile.id.c_str());
            ++failures;
            continue;
        }
        std::printf("%s: %zu joints, %zu required, %zu ignored\n",
                    profile.id.c_str(), profile.joints.size(),
                    profile.RequiredMappingCount(),
                    profile.ignoredJoints.size());
    }
    if (failures != 0) {
        return 1;
    }
    std::printf("motionSource shipped profiles: %zu verified\n", files.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckShippedProfiles(argv[1]);
    }

    TestTheKeys();
    TestTheOtherShapes();
    TestQuotingAndComments();
    TestUnknownKeysAreRefused();
    TestMissingKeys();
    TestDuplicateKeys();
    TestUnknownVocabulary();
    TestValueShapes();
    TestSchemaVersion();
    TestMalformedText();
    TestAWellFormedFileThatIsNotAProfile();
    TestALoadedProfileMatchesARig();
    TestFiles();
    std::printf("motionSource profile file: verified\n");
    return 0;
}
