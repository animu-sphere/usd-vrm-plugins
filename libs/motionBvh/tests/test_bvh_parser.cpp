// SPDX-License-Identifier: Apache-2.0
//
// The parser, in two halves that were not written from each other.
//
// The unit cases below build BVH text character by character, because the point
// of this layer is what it does with characters: a tab, a CRLF, a byte-order
// mark, a lowercase keyword, and a colon in the wrong place are all things a
// real writer emits and none of them are a difference in meaning.
//
// Corpus mode then runs the same parser over every committed fixture, against a
// table of what each file must produce or refuse. `tools/check_corpus.py`
// measures those same files independently, so the fixtures are pinned by two
// implementations rather than by this one.
//
// It runs over each half of the corpus separately — `generated/` holds shapes
// of the format, `recorded/redistributable/` holds real producer exports — and
// each half has its own table, so a file in the wrong one fails rather than
// passing quietly.
#include "motionBvh/BvhParser.h"

#include "motionBvh/BvhDocument.h"
#include "motionBvh/Diagnostics.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using motionBvh::BvhChannel;
using motionBvh::BvhDocument;
using motionBvh::BvhParseOptions;
using motionBvh::Diagnostic;
using motionBvh::DiagnosticCode;

constexpr std::string_view kMinimal =
    "HIERARCHY\n"
    "ROOT Hips\n"
    "{\n"
    "\tOFFSET 0.0 0.0 0.0\n"
    "\tCHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n"
    "\tJOINT Spine\n"
    "\t{\n"
    "\t\tOFFSET 0.0 10.5 0.0\n"
    "\t\tCHANNELS 3 Zrotation Xrotation Yrotation\n"
    "\t\tEnd Site\n"
    "\t\t{\n"
    "\t\t\tOFFSET 0.0 5.25 0.0\n"
    "\t\t}\n"
    "\t}\n"
    "}\n"
    "MOTION\n"
    "Frames: 2\n"
    "Frame Time: 0.5\n"
    "0.0 90.0 0.0 1.0 2.0 3.0 4.0 5.0 6.0\n"
    "0.5 90.0 0.0 7.0 8.0 9.0 10.0 11.0 12.0\n";

bool
Parses(std::string_view text, BvhDocument* document = nullptr)
{
    BvhDocument local;
    return motionBvh::ParseBvhText(text, document ? document : &local);
}

Diagnostic
Refusal(std::string_view text, const BvhParseOptions& options = {})
{
    BvhDocument document;
    Diagnostic diagnostic;
    const bool parsed =
        motionBvh::ParseBvhText(text, &document, &diagnostic, options);
    assert(!parsed);
    // Every refusal this layer raises is a syntax code: it does not know what a
    // profile is, so it cannot be the layer that disagrees with one.
    assert(motionBvh::DiagnosticIsSyntax(diagnostic.code));
    return diagnostic;
}

void
TestMinimalDocument()
{
    BvhDocument document;
    Diagnostic diagnostic;
    assert(motionBvh::ParseBvhText(kMinimal, &document, &diagnostic));
    assert(motionBvh::ValidateBvhDocument(document));

    assert(document.joints.size() == 2);
    assert(document.joints[0].name == "Hips");
    assert(document.joints[0].parent == -1);
    assert(document.joints[0].offset == motionBvh::BvhVec3{});
    assert(document.joints[0].channels.size() == 6);
    assert(document.joints[0].channelOffset == 0);
    assert(!document.joints[0].endSiteOffset);

    assert(document.joints[1].name == "Spine");
    assert(document.joints[1].parent == 0);
    assert((document.joints[1].offset == motionBvh::BvhVec3{0.0f, 10.5f, 0.0f}));
    assert(document.joints[1].channels.size() == 3);
    assert(document.joints[1].channelOffset == 6);
    assert(document.joints[1].endSiteOffset);
    assert((*document.joints[1].endSiteOffset
            == motionBvh::BvhVec3{0.0f, 5.25f, 0.0f}));

    assert(document.channelCount == 9);
    assert(document.frameCount == 2);
    assert(document.frameTime == 0.5);
    assert(document.values.size() == 18);
    assert(document.ChannelValue(0, 1, 0) == 4.0f);
    assert(document.ChannelValue(1, 0, 0) == 0.5f);
    assert(document.ChannelValue(1, 1, 2) == 12.0f);
}

// The file's Euler order *is* its rotation-channel declaration order, so a
// parser that normalised channels would destroy the only statement the file
// makes about it -- invisibly, until something came out rotated.
void
TestDeclarationOrderIsRetained()
{
    const std::string text =
        "HIERARCHY\n"
        "ROOT Root\n"
        "{\n"
        "OFFSET 0 0 0\n"
        "CHANNELS 6 Xposition Yposition Zposition Yrotation Xrotation Zrotation\n"
        "}\n"
        "MOTION\n"
        "Frames: 1\n"
        "Frame Time: 0.04\n"
        "0 0 0 10 20 30\n";
    BvhDocument document;
    assert(motionBvh::ParseBvhText(text, &document));
    const std::vector<BvhChannel>& channels = document.joints[0].channels;
    assert(channels[3] == BvhChannel::Yrotation);
    assert(channels[4] == BvhChannel::Xrotation);
    assert(channels[5] == BvhChannel::Zrotation);
    // And the values follow the same order rather than a canonical one.
    assert(document.ChannelValue(0, 0, 3) == 10.0f);
    assert(document.ChannelValue(0, 0, 4) == 20.0f);
    assert(document.ChannelValue(0, 0, 5) == 30.0f);
}

// A writer's line endings, byte-order mark, keyword case, colon placement and
// padding are not statements about the motion.
void
TestWriterVariation()
{
    std::string crlf;
    for (const char c : kMinimal) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    BvhDocument fromCrlf;
    BvhDocument fromLf;
    assert(motionBvh::ParseBvhText(crlf, &fromCrlf));
    assert(motionBvh::ParseBvhText(kMinimal, &fromLf));
    assert(fromCrlf.values == fromLf.values);
    assert(fromCrlf.frameTime == fromLf.frameTime);
    assert(fromCrlf.joints.size() == fromLf.joints.size());

    // A Windows exporter's byte-order mark is not part of the HIERARCHY
    // keyword, and refusing a file for it would be refusing a file nothing is
    // wrong with.
    const std::string bom = "\xEF\xBB\xBF" + std::string(kMinimal);
    BvhDocument fromBom;
    assert(motionBvh::ParseBvhText(bom, &fromBom));
    assert(fromBom.values == fromLf.values);

    const std::string_view lowercase =
        "hierarchy\n"
        "root hips\n"
        "{\n"
        "  offset 0 0 0\n"
        "  channels 3 zrotation xrotation yrotation\n"
        "  end site\n"
        "  {\n"
        "    offset 0 1 0\n"
        "  }\n"
        "}\n"
        "motion\n"
        "Frames : 1\n"
        "Frame Time : 0.04\n"
        "\n"
        "   1.0   2.0   3.0   \n"
        "\n";
    BvhDocument lowered;
    assert(motionBvh::ParseBvhText(lowercase, &lowered));
    assert(lowered.joints.size() == 1);
    assert(lowered.joints[0].name == "hips"); // verbatim, never folded
    assert(lowered.joints[0].channels[0] == BvhChannel::Zrotation);
    assert(lowered.joints[0].endSiteOffset);
    assert(lowered.values == std::vector<float>({1.0f, 2.0f, 3.0f}));
}

// A joint with no CHANNELS is legal: a producer exporting a static prop beside
// the rig is not writing a broken file, and what to do about the prop is a
// profile's decision one layer up.
void
TestStaticJointAndEmptyMotion()
{
    const std::string_view staticJoint =
        "HIERARCHY\n"
        "ROOT Hips\n"
        "{\n"
        "OFFSET 0 0 0\n"
        "CHANNELS 3 Zrotation Xrotation Yrotation\n"
        "JOINT Prop\n"
        "{\n"
        "OFFSET 1 2 3\n"
        "CHANNELS 0\n"
        "}\n"
        "}\n"
        "MOTION\n"
        "Frames: 1\n"
        "Frame Time: 0.04\n"
        "1 2 3\n";
    BvhDocument document;
    assert(motionBvh::ParseBvhText(staticJoint, &document));
    assert(document.joints.size() == 2);
    assert(document.joints[1].channels.empty());
    assert(document.joints[1].channelOffset == 3);
    assert(document.channelCount == 3);

    const std::string_view empty =
        "HIERARCHY\n"
        "ROOT Hips\n"
        "{\n"
        "OFFSET 0 0 0\n"
        "CHANNELS 3 Zrotation Xrotation Yrotation\n"
        "}\n"
        "MOTION\n"
        "Frames: 0\n"
        "Frame Time: 0.0333333\n";
    BvhDocument none;
    assert(motionBvh::ParseBvhText(empty, &none));
    assert(none.frameCount == 0);
    assert(none.values.empty());
    assert(none.Frame(0) == nullptr);
}

void
TestSyntaxRefusals()
{
    // Not a BVH file at all.
    assert(Refusal("").code == DiagnosticCode::ParseFailed);
    assert(Refusal("MOTION\n").code == DiagnosticCode::ParseFailed);
    assert(Refusal("HIERARCHY\nJOINT Hips\n").code
           == DiagnosticCode::ParseFailed);
    assert(Refusal("HIERARCHY\nROOT\n{\n").code == DiagnosticCode::ParseFailed);

    const std::string prefix =
        "HIERARCHY\n"
        "ROOT Hips\n"
        "{\n";
    const std::string body =
        "OFFSET 0 0 0\n"
        "CHANNELS 3 Zrotation Xrotation Yrotation\n";
    const std::string motion =
        "MOTION\n"
        "Frames: 1\n"
        "Frame Time: 0.04\n"
        "1 2 3\n";

    // An unbalanced brace, and a joint that never closes.
    assert(Refusal(prefix + body).code == DiagnosticCode::ParseFailed);
    assert(Refusal(prefix + body + "JOINT Spine\n{\n" + body + "}\n" + motion)
               .code
           == DiagnosticCode::ParseFailed);

    // A channel this format model cannot represent. Refused rather than
    // retained as an unknown column: nothing above could attribute its values.
    const Diagnostic channel = Refusal(
        prefix + "OFFSET 0 0 0\nCHANNELS 4 Zrotation Xrotation Yrotation "
                 "Wrotation\n}\n" + motion);
    assert(channel.code == DiagnosticCode::UnsupportedChannel);
    assert(channel.subject == "Wrotation");
    assert(channel.line == 5);

    // A CHANNELS count the list does not carry. Reporting `JOINT` as an
    // unsupported channel name would send the reader looking for a channel.
    const Diagnostic count =
        Refusal(prefix + "OFFSET 0 0 0\nCHANNELS 6 Zrotation Xrotation "
                         "Yrotation\nJOINT Spine\n{\n" + body + "}\n}\n"
                + motion);
    assert(count.code == DiagnosticCode::ParseFailed);
    assert(count.subject == "JOINT");

    // No OFFSET at all: a joint with no offset is not a bone.
    assert(Refusal(prefix + "CHANNELS 3 Zrotation Xrotation Yrotation\n}\n"
                   + motion)
               .code
           == DiagnosticCode::ParseFailed);
    // And each of OFFSET, CHANNELS and End Site at most once, because the model
    // holds one of each and silently keeping the last would lose the file's
    // disagreement with itself.
    assert(Refusal(prefix + body + "OFFSET 1 1 1\n}\n" + motion).code
           == DiagnosticCode::ParseFailed);
    assert(Refusal(prefix + body + "CHANNELS 3 Zrotation Xrotation Yrotation\n"
                                   "}\n" + motion)
               .code
           == DiagnosticCode::ParseFailed);
    assert(Refusal(prefix + body + "End Site\n{\nOFFSET 0 1 0\n}\n"
                                   "End Site\n{\nOFFSET 0 2 0\n}\n}\n" + motion)
               .code
           == DiagnosticCode::ParseFailed);
}

void
TestMotionRefusals()
{
    const std::string head =
        "HIERARCHY\n"
        "ROOT Hips\n"
        "{\n"
        "OFFSET 0 0 0\n"
        "CHANNELS 3 Zrotation Xrotation Yrotation\n"
        "}\n"
        "MOTION\n";

    // A short row is reported on the row that is short, not at the end of the
    // file -- which is the whole reason the motion section is read as lines.
    const Diagnostic narrow =
        Refusal(head + "Frames: 3\nFrame Time: 0.04\n1 2 3\n1 2\n1 2 3\n");
    assert(narrow.code == DiagnosticCode::FrameWidthMismatch);
    assert(narrow.line == 11);
    assert(narrow.subject == "frame 1");

    const Diagnostic wide =
        Refusal(head + "Frames: 2\nFrame Time: 0.04\n1 2 3\n1 2 3 4\n");
    assert(wide.code == DiagnosticCode::FrameWidthMismatch);
    assert(wide.line == 11);

    // There is no frame-count code, and inventing one the moment a parser meets
    // this file is exactly the drift a frozen set prevents.
    const Diagnostic tooFew =
        Refusal(head + "Frames: 3\nFrame Time: 0.04\n1 2 3\n1 2 3\n");
    assert(tooFew.code == DiagnosticCode::ParseFailed);
    assert(tooFew.detail.find("declared 3") != std::string::npos);

    const Diagnostic tooMany =
        Refusal(head + "Frames: 1\nFrame Time: 0.04\n1 2 3\n1 2 3\n");
    assert(tooMany.code == DiagnosticCode::ParseFailed);
    assert(tooMany.line == 11);

    // Frame time: absent, unreadable, negative, or zero across more than one
    // frame.
    assert(Refusal(head + "Frames: 1\nFrame Time:\n").code
           == DiagnosticCode::ParseFailed);
    assert(Refusal(head + "Frames: 1\nFrame Time: soon\n1 2 3\n").code
           == DiagnosticCode::InvalidFrameTime);
    assert(Refusal(head + "Frames: 1\nFrame Time: -0.04\n1 2 3\n").code
           == DiagnosticCode::InvalidFrameTime);
    assert(Refusal(head + "Frames: 2\nFrame Time: 0.0\n1 2 3\n1 2 3\n").code
           == DiagnosticCode::InvalidFrameTime);
    // ... but a single pose has no interval to describe.
    assert(Parses(head + "Frames: 1\nFrame Time: 0.0\n1 2 3\n"));
    assert(Parses(head + "Frames: 0\nFrame Time: 0.0\n"));

    assert(Refusal(head + "Frames: many\nFrame Time: 0.04\n").code
           == DiagnosticCode::ParseFailed);
    assert(Refusal(head + "Frames: -1\nFrame Time: 0.04\n").code
           == DiagnosticCode::ParseFailed);

    // A number that is not one, and numbers that are not finite. An overflowing
    // literal is the same refusal as a literal `inf`: neither has an
    // interpretation at any layer above.
    assert(Refusal(head + "Frames: 1\nFrame Time: 0.04\n1 two 3\n").code
           == DiagnosticCode::ParseFailed);
    assert(Refusal(head + "Frames: 1\nFrame Time: 0.04\n1 nan 3\n").code
           == DiagnosticCode::NonFiniteValue);
    assert(Refusal(head + "Frames: 1\nFrame Time: 0.04\n1 -inf 3\n").code
           == DiagnosticCode::NonFiniteValue);
    assert(Refusal(head + "Frames: 1\nFrame Time: 0.04\n1 1e400 3\n").code
           == DiagnosticCode::NonFiniteValue);
    assert(Refusal("HIERARCHY\nROOT Hips\n{\nOFFSET 0 nan 0\n"
                   "CHANNELS 3 Zrotation Xrotation Yrotation\n}\n"
                   "MOTION\nFrames: 1\nFrame Time: 0.04\n1 2 3\n")
               .code
           == DiagnosticCode::NonFiniteValue);
}

// Refusals of the pathological case, checked by lowering the limit rather than
// by committing a pathological fixture.
void
TestLimits()
{
    std::string deep = "HIERARCHY\nROOT J0\n{\nOFFSET 0 0 0\nCHANNELS 0\n";
    for (int level = 1; level < 6; ++level) {
        deep += "JOINT J" + std::to_string(level) + "\n{\nOFFSET 0 1 0\n"
                "CHANNELS 0\n";
    }
    for (int level = 0; level < 6; ++level) {
        deep += "}\n";
    }
    deep += "MOTION\nFrames: 0\nFrame Time: 0.04\n";

    assert(Parses(deep));

    BvhParseOptions shallow;
    shallow.limits.maxHierarchyDepth = 3;
    assert(Refusal(deep, shallow).code == DiagnosticCode::ParseFailed);

    BvhParseOptions fewJoints;
    fewJoints.limits.maxJoints = 3;
    assert(Refusal(deep, fewJoints).code == DiagnosticCode::ParseFailed);

    BvhParseOptions fewFrames;
    fewFrames.limits.maxFrames = 1;
    // A declared count is refused before it is trusted, so this allocates
    // nothing.
    const std::string many =
        "HIERARCHY\nROOT Hips\n{\nOFFSET 0 0 0\nCHANNELS 0\n}\n"
        "MOTION\nFrames: 2000000000\nFrame Time: 0.04\n";
    const Diagnostic refused = Refusal(many, fewFrames);
    assert(refused.code == DiagnosticCode::ParseFailed);
    assert(refused.subject == "2000000000");
}

// A half-read document is worse than a refused one: the layer above cannot tell
// which half it got, and the producer gets blamed for a file it wrote whole.
void
TestFailureLeavesTheDocumentUntouched()
{
    BvhDocument document;
    assert(motionBvh::ParseBvhText(kMinimal, &document));
    const BvhDocument before = document;

    Diagnostic diagnostic;
    assert(!motionBvh::ParseBvhText("HIERARCHY\nROOT Hips\n{\n", &document,
                                    &diagnostic));
    assert(document.joints.size() == before.joints.size());
    assert(document.values == before.values);
    assert(document.frameCount == before.frameCount);
    assert(document.channelCount == before.channelCount);
}

void
TestDeterminism()
{
    BvhDocument first;
    BvhDocument second;
    assert(motionBvh::ParseBvhText(kMinimal, &first));
    assert(motionBvh::ParseBvhText(kMinimal, &second));
    assert(first.values == second.values);
    assert(first.channelCount == second.channelCount);
    assert(first.frameTime == second.frameTime);
    for (std::size_t index = 0; index < first.joints.size(); ++index) {
        assert(first.joints[index].name == second.joints[index].name);
        assert(first.joints[index].offset == second.joints[index].offset);
        assert(first.joints[index].channels == second.joints[index].channels);
        assert(first.joints[index].channelOffset
               == second.joints[index].channelOffset);
    }
}

void
TestDiagnosticSource()
{
    BvhParseOptions options;
    options.source = "fixture.bvh";
    const Diagnostic diagnostic = Refusal("nonsense\n", options);
    assert(diagnostic.source == "fixture.bvh");
    assert(diagnostic.line == 1);
    assert(diagnostic.subject == "nonsense");

    // A file that cannot be opened has no code of its own: inventing one would
    // widen the frozen set for a failure that is not about BVH.
    BvhDocument document;
    Diagnostic missing;
    assert(!motionBvh::ParseBvhFile("does-not-exist.bvh", &document, &missing));
    assert(missing.code == DiagnosticCode::ParseFailed);
    assert(missing.source == "does-not-exist.bvh");
}

// ---------------------------------------------------------------------------
// Corpus mode
// ---------------------------------------------------------------------------

struct Expectation
{
    bool parses = true;
    DiagnosticCode code = DiagnosticCode::ParseFailed;
    std::size_t joints = 0;
    std::size_t channels = 0;
    std::size_t frames = 0;
    double frameTime = 0.0;
};

// Two tables, because the corpus has two halves and a fixture belonging to the
// wrong one is a failure worth having. The generated half is shapes of the
// format; the recorded half is real producer exports, and the difference is not
// stylistic — see tests/corpus/recorded/manifest.json.
//
// The recorded rows say only what the parser must *read*. What the file means —
// its unit, its axes, what its root translation is — lives in that manifest as
// observations, because this layer may not act on any of it.
const std::map<std::string, Expectation>&
CorpusExpectations(const std::string& half)
{
    static const std::map<std::string, Expectation> recorded = {
        {"mocopi-mobile-arm-raise-turn.bvh", {true, {}, 27, 162, 853, 0.02}},
    };
    static const std::map<std::string, Expectation> generated = {
        {"valid-minimal-root.bvh", {true, {}, 1, 6, 2, 0.0333333}},
        {"valid-nested-joints.bvh", {true, {}, 4, 15, 3, 0.0333333}},
        {"valid-channel-order-yxz.bvh", {true, {}, 2, 9, 2, 0.0166667}},
        {"valid-position-on-joint.bvh", {true, {}, 2, 12, 2, 0.04}},
        {"valid-empty-motion.bvh", {true, {}, 1, 6, 0, 0.0333333}},
        {"valid-single-frame-zero-time.bvh", {true, {}, 1, 6, 1, 0.0}},
        {"valid-lowercase-and-padding.bvh", {true, {}, 2, 9, 2, 0.0333333}},
        {"valid-static-joint.bvh", {true, {}, 2, 6, 2, 0.0333333}},
        {"valid-duplicate-joint-names.bvh", {true, {}, 3, 12, 1, 0.0333333}},

        {"malformed-missing-hierarchy.bvh",
         {false, DiagnosticCode::ParseFailed}},
        {"malformed-unclosed-joint.bvh", {false, DiagnosticCode::ParseFailed}},
        {"malformed-unsupported-channel.bvh",
         {false, DiagnosticCode::UnsupportedChannel}},
        {"malformed-frame-width.bvh",
         {false, DiagnosticCode::FrameWidthMismatch}},
        {"malformed-frame-count.bvh", {false, DiagnosticCode::ParseFailed}},
        {"malformed-frame-time.bvh", {false, DiagnosticCode::InvalidFrameTime}},
        {"malformed-non-finite.bvh", {false, DiagnosticCode::NonFiniteValue}},
        {"malformed-missing-offset.bvh", {false, DiagnosticCode::ParseFailed}},
        {"malformed-channel-count.bvh", {false, DiagnosticCode::ParseFailed}},
        {"malformed-no-motion.bvh", {false, DiagnosticCode::ParseFailed}},
    };
    return half == "recorded" ? recorded : generated;
}

int
RunCorpus(const std::filesystem::path& directory, const std::string& half)
{
    if (!std::filesystem::is_directory(directory)) {
        std::fprintf(stderr, "corpus directory not found: %s\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    std::size_t verified = 0;
    std::set<std::string> seen;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".bvh") {
            continue;
        }
        const std::string name = entry.path().filename().string();
        seen.insert(name);

        const auto expectation = CorpusExpectations(half).find(name);
        if (expectation == CorpusExpectations(half).end()) {
            // A fixture nobody stated an expectation for is a fixture that
            // proves nothing, so adding one without a row here fails.
            std::fprintf(stderr, "%s: no expectation in the table\n",
                         name.c_str());
            ++failures;
            continue;
        }

        BvhDocument document;
        Diagnostic diagnostic;
        const bool parsed =
            motionBvh::ParseBvhFile(entry.path(), &document, &diagnostic);
        if (parsed != expectation->second.parses) {
            std::fprintf(stderr, "%s: expected %s, got %s (%s)\n", name.c_str(),
                         expectation->second.parses ? "a document" : "a refusal",
                         parsed ? "a document" : "a refusal",
                         parsed ? "" : FormatDiagnostic(diagnostic).c_str());
            ++failures;
            continue;
        }

        if (!parsed) {
            if (diagnostic.code != expectation->second.code) {
                std::fprintf(stderr, "%s: expected %s, got %s\n", name.c_str(),
                             std::string(motionBvh::DiagnosticCodeString(
                                             expectation->second.code))
                                 .c_str(),
                             FormatDiagnostic(diagnostic).c_str());
                ++failures;
                continue;
            }
            // Every refusal names where it happened; a refusal without a place
            // sends the reader through the whole file.
            if (!diagnostic.line) {
                std::fprintf(stderr, "%s: refusal carries no line\n",
                             name.c_str());
                ++failures;
                continue;
            }
            ++verified;
            continue;
        }

        const Expectation& want = expectation->second;
        bool ok = true;
        if (document.joints.size() != want.joints) {
            std::fprintf(stderr, "%s: %zu joints, expected %zu\n", name.c_str(),
                         document.joints.size(), want.joints);
            ok = false;
        }
        if (document.channelCount != want.channels) {
            std::fprintf(stderr, "%s: %zu channels, expected %zu\n",
                         name.c_str(), document.channelCount, want.channels);
            ok = false;
        }
        if (document.frameCount != want.frames) {
            std::fprintf(stderr, "%s: %zu frames, expected %zu\n", name.c_str(),
                         document.frameCount, want.frames);
            ok = false;
        }
        if (document.frameTime != want.frameTime) {
            std::fprintf(stderr, "%s: frame time %.9g, expected %.9g\n",
                         name.c_str(), document.frameTime, want.frameTime);
            ok = false;
        }
        if (document.values.size() != want.frames * want.channels) {
            std::fprintf(stderr, "%s: %zu values, expected %zu\n", name.c_str(),
                         document.values.size(), want.frames * want.channels);
            ok = false;
        }
        Diagnostic validation;
        if (!motionBvh::ValidateBvhDocument(document, &validation)) {
            std::fprintf(stderr, "%s: %s\n", name.c_str(),
                         FormatDiagnostic(validation).c_str());
            ok = false;
        }
        if (!ok) {
            ++failures;
            continue;
        }
        ++verified;
    }

    for (const auto& [name, expectation] : CorpusExpectations(half)) {
        (void)expectation;
        if (seen.find(name) == seen.end()) {
            std::fprintf(stderr, "%s: expected fixture is missing\n",
                         name.c_str());
            ++failures;
        }
    }

    if (failures > 0) {
        std::fprintf(stderr, "%d corpus fixture(s) failed\n", failures);
        return 1;
    }
    std::printf("motionBvh corpus: %zu fixture(s) verified\n", verified);
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        // A second argument names the half; without one this is the generated
        // corpus, which is what every existing caller means.
        return RunCorpus(argv[1], argc > 2 ? argv[2] : "generated");
    }

    TestMinimalDocument();
    TestDeclarationOrderIsRetained();
    TestWriterVariation();
    TestStaticJointAndEmptyMotion();
    TestSyntaxRefusals();
    TestMotionRefusals();
    TestLimits();
    TestFailureLeavesTheDocumentUntouched();
    TestDeterminism();
    TestDiagnosticSource();
    std::printf("motionBvh parser: verified\n");
    return 0;
}
