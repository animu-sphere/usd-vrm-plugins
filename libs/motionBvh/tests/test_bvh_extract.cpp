// SPDX-License-Identifier: Apache-2.0
//
// Extraction: BVH shapes into neutral shapes.
//
// The document is built by hand rather than parsed in most of what follows,
// because the point of this layer is what it does with a *channel set* — and a
// channel set is the one thing a parser hands over that this file has to make a
// decision about. Building documents directly is also the only way to reach the
// shapes a real file rarely carries and BVH permits anyway: a joint with two
// rotation channels, one with the same axis twice, one with a partial position
// set, and one whose chain both ends and continues.
//
// The last case runs over parsed text instead, because the interesting property
// there is that the two ends of this library agree: what the parser produces,
// extraction accepts.
//
// No producer appears here. Every rig below is a plausible shape with ordinary
// names — a test written against a real product's joint set would be the first
// place this layer learned one.
#include "motionBvh/BvhExtract.h"

#include "motionBvh/BvhDocument.h"
#include "motionBvh/BvhParser.h"
#include "motionBvh/Diagnostics.h"

#include "motionSource/SourceAnimation.h"
#include "motionSource/SourceSkeleton.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using motionBvh::BvhChannel;
using motionBvh::BvhDocument;
using motionBvh::BvhExtractOptions;
using motionBvh::BvhJoint;
using motionBvh::Diagnostic;
using motionBvh::DiagnosticCode;
using motionBvh::ExtractBvhSource;
using motionSource::SourceAnimation;
using motionSource::SourceAngleUnit;
using motionSource::SourceEulerOrder;
using motionSource::SourceSkeleton;

// A document assembled the way the parser would assemble one: channel offsets
// follow declaration order across the whole hierarchy, and `values` is one row
// per frame. Written once here so that every case below differs from every
// other in the one thing it is about.
class DocumentBuilder
{
public:
    // Returns the new joint's index.
    std::size_t AddJoint(std::string name, int parent,
                         motionBvh::BvhVec3 offset,
                         std::vector<BvhChannel> channels)
    {
        BvhJoint joint;
        joint.name = std::move(name);
        joint.parent = parent;
        joint.offset = offset;
        joint.channels = std::move(channels);
        joint.channelOffset = _document.channelCount;
        _document.channelCount += joint.channels.size();
        _document.joints.push_back(std::move(joint));
        return _document.joints.size() - 1;
    }

    void SetTip(std::size_t jointIndex, motionBvh::BvhVec3 offset)
    {
        _document.joints[jointIndex].endSiteOffset = offset;
    }

    void AddFrame(const std::vector<float>& row)
    {
        assert(row.size() == _document.channelCount);
        _document.values.insert(_document.values.end(), row.begin(), row.end());
        ++_document.frameCount;
    }

    void SetFrameTime(double frameTime) { _document.frameTime = frameTime; }

    const BvhDocument& Document() const { return _document; }

private:
    BvhDocument _document;
};

motionBvh::BvhVec3
Vec(float x, float y, float z)
{
    motionBvh::BvhVec3 value;
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

bool
Extracts(const BvhDocument& document, SourceSkeleton* skeleton,
         SourceAnimation* animation, Diagnostic* diagnostic = nullptr)
{
    BvhExtractOptions options;
    options.sourceId = "fixture";
    return ExtractBvhSource(document, skeleton, animation, diagnostic, options);
}

// The six-channel root and a three-channel child: the shape every real export
// this pipeline has met so far is built out of.
DocumentBuilder
MinimalRig()
{
    DocumentBuilder builder;
    builder.AddJoint("Root", -1, Vec(0.0f, 90.0f, 0.0f),
                     {BvhChannel::Xposition, BvhChannel::Yposition,
                      BvhChannel::Zposition, BvhChannel::Zrotation,
                      BvhChannel::Xrotation, BvhChannel::Yrotation});
    const std::size_t child =
        builder.AddJoint("Child", 0, Vec(0.0f, 10.0f, 0.0f),
                         {BvhChannel::Zrotation, BvhChannel::Xrotation,
                          BvhChannel::Yrotation});
    builder.SetTip(child, Vec(0.0f, 5.0f, 0.0f));
    builder.SetFrameTime(0.5);
    builder.AddFrame({1.0f, 91.0f, 2.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f,
                      60.0f});
    builder.AddFrame({3.0f, 92.0f, 4.0f, 11.0f, 21.0f, 31.0f, 41.0f, 51.0f,
                      61.0f});
    return builder;
}

void
TestMinimalExtraction()
{
    const DocumentBuilder builder = MinimalRig();
    SourceSkeleton skeleton;
    SourceAnimation animation;
    Diagnostic diagnostic;
    assert(Extracts(builder.Document(), &skeleton, &animation, &diagnostic));

    assert(skeleton.joints.size() == 2);
    assert(skeleton.joints[0].name == "Root");
    assert(skeleton.joints[0].parent == -1);
    assert(skeleton.joints[0].restTranslation.y == 90.0f);
    // BVH states no rest rotation, so the option stays empty rather than
    // becoming an identity nobody wrote.
    assert(!skeleton.joints[0].restRotation.has_value());
    assert(!skeleton.joints[0].tipOffset.has_value());
    assert(skeleton.joints[1].parent == 0);
    assert(skeleton.joints[1].tipOffset.has_value());
    assert(skeleton.joints[1].tipOffset->y == 5.0f);

    assert(animation.tracks.size() == 2);
    assert(animation.frameCount == 2);
    assert(animation.frameTime == 0.5);
    assert(animation.startTime == 0.0);
    assert(animation.provenance.format == motionBvh::BvhFormatLabel());
    assert(animation.provenance.sourceId == "fixture");
    // A reader concludes neither, and the two fields are where a caller would
    // otherwise find a guess (BvhExtract.h).
    assert(animation.provenance.producer.empty());
    assert(animation.provenance.profileId.empty());

    // The root animated translation; the child declared none.
    assert(animation.tracks[0].HasTranslation());
    assert(animation.tracks[0].translations.size() == 2);
    assert(animation.tracks[0].translations[1].x == 3.0f);
    assert(animation.tracks[0].translations[1].y == 92.0f);
    assert(!animation.tracks[1].HasTranslation());

    // Both rotate, and both declared ZXY.
    assert(animation.tracks[0].HasRotation());
    assert(animation.tracks[0].eulerOrder == SourceEulerOrder::ZXY);
    assert(animation.tracks[0].angleUnit == SourceAngleUnit::Degrees);
    assert(animation.tracks[0].eulerAngles[0].first == 10.0f);
    assert(animation.tracks[0].eulerAngles[0].second == 20.0f);
    assert(animation.tracks[0].eulerAngles[0].third == 30.0f);
    assert(animation.tracks[1].eulerAngles[1].first == 41.0f);
    assert(animation.tracks[1].rotations.empty());
}

// The Euler order is read out of the declaration, never assumed -- the one
// statement a BVH file makes about its rotation order and the one this layer
// exists to carry.
void
TestEulerOrderFollowsDeclaration()
{
    struct Case
    {
        BvhChannel first;
        BvhChannel second;
        BvhChannel third;
        SourceEulerOrder order;
    };
    const Case cases[] = {
        {BvhChannel::Xrotation, BvhChannel::Yrotation, BvhChannel::Zrotation,
         SourceEulerOrder::XYZ},
        {BvhChannel::Xrotation, BvhChannel::Zrotation, BvhChannel::Yrotation,
         SourceEulerOrder::XZY},
        {BvhChannel::Yrotation, BvhChannel::Xrotation, BvhChannel::Zrotation,
         SourceEulerOrder::YXZ},
        {BvhChannel::Yrotation, BvhChannel::Zrotation, BvhChannel::Xrotation,
         SourceEulerOrder::YZX},
        {BvhChannel::Zrotation, BvhChannel::Xrotation, BvhChannel::Yrotation,
         SourceEulerOrder::ZXY},
        {BvhChannel::Zrotation, BvhChannel::Yrotation, BvhChannel::Xrotation,
         SourceEulerOrder::ZYX},
    };
    for (const Case& entry : cases) {
        DocumentBuilder builder;
        builder.AddJoint("Root", -1, Vec(0.0f, 0.0f, 0.0f),
                         {entry.first, entry.second, entry.third});
        builder.AddFrame({1.0f, 2.0f, 3.0f});
        SourceSkeleton skeleton;
        SourceAnimation animation;
        assert(Extracts(builder.Document(), &skeleton, &animation));
        assert(animation.tracks[0].eulerOrder == entry.order);
        // Stored in declaration order, so `first` is the first declared
        // channel's value whatever axis that channel names.
        assert(animation.tracks[0].eulerAngles[0].first == 1.0f);
        assert(animation.tracks[0].eulerAngles[0].third == 3.0f);
    }
}

// Position channels between rotation channels do not break the order: the
// *relative* order of the three is unambiguous and is what the file stated.
void
TestInterleavedChannelsKeepTheirOrder()
{
    DocumentBuilder builder;
    builder.AddJoint("Root", -1, Vec(0.0f, 0.0f, 0.0f),
                     {BvhChannel::Xposition, BvhChannel::Zrotation,
                      BvhChannel::Yposition, BvhChannel::Xrotation,
                      BvhChannel::Yrotation, BvhChannel::Zposition});
    builder.AddFrame({1.0f, 10.0f, 2.0f, 20.0f, 30.0f, 3.0f});
    SourceSkeleton skeleton;
    SourceAnimation animation;
    assert(Extracts(builder.Document(), &skeleton, &animation));
    assert(animation.tracks[0].eulerOrder == SourceEulerOrder::ZXY);
    assert(animation.tracks[0].eulerAngles[0].first == 10.0f);
    assert(animation.tracks[0].eulerAngles[0].second == 20.0f);
    assert(animation.tracks[0].eulerAngles[0].third == 30.0f);
    // The position columns are read by name, not by position in the row.
    assert(animation.tracks[0].translations[0].x == 1.0f);
    assert(animation.tracks[0].translations[0].y == 2.0f);
    assert(animation.tracks[0].translations[0].z == 3.0f);
}

// A component the file did not animate is the one the hierarchy stated. Zero
// would move the joint onto its parent, in a file nothing is wrong with.
void
TestPartialPositionFallsBackToTheOffset()
{
    DocumentBuilder builder;
    builder.AddJoint("Root", -1, Vec(4.0f, 5.0f, 6.0f),
                     {BvhChannel::Yposition, BvhChannel::Zrotation,
                      BvhChannel::Xrotation, BvhChannel::Yrotation});
    builder.AddFrame({50.0f, 0.0f, 0.0f, 0.0f});
    SourceSkeleton skeleton;
    SourceAnimation animation;
    assert(Extracts(builder.Document(), &skeleton, &animation));
    assert(animation.tracks[0].HasTranslation());
    assert(animation.tracks[0].translations[0].x == 4.0f);
    assert(animation.tracks[0].translations[0].y == 50.0f);
    assert(animation.tracks[0].translations[0].z == 6.0f);
}

// A joint the file animates nothing about is legal BVH and is an empty track,
// not a run of identity values.
void
TestStaticJointIsAnEmptyTrack()
{
    DocumentBuilder builder;
    builder.AddJoint("Root", -1, Vec(0.0f, 0.0f, 0.0f),
                     {BvhChannel::Zrotation, BvhChannel::Xrotation,
                      BvhChannel::Yrotation});
    builder.AddJoint("Prop", 0, Vec(1.0f, 2.0f, 3.0f), {});
    builder.AddFrame({1.0f, 2.0f, 3.0f});
    SourceSkeleton skeleton;
    SourceAnimation animation;
    assert(Extracts(builder.Document(), &skeleton, &animation));
    assert(animation.tracks.size() == 2);
    assert(animation.tracks[1].IsEmpty());
    // The track is still there: a track index means a joint index, and dropping
    // one would be the one thing tying these two values together going away.
    assert(skeleton.joints[1].restTranslation.x == 1.0f);
}

void
TestEmptyMotion()
{
    DocumentBuilder builder;
    builder.AddJoint("Root", -1, Vec(0.0f, 0.0f, 0.0f),
                     {BvhChannel::Zrotation, BvhChannel::Xrotation,
                      BvhChannel::Yrotation});
    SourceSkeleton skeleton;
    SourceAnimation animation;
    assert(Extracts(builder.Document(), &skeleton, &animation));
    assert(animation.frameCount == 0);
    assert(animation.frameTime == 0.0);
    assert(animation.tracks.size() == 1);
    assert(animation.tracks[0].IsEmpty());
}

bool
Refuses(const BvhDocument& document, DiagnosticCode code)
{
    SourceSkeleton skeleton;
    SourceAnimation animation;
    Diagnostic diagnostic;
    if (ExtractBvhSource(document, &skeleton, &animation, &diagnostic)) {
        return false;
    }
    // The parser's rule, for the parser's reason: a caller cannot tell which
    // half of a half-extracted document it got.
    if (!skeleton.joints.empty() || !animation.tracks.empty()) {
        return false;
    }
    return diagnostic.code == code;
}

void
TestRotationOrderRefusals()
{
    {
        // Two rotation channels form no order, whoever wrote the file. This is
        // the one code from the frozen set's semantic half that extraction may
        // raise, and no profile is involved in raising it.
        DocumentBuilder builder;
        builder.AddJoint("Root", -1, Vec(0.0f, 0.0f, 0.0f),
                         {BvhChannel::Xrotation, BvhChannel::Yrotation});
        builder.AddFrame({1.0f, 2.0f});
        assert(Refuses(builder.Document(),
                       DiagnosticCode::InvalidRotationOrder));
    }
    {
        // Four is not an order either.
        DocumentBuilder builder;
        builder.AddJoint("Root", -1, Vec(0.0f, 0.0f, 0.0f),
                         {BvhChannel::Xrotation, BvhChannel::Yrotation,
                          BvhChannel::Zrotation, BvhChannel::Xrotation});
        builder.AddFrame({1.0f, 2.0f, 3.0f, 4.0f});
        assert(Refuses(builder.Document(),
                       DiagnosticCode::InvalidRotationOrder));
    }
    {
        // Three channels naming two axes is not an order, and it is the case a
        // count check alone would pass.
        DocumentBuilder builder;
        builder.AddJoint("Root", -1, Vec(0.0f, 0.0f, 0.0f),
                         {BvhChannel::Xrotation, BvhChannel::Xrotation,
                          BvhChannel::Yrotation});
        builder.AddFrame({1.0f, 2.0f, 3.0f});
        assert(Refuses(builder.Document(),
                       DiagnosticCode::InvalidRotationOrder));
    }
}

void
TestRepeatedPositionChannelIsRefused()
{
    // Not in the frozen set, so `VRM_BVH_PARSE_FAILED` with a precise detail
    // rather than a new code.
    DocumentBuilder builder;
    builder.AddJoint("Root", -1, Vec(0.0f, 0.0f, 0.0f),
                     {BvhChannel::Xposition, BvhChannel::Xposition,
                      BvhChannel::Zrotation, BvhChannel::Xrotation,
                      BvhChannel::Yrotation});
    builder.AddFrame({1.0f, 2.0f, 3.0f, 4.0f, 5.0f});
    assert(Refuses(builder.Document(), DiagnosticCode::ParseFailed));
}

// BVH lets a joint carry both an `End Site` and a child; the value model above
// does not, because a chain that continues has not ended. Refused rather than
// silently dropped: dropping it would be this layer deciding what a shape it
// cannot represent meant.
void
TestTipOnABranchingJointIsRefused()
{
    DocumentBuilder builder;
    builder.AddJoint("Root", -1, Vec(0.0f, 0.0f, 0.0f),
                     {BvhChannel::Zrotation, BvhChannel::Xrotation,
                      BvhChannel::Yrotation});
    builder.AddJoint("Child", 0, Vec(0.0f, 1.0f, 0.0f), {});
    builder.SetTip(0, Vec(0.0f, 0.1f, 0.0f));
    builder.AddFrame({1.0f, 2.0f, 3.0f});
    assert(Refuses(builder.Document(), DiagnosticCode::ParseFailed));
}

void
TestInvalidDocumentIsRefused()
{
    // A document no parser produces: `channelCount` disagreeing with the
    // joints. Validation is what stands between it and the direct indexing
    // every line of extraction does.
    DocumentBuilder builder = MinimalRig();
    BvhDocument broken = builder.Document();
    broken.channelCount += 1;
    assert(Refuses(broken, DiagnosticCode::ParseFailed));
}

void
TestNoPlaceToPutTheResult()
{
    const DocumentBuilder builder = MinimalRig();
    SourceAnimation animation;
    Diagnostic diagnostic;
    assert(!ExtractBvhSource(builder.Document(), nullptr, &animation,
                             &diagnostic));
    assert(diagnostic.code == DiagnosticCode::ParseFailed);
    SourceSkeleton skeleton;
    assert(!ExtractBvhSource(builder.Document(), &skeleton, nullptr));
}

void
TestDeterminism()
{
    const DocumentBuilder builder = MinimalRig();
    SourceSkeleton firstSkeleton;
    SourceAnimation firstAnimation;
    SourceSkeleton secondSkeleton;
    SourceAnimation secondAnimation;
    assert(Extracts(builder.Document(), &firstSkeleton, &firstAnimation));
    assert(Extracts(builder.Document(), &secondSkeleton, &secondAnimation));
    // `operator==` and not `NearlyEqual`: this is recorded-value identity, and
    // an extraction that rounded differently on a second run would be a defect
    // rather than a tolerance question.
    assert(firstSkeleton == secondSkeleton);
    assert(firstAnimation == secondAnimation);
}

// The two ends of this library agree: what the parser produces, extraction
// accepts. Text rather than a hand-built document, because the property here is
// exactly that nothing sits between them.
void
TestParsedTextExtracts()
{
    constexpr std::string_view kText =
        "HIERARCHY\n"
        "ROOT Root\n"
        "{\n"
        "\tOFFSET 0.0 90.0 0.0\n"
        "\tCHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation "
        "Yrotation\n"
        "\tJOINT Child\n"
        "\t{\n"
        "\t\tOFFSET 0.0 10.0 0.0\n"
        "\t\tCHANNELS 3 Yrotation Xrotation Zrotation\n"
        "\t\tEnd Site\n"
        "\t\t{\n"
        "\t\t\tOFFSET 0.0 5.0 0.0\n"
        "\t\t}\n"
        "\t}\n"
        "}\n"
        "MOTION\n"
        "Frames: 2\n"
        "Frame Time: 0.0333333\n"
        "0.0 90.0 0.0 1.0 2.0 3.0 4.0 5.0 6.0\n"
        "0.1 90.5 0.2 7.0 8.0 9.0 10.0 11.0 12.0\n";
    BvhDocument document;
    assert(motionBvh::ParseBvhText(kText, &document));
    SourceSkeleton skeleton;
    SourceAnimation animation;
    assert(Extracts(document, &skeleton, &animation));
    assert(skeleton.joints.size() == 2);
    assert(animation.frameCount == 2);
    assert(animation.tracks[0].eulerOrder == SourceEulerOrder::ZXY);
    assert(animation.tracks[1].eulerOrder == SourceEulerOrder::YXZ);
    assert(animation.tracks[1].eulerAngles[1].first == 10.0f);
    const std::optional<double> rate = animation.FrameRate();
    assert(rate.has_value());
    assert(std::abs(*rate - 30.0) < 0.01);
}

} // namespace

int
main()
{
    TestMinimalExtraction();
    TestEulerOrderFollowsDeclaration();
    TestInterleavedChannelsKeepTheirOrder();
    TestPartialPositionFallsBackToTheOffset();
    TestStaticJointIsAnEmptyTrack();
    TestEmptyMotion();
    TestRotationOrderRefusals();
    TestRepeatedPositionChannelIsRefused();
    TestTipOnABranchingJointIsRefused();
    TestInvalidDocumentIsRefused();
    TestNoPlaceToPutTheResult();
    TestDeterminism();
    TestParsedTextExtracts();
    std::printf("motionBvh extract: verified\n");
    return 0;
}
