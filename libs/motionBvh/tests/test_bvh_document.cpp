// SPDX-License-Identifier: Apache-2.0
//
// The syntax model, checked without a parser anywhere near it.
//
// Everything here builds a `BvhDocument` by hand, which is the case
// `ValidateBvhDocument` exists for: the parser is not the only thing that can
// produce one, and every layer above is entitled to the invariants rather than
// to a re-derivation of them.
#include "motionBvh/BvhDocument.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{

using motionBvh::BvhChannel;
using motionBvh::BvhDocument;
using motionBvh::BvhJoint;
using motionBvh::BvhVec3;
using motionBvh::Diagnostic;
using motionBvh::DiagnosticCode;

BvhJoint
MakeJoint(std::string name, int parent, std::vector<BvhChannel> channels,
          std::size_t channelOffset)
{
    BvhJoint joint;
    joint.name = std::move(name);
    joint.parent = parent;
    joint.channels = std::move(channels);
    joint.channelOffset = channelOffset;
    return joint;
}

// Hips (6 channels) -> Spine (3 channels), two frames.
BvhDocument MakeDocument()
{
    BvhDocument document;
    document.joints.push_back(MakeJoint(
        "Hips", -1,
        {BvhChannel::Xposition, BvhChannel::Yposition, BvhChannel::Zposition,
         BvhChannel::Zrotation, BvhChannel::Xrotation, BvhChannel::Yrotation},
        0));
    document.joints.push_back(MakeJoint(
        "Spine", 0,
        {BvhChannel::Zrotation, BvhChannel::Xrotation, BvhChannel::Yrotation},
        6));
    document.joints[1].endSiteOffset = BvhVec3{0.0f, 5.0f, 0.0f};
    document.channelCount = 9;
    document.frameCount = 2;
    document.frameTime = 0.5;
    document.values = {
        0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f,
    };
    return document;
}

void
TestChannelVocabulary()
{
    assert(motionBvh::BvhChannelName(BvhChannel::Xposition) == "Xposition");
    assert(motionBvh::BvhChannelName(BvhChannel::Zrotation) == "Zrotation");
    assert(motionBvh::BvhChannelName(BvhChannel::Count).empty());

    // Writers disagree about case, and a case difference is not a meaning
    // difference.
    assert(motionBvh::FindBvhChannel("Xrotation") == BvhChannel::Xrotation);
    assert(motionBvh::FindBvhChannel("xrotation") == BvhChannel::Xrotation);
    assert(motionBvh::FindBvhChannel("XROTATION") == BvhChannel::Xrotation);
    assert(motionBvh::FindBvhChannel("XRotation") == BvhChannel::Xrotation);

    assert(!motionBvh::FindBvhChannel("Wrotation"));
    assert(!motionBvh::FindBvhChannel("Xscale"));
    assert(!motionBvh::FindBvhChannel("Xrotation "));
    assert(!motionBvh::FindBvhChannel(""));

    for (std::size_t index = 0; index < motionBvh::BvhChannelCount; ++index) {
        const auto channel = static_cast<BvhChannel>(index);
        assert(motionBvh::BvhChannelIsPosition(channel)
               != motionBvh::BvhChannelIsRotation(channel));
        assert(motionBvh::FindBvhChannel(motionBvh::BvhChannelName(channel))
               == channel);
    }
}

void
TestAccessors()
{
    const BvhDocument document = MakeDocument();
    assert(motionBvh::ValidateBvhDocument(document));

    const float* frame0 = document.Frame(0);
    const float* frame1 = document.Frame(1);
    assert(frame0 && frame1);
    assert(frame1 - frame0 == 9);
    assert(frame0[0] == 0.0f);
    assert(frame1[8] == 17.0f);
    assert(document.Frame(2) == nullptr);

    // `channelIndex` indexes the joint's own declaration list, not the six
    // channel values -- the whole point of retaining declaration order.
    assert(document.ChannelValue(0, 0, 0) == 0.0f);
    assert(document.ChannelValue(0, 1, 0) == 6.0f);
    assert(document.ChannelValue(1, 1, 2) == 17.0f);
    assert(!document.ChannelValue(0, 1, 3));
    assert(!document.ChannelValue(0, 2, 0));
    assert(!document.ChannelValue(2, 0, 0));

    assert(document.FindJoint("Spine") == 1u);
    assert(!document.FindJoint("spine")); // names are verbatim, never folded
    assert(!document.FindJoint("Head"));
    assert(document.HasUniqueJointNames());
    assert(document.Depth(0) == 0);
    assert(document.Depth(1) == 1);
    assert(document.MaxDepth() == 1);
    assert(document.TotalDeclaredChannels() == 9);
}

// BVH does not require joint names to be unique and real exports repeat them,
// so a profile matching by name has to be able to see the ambiguity rather than
// silently take the first match.
void
TestDuplicateNames()
{
    BvhDocument document = MakeDocument();
    document.joints[1].name = "Hips";
    assert(!document.HasUniqueJointNames());
    assert(document.FindJoint("Hips") == 0u);
    const std::vector<std::size_t> matches = document.FindJoints("Hips");
    assert(matches.size() == 2);
    assert(matches[0] == 0 && matches[1] == 1);
    assert(document.FindJoints("Spine").empty());
}

// `Depth` and `MaxDepth` walk parent links, and a hand-assembled document can
// point one at itself. That walk has to terminate on the type's own terms:
// "parents precede their children" is an invariant of what validation accepts,
// not of what a caller can build, and a `noexcept` accessor that hangs is worse
// than one that refuses. Both of these looped forever before the bound.
void
TestCyclicParentTerminates()
{
    {
        BvhDocument document;
        document.joints.push_back(MakeJoint("Hips", 0, {}, 0));
        assert(document.Depth(0) == 0);
        assert(document.MaxDepth() == 0);
        assert(!motionBvh::ValidateBvhDocument(document));
    }
    {
        // A two-joint cycle, which no single index check would catch.
        BvhDocument document;
        document.joints.push_back(MakeJoint("A", 1, {}, 0));
        document.joints.push_back(MakeJoint("B", 0, {}, 0));
        assert(document.Depth(0) == 0);
        assert(document.Depth(1) == 0);
        assert(document.MaxDepth() == 0);
        assert(!motionBvh::ValidateBvhDocument(document));
    }
    // The accepted shape still reports real depths, so the bound did not turn
    // every answer into zero.
    const BvhDocument valid = MakeDocument();
    assert(valid.Depth(1) == 1);
    assert(valid.MaxDepth() == 1);
}

void
TestValidationRefusals()
{
    Diagnostic diagnostic;

    {
        BvhDocument empty;
        assert(!motionBvh::ValidateBvhDocument(empty, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::ParseFailed);
    }
    {
        // A second parentless joint: a BVH document has exactly one root.
        BvhDocument document = MakeDocument();
        document.joints[1].parent = -1;
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::ParseFailed);
    }
    {
        // A child stored before its parent. Every consumer walks the joint
        // array in order and would read an uninitialised parent.
        BvhDocument document = MakeDocument();
        document.joints[1].parent = 1;
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::ParseFailed);
    }
    {
        // A channel offset that does not follow declaration order: the row
        // would still be the right width and every value would belong to the
        // wrong joint.
        BvhDocument document = MakeDocument();
        document.joints[1].channelOffset = 5;
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::ParseFailed);
        assert(diagnostic.subject == "Spine");
    }
    {
        BvhDocument document = MakeDocument();
        document.channelCount = 8;
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::ParseFailed);
    }
    {
        BvhDocument document = MakeDocument();
        document.values.pop_back();
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::ParseFailed);
    }
    {
        BvhDocument document = MakeDocument();
        document.values[4] = std::numeric_limits<float>::quiet_NaN();
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::NonFiniteValue);
    }
    {
        BvhDocument document = MakeDocument();
        document.joints[0].offset.y = std::numeric_limits<float>::infinity();
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::NonFiniteValue);
        assert(diagnostic.subject == "Hips");
    }
    {
        BvhDocument document = MakeDocument();
        document.joints[1].endSiteOffset->z =
            -std::numeric_limits<float>::infinity();
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::NonFiniteValue);
        assert(diagnostic.subject == "Spine");
    }
    {
        BvhDocument document = MakeDocument();
        document.frameTime = 0.0;
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::InvalidFrameTime);
    }
    {
        BvhDocument document = MakeDocument();
        document.frameTime = -0.5;
        assert(!motionBvh::ValidateBvhDocument(document, &diagnostic));
        assert(diagnostic.code == DiagnosticCode::InvalidFrameTime);
    }
}

// A single pose has no interval to describe, so a zero frame time there is a
// statement rather than an error. Two frames a zero-second interval apart
// cannot both be placed in time.
void
TestZeroFrameTimeBelowTwoFrames()
{
    BvhDocument document = MakeDocument();
    document.frameCount = 1;
    document.values.resize(9);
    document.frameTime = 0.0;
    assert(motionBvh::ValidateBvhDocument(document));

    document.frameCount = 0;
    document.values.clear();
    assert(motionBvh::ValidateBvhDocument(document));
}

} // namespace

int
main()
{
    TestChannelVocabulary();
    TestAccessors();
    TestDuplicateNames();
    TestCyclicParentTerminates();
    TestValidationRefusals();
    TestZeroFrameTimeBelowTwoFrames();
    std::printf("motionBvh document model: verified\n");
    return 0;
}
