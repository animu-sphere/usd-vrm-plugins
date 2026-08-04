// SPDX-License-Identifier: Apache-2.0
//
// Motion Phase D: the live-capture intake, the recorded trace format, and the
// replay driver. Every test here is deterministic by construction -- no wall
// clock is read anywhere in this file or in the code it exercises.
#include "motionRuntime/CaptureTrace.h"
#include "motionRuntime/LiveCaptureSource.h"
#include "motionRuntime/MotionSource.h"
#include "motionRuntime/Recorder.h"
#include "motionRuntime/ReplaySender.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

constexpr float kEpsilon = 1e-4f;

constexpr std::size_t kHips = static_cast<std::size_t>(motion::HumanBone::Hips);
constexpr std::size_t kSpine =
    static_cast<std::size_t>(motion::HumanBone::Spine);
constexpr std::size_t kLeftHand =
    static_cast<std::size_t>(motion::HumanBone::LeftHand);

bool
NearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= kEpsilon;
}

bool
NearlyEqual(const pxr::GfVec3f& a, const pxr::GfVec3f& b)
{
    return NearlyEqual(a[0], b[0]) && NearlyEqual(a[1], b[1])
        && NearlyEqual(a[2], b[2]);
}

bool
SameOrientation(const pxr::GfQuatf& a, const pxr::GfQuatf& b)
{
    const pxr::GfQuatf na = a.GetNormalized();
    pxr::GfQuatf nb = b.GetNormalized();
    if (pxr::GfDot(na, nb) < 0.0f) {
        nb = pxr::GfQuatf(-nb.GetReal(), -nb.GetImaginary());
    }
    return NearlyEqual(na.GetReal(), nb.GetReal())
        && NearlyEqual(na.GetImaginary(), nb.GetImaginary());
}

pxr::GfQuatf
RotationX(float degrees)
{
    const float radians = degrees * 3.14159265358979324f / 180.0f;
    return pxr::GfQuatf(std::cos(radians * 0.5f),
                        pxr::GfVec3f(std::sin(radians * 0.5f), 0.0f, 0.0f));
}

// A frame as an adapter would hand it over: already decoded, already in the
// canonical basis, stamped in the capture system's own clock.
motion::HumanoidPose
MakeFrame(double timestamp, float hipsDegrees, const pxr::GfVec3f& rootPosition)
{
    motion::HumanoidPose pose;
    pose.timestamp = timestamp;
    pose.localRotations[kHips] = RotationX(hipsDegrees);
    pose.validRotations.set(kHips);
    pose.localRotations[kSpine] = RotationX(hipsDegrees * 0.5f);
    pose.validRotations.set(kSpine);
    pose.root.worldPosition = rootPosition;
    pose.root.hasPosition = true;
    return pose;
}

void
SetConfidence(motion::HumanoidPose* pose, std::size_t bone, float score)
{
    if (!pose->confidence) {
        std::array<float, motion::HumanBoneCount> scores{};
        scores.fill(1.0f);
        pose->confidence = scores;
    }
    (*pose->confidence)[bone] = score;
}

// A short synthetic walk: the hips swing, the root advances at a constant
// 1 m/s, and both feet report contact state. 30 Hz.
motion::HumanoidAnimation
MakeTrace(std::size_t frames = 12)
{
    motion::HumanoidAnimation trace;
    trace.nominalFrameRate = 30.0;
    trace.source.kind = motion::MotionSourceKind::LiveCapture;
    trace.source.provider = "example.replay";
    trace.source.protocol = "replay";
    trace.source.sourceId = "walk-01";

    for (std::size_t index = 0; index < frames; ++index) {
        const double timestamp = static_cast<double>(index) / 30.0;
        motion::HumanoidPose pose = MakeFrame(
            timestamp, 10.0f * std::sin(static_cast<float>(index) * 0.5f),
            pxr::GfVec3f(0.0f, 0.9f, static_cast<float>(timestamp)));
        motion::ContactState contacts;
        contacts.leftFoot = (index % 2 == 0) ? motion::FootContact::InContact
                                             : motion::FootContact::NotInContact;
        contacts.rightFoot = (index % 2 == 0)
            ? motion::FootContact::NotInContact
            : motion::FootContact::InContact;
        pose.contacts = contacts;
        // A face channel on the same timeline, and one name that only some
        // frames report -- so the round trip below covers both an expression
        // that is always there and the absent/zero distinction.
        pose.expressions.Set("happy", 0.25f);
        if (index % 3 == 0) {
            pose.expressions.Set("blink", 1.0f);
        }
        pose.source = trace.source;
        trace.samples.push_back(pose);
    }
    trace.startTime = trace.samples.front().timestamp;
    trace.endTime = trace.samples.back().timestamp;
    return trace;
}

void
TestConfidenceGateResolvesThroughTheMissingBonePolicy()
{
    motion::LiveCaptureConfig config;
    config.confidenceFloor = 0.5f;
    config.missingBones = motion::MissingBonePolicy::HoldLast;
    motion::LiveCaptureSource source(config);

    motion::HumanoidPose first = MakeFrame(0.0, 30.0f, pxr::GfVec3f(0.0f));
    SetConfidence(&first, kHips, 0.9f);
    SetConfidence(&first, kSpine, 0.9f);
    assert(source.Push(first));

    // The spine drops below the floor: it must not be believed, and it must not
    // snap back to rest either -- the previous rotation is carried forward.
    motion::HumanoidPose second = MakeFrame(1.0, 60.0f, pxr::GfVec3f(0.0f));
    SetConfidence(&second, kHips, 0.9f);
    SetConfidence(&second, kSpine, 0.1f);
    assert(source.Push(second));

    const motion::PoseSampleResult result = source.Sample(1.0);
    assert(result.IsValid());
    assert(result.pose->validRotations.test(kSpine));
    assert(SameOrientation(result.pose->localRotations[kSpine],
                           RotationX(15.0f)));
    assert(SameOrientation(result.pose->localRotations[kHips],
                           RotationX(60.0f)));
    assert(source.GetStats().bonesGatedByConfidence == 1);
    assert(source.GetStats().bonesHeld >= 1);

    // A bone the rig never solves is never "observed", however many frames
    // arrive. This is the coverage diagnostic Phase D reports.
    assert(source.GetObservedBones().test(kHips));
    assert(source.GetObservedBones().test(kSpine));
    assert(!source.GetObservedBones().test(kLeftHand));

    // Under LeaveUnbound the same gated bone is reported missing instead, so
    // downstream can fall back to the target rig's rest pose.
    motion::LiveCaptureConfig unbound = config;
    unbound.missingBones = motion::MissingBonePolicy::LeaveUnbound;
    motion::LiveCaptureSource strict(unbound);
    assert(strict.Push(first));
    assert(strict.Push(second));
    const motion::PoseSampleResult strictResult = strict.Sample(1.0);
    assert(strictResult.IsValid());
    assert(!strictResult.pose->validRotations.test(kSpine));
}

void
TestConfidenceIsNotGatedWhenTheAdapterReportsNone()
{
    motion::LiveCaptureConfig config;
    config.confidenceFloor = 0.9f;
    motion::LiveCaptureSource source(config);

    // No confidence array at all: an adapter that cannot measure confidence
    // must not lose every bone for saying so.
    assert(source.Push(MakeFrame(0.0, 30.0f, pxr::GfVec3f(0.0f))));
    assert(source.GetStats().bonesGatedByConfidence == 0);
    assert(source.GetObservedBones().test(kHips));
}

void
TestRootMotionIntakeModes()
{
    motion::LiveCaptureConfig derive;
    derive.rootMotion = motion::RootMotionIntake::DeriveVelocity;
    motion::LiveCaptureSource source(derive);
    assert(source.Push(MakeFrame(0.0, 0.0f, pxr::GfVec3f(0.0f, 0.9f, 0.0f))));
    assert(source.Push(MakeFrame(0.5, 0.0f, pxr::GfVec3f(0.0f, 0.9f, 1.0f))));

    const motion::PoseSampleResult derived = source.Sample(0.5);
    assert(derived.IsValid());
    assert(derived.pose->root.hasLinearVelocity);
    assert(NearlyEqual(derived.pose->root.linearVelocity,
                       pxr::GfVec3f(0.0f, 0.0f, 2.0f)));
    assert(source.GetStats().rootVelocitiesDerived == 1);
    assert(source.GetStats().rootSamplesObserved == 2);

    // Passthrough records exactly what arrived and invents nothing.
    motion::LiveCaptureConfig passthrough;
    passthrough.rootMotion = motion::RootMotionIntake::Passthrough;
    motion::LiveCaptureSource plain(passthrough);
    assert(plain.Push(MakeFrame(0.0, 0.0f, pxr::GfVec3f(0.0f, 0.9f, 0.0f))));
    assert(plain.Push(MakeFrame(0.5, 0.0f, pxr::GfVec3f(0.0f, 0.9f, 1.0f))));
    assert(!plain.Sample(0.5).pose->root.hasLinearVelocity);
    assert(plain.GetStats().rootVelocitiesDerived == 0);

    // Ignore drops the root entirely; the joint hierarchy still arrives.
    motion::LiveCaptureConfig ignore;
    ignore.rootMotion = motion::RootMotionIntake::Ignore;
    motion::LiveCaptureSource rootless(ignore);
    assert(rootless.Push(MakeFrame(0.0, 45.0f, pxr::GfVec3f(0.0f, 0.9f, 0.0f))));
    const motion::PoseSampleResult ignored = rootless.Sample(0.0);
    assert(ignored.IsValid());
    assert(!ignored.pose->root.hasPosition);
    assert(ignored.pose->validRotations.test(kHips));
    assert(rootless.GetStats().rootSamplesObserved == 0);
}

void
TestOutOfOrderAndStaleFramesAreSeparated()
{
    motion::LiveCaptureConfig config;
    config.staleFrameSeconds = 0.5;
    motion::LiveCaptureSource source(config);

    assert(source.Push(MakeFrame(0.0, 0.0f, pxr::GfVec3f(0.0f))));
    assert(source.Push(MakeFrame(1.0, 0.0f, pxr::GfVec3f(0.0f))));

    // Slightly behind the head: the clock is jittering, and the caller has a
    // fault to look at.
    assert(!source.Push(MakeFrame(0.9, 0.0f, pxr::GfVec3f(0.0f))));
    assert(source.GetStats().framesRejectedOutOfOrder == 1);
    assert(source.GetStats().framesRejectedStale == 0);

    // Far behind the head: an ordinary straggler.
    assert(!source.Push(MakeFrame(0.2, 0.0f, pxr::GfVec3f(0.0f))));
    assert(source.GetStats().framesRejectedOutOfOrder == 1);
    assert(source.GetStats().framesRejectedStale == 1);

    // A frame carrying neither a bone nor a root is refused outright.
    motion::HumanoidPose empty;
    empty.timestamp = 2.0;
    assert(!source.Push(empty));
    assert(source.GetStats().framesRejectedEmpty == 1);
    assert(source.GetStats().framesAccepted == 2);
}

void
TestClockAlignmentAndSampleStatus()
{
    motion::LiveCaptureConfig config;
    config.maxExtrapolationSeconds = 0.1;
    motion::LiveCaptureSource source(config);
    assert(!source.AlignClock(0.0)); // nothing buffered yet

    for (std::size_t index = 0; index < 4; ++index) {
        const double timestamp = 100.0 + static_cast<double>(index) / 30.0;
        assert(source.Push(MakeFrame(timestamp, 0.0f,
                                     pxr::GfVec3f(0.0f, 0.9f,
                                                  static_cast<float>(index)))));
    }

    // The capture clock starts at 100 s; the consumer's starts at 0. Aligning
    // pins the newest frame to the current evaluation time.
    assert(source.AlignClock(0.0));
    const motion::PoseSampleResult head = source.Sample(0.0);
    assert(head.status == motion::PoseSampleStatus::Sampled);
    assert(NearlyEqual(static_cast<float>(head.lag), 0.0f));
    assert(NearlyEqual(head.pose->timestamp, 0.0));

    // Inside the buffered window: a real interpolation.
    assert(source.Sample(-1.0 / 60.0).status
           == motion::PoseSampleStatus::Sampled);

    // Before the window: the oldest pose is held, never extrapolated backwards.
    const motion::PoseSampleResult before = source.Sample(-10.0);
    assert(before.status == motion::PoseSampleStatus::Held);

    // Just past the head, within the lead budget: the root carries forward.
    const motion::PoseSampleResult ahead = source.Sample(0.05);
    assert(ahead.status == motion::PoseSampleStatus::Extrapolated);
    assert(ahead.lag > 0.0);
    assert(ahead.pose->root.worldPosition[2] > 3.0f);

    // Evaluation reports the consumer's clock, not the capture clock, however
    // the sample was resolved.
    assert(NearlyEqual(ahead.pose->timestamp, 0.05));

    // With extrapolation disabled the same request holds instead.
    motion::LiveCaptureConfig held = config;
    held.maxExtrapolationSeconds = 0.0;
    source.SetConfig(held);
    assert(source.Sample(0.05).status == motion::PoseSampleStatus::Held);

    assert(source.GetStats().peakLagSeconds > 0.0);

    // One counter per PoseSampleStatus and nothing else, so the four sum to the
    // number of Sample() calls made above -- five.
    const motion::LiveCaptureStats& stats = source.GetStats();
    assert(stats.samplesSampled + stats.samplesHeld + stats.samplesExtrapolated
               + stats.samplesUnavailable
           == 5);
}

void
TestAnEmptySourceIsUnavailableRatherThanWrong()
{
    motion::LiveCaptureSource source;
    const motion::PoseSampleResult result = source.Sample(0.0);
    assert(!result.IsValid());
    assert(result.status == motion::PoseSampleStatus::Unavailable);
    assert(source.GetStats().samplesUnavailable == 1);
    assert(!source.GetTimeRange(nullptr, nullptr));
}

void
TestCaptureTraceRoundTripsByteIdentically()
{
    const motion::HumanoidAnimation trace = MakeTrace();

    std::ostringstream first;
    assert(motion::WriteCaptureTrace(first, trace));
    const std::string text = first.str();

    motion::HumanoidAnimation parsed;
    motion::CaptureTraceError error;
    std::istringstream input(text);
    if (!motion::ReadCaptureTrace(input, &parsed, &error)) {
        std::fprintf(stderr, "trace parse failed at line %zu: %s\n", error.line,
                     error.message.c_str());
        assert(false);
    }

    assert(parsed.samples.size() == trace.samples.size());
    assert(parsed.source.provider == "example.replay");
    assert(parsed.source.protocol == "replay");
    assert(parsed.source.sourceId == "walk-01");
    assert(parsed.source.kind == motion::MotionSourceKind::LiveCapture);
    assert(NearlyEqual(static_cast<float>(parsed.nominalFrameRate), 30.0f));

    for (std::size_t index = 0; index < trace.samples.size(); ++index) {
        const motion::HumanoidPose& a = trace.samples[index];
        const motion::HumanoidPose& b = parsed.samples[index];
        assert(NearlyEqual(static_cast<float>(a.timestamp),
                           static_cast<float>(b.timestamp)));
        assert(a.validRotations == b.validRotations);
        assert(SameOrientation(a.localRotations[kHips], b.localRotations[kHips]));
        assert(a.root.hasPosition == b.root.hasPosition);
        assert(NearlyEqual(a.root.worldPosition, b.root.worldPosition));
        assert(b.contacts.has_value());
        assert(a.contacts->leftFoot == b.contacts->leftFoot);
        assert(a.contacts->rightFoot == b.contacts->rightFoot);
        // The expression channel survives with the same names, and a frame that
        // reported no `blink` still reports none -- rather than a zero.
        assert(a.expressions == b.expressions);
    }

    // The byte-level claim: a trace this writer produced survives a read and a
    // rewrite unchanged, which is what lets a fixture be compared rather than
    // merely parsed.
    std::ostringstream second;
    assert(motion::WriteCaptureTrace(second, parsed));
    assert(second.str() == text);
}

void
TestCaptureTraceRejectsMalformedInput()
{
    struct Case
    {
        const char* name;
        const char* text;
    };

    const Case cases[] = {
        {"no magic", "provider x\nt 0.0\nb hips 1 0 0 0\n"},
        {"wrong version", "!motion-capture-trace 99\nt 0.0\nb hips 1 0 0 0\n"},
        {"unknown bone",
         "!motion-capture-trace 1\nt 0.0\nb elbow 1 0 0 0\n"},
        {"duplicate bone",
         "!motion-capture-trace 1\nt 0.0\nb hips 1 0 0 0\nb hips 1 0 0 0\n"},
        {"non-increasing time",
         "!motion-capture-trace 1\nt 1.0\nb hips 1 0 0 0\nt 0.5\n"
         "b hips 1 0 0 0\n"},
        {"zero-length rotation",
         "!motion-capture-trace 1\nt 0.0\nb hips 0 0 0 0\n"},
        {"confidence out of range",
         "!motion-capture-trace 1\nt 0.0\nb hips 1 0 0 0 1.5\n"},
        {"unknown header key",
         "!motion-capture-trace 1\nnonsense 3\nt 0.0\nb hips 1 0 0 0\n"},
        {"no frames", "!motion-capture-trace 1\nprovider x\n"},
        // A rotation that is not one. Accepting it would hand UsdSkel a joint
        // basis that quietly skews or scales the limb.
        {"non-unit rotation",
         "!motion-capture-trace 1\nt 0.0\nb hips 2 0 0 0\n"},
        // 0.5 0.5 0.5 0.5 would be a *unit* quaternion (|q|^2 = 4 * 0.25); the
        // 0.9 is what makes this one 1.56 long.
        {"non-unit root rotation",
         "!motion-capture-trace 1\nt 0.0\nb hips 1 0 0 0\n"
         "root rot 0.5 0.5 0.5 0.9\n"},
        // Text left over after a line's operands. Every one of these used to
        // parse: the tail was simply dropped, so a typo read as good data.
        {"trailing text after a rotation",
         "!motion-capture-trace 1\nt 0.0\nb hips 1 0 0 0 grbage\n"},
        {"trailing text after a confidence",
         "!motion-capture-trace 1\nt 0.0\nb hips 1 0 0 0 0.9 extra\n"},
        {"trailing text after a timestamp",
         "!motion-capture-trace 1\nt 0.0 later\nb hips 1 0 0 0\n"},
        {"trailing text after a root vector",
         "!motion-capture-trace 1\nt 0.0\nb hips 1 0 0 0\n"
         "root pos 0 0 0 0\n"},
        {"trailing text after a contact pair",
         "!motion-capture-trace 1\nt 0.0\nb hips 1 0 0 0\n"
         "contacts contact free contact\n"},
        {"trailing text after a header value",
         "!motion-capture-trace 1\nframeRate 30 60\nt 0.0\nb hips 1 0 0 0\n"},
        // A version is a claim about content. Reading this leniently would let
        // a file be one format by its header and another by its body, and the
        // header is what a reader dispatches on.
        {"expression in a format 1 trace",
         "!motion-capture-trace 1\nt 0.0\nb hips 1 0 0 0\ne happy 0.5\n"},
        // The same defect a repeated bone is: two values for one channel in one
        // frame, with no rule saying which wins.
        {"duplicate expression",
         "!motion-capture-trace 2\nt 0.0\nb hips 1 0 0 0\ne happy 0.5\n"
         "e happy 0.25\n"},
        {"expression with no weight",
         "!motion-capture-trace 2\nt 0.0\nb hips 1 0 0 0\ne happy\n"},
        {"trailing text after an expression weight",
         "!motion-capture-trace 2\nt 0.0\nb hips 1 0 0 0\ne happy 0.5 extra\n"},
        {"non-finite expression weight",
         "!motion-capture-trace 2\nt 0.0\nb hips 1 0 0 0\ne happy nan\n"},
    };

    for (const Case& testCase : cases) {
        motion::HumanoidAnimation parsed;
        motion::CaptureTraceError error;
        std::istringstream input(testCase.text);
        const bool ok = motion::ReadCaptureTrace(input, &parsed, &error);
        if (ok) {
            std::fprintf(stderr, "malformed trace was accepted: %s\n",
                         testCase.name);
            assert(false);
        }
        // A rejection has to say what and where, or a corpus fixture cannot be
        // fixed without a debugger.
        assert(!error.message.empty());
        assert(parsed.samples.empty());
    }
}

void
TestCaptureTraceVersioningAndUnwritableNames()
{
    // A recording that stops being readable because the format moved on is a
    // recording lost, so format 1 still parses -- it simply carries no `e`.
    motion::HumanoidAnimation old;
    motion::CaptureTraceError error;
    std::istringstream input(
        "!motion-capture-trace 1\nprovider x\nt 0.0\nb hips 1 0 0 0\n");
    assert(motion::ReadCaptureTrace(input, &old, &error));
    assert(old.samples.size() == 1);
    assert(old.samples.front().expressions.IsEmpty());

    // The writer only ever emits the current version, so a format 1 file read
    // back out is a format 2 file. That is why the committed corpus was
    // regenerated rather than left alone: byte-identity is a property of traces
    // this writer produced, not of every trace it can read.
    std::ostringstream rewritten;
    assert(motion::WriteCaptureTrace(rewritten, old));
    assert(rewritten.str().rfind("!motion-capture-trace 2", 0) == 0);

    // The one value this format cannot spell. A name is written as a single
    // token, so whitespace in one would read back as a different animation --
    // or as none. The refusal happens before the first byte, so a caller that
    // is refused still has an untouched stream rather than a plausible file
    // holding the frames that happened to come first.
    for (const char* unwritable : {"pursed lips", "", "tab\there"}) {
        motion::HumanoidAnimation broken = MakeTrace(3);
        broken.samples[1].expressions.Set(unwritable, 0.5f);
        std::ostringstream refused;
        assert(!motion::WriteCaptureTrace(refused, broken));
        assert(refused.str().empty());
    }
}

void
TestCaptureTraceCarriesProvenanceWithSpaces()
{
    // The bug this exists for: every `sourceId` in this repository was a single
    // token until a VMC session supplied one, and a sender's is a model title
    // somebody typed. The writer emitted it verbatim and the reader then
    // refused the file the writer had just produced -- a round trip that failed
    // on a value neither side had any reason to reject.
    motion::HumanoidAnimation animation = MakeTrace(3);
    animation.source.provider = "Studio Example, Inc.";
    animation.source.protocol = "vmc";
    animation.source.sourceId = "Example Avatar";

    std::ostringstream first;
    assert(motion::WriteCaptureTrace(first, animation));

    motion::HumanoidAnimation parsed;
    motion::CaptureTraceError error;
    std::istringstream input(first.str());
    if (!motion::ReadCaptureTrace(input, &parsed, &error)) {
        std::fprintf(stderr, "provenance round trip failed at line %zu: %s\n",
                     error.line, error.message.c_str());
        assert(false);
    }
    assert(parsed.source.provider == "Studio Example, Inc.");
    assert(parsed.source.sourceId == "Example Avatar");

    // And it still diffs: a second write of what was read is byte-identical,
    // which is the property the whole format exists for.
    std::ostringstream second;
    assert(motion::WriteCaptureTrace(second, parsed));
    assert(first.str() == second.str());

    // What no line-oriented format can carry, refused before the first byte for
    // the same reason an expression name is. Padding is in this list because
    // trimming it on the way back in would be a silent edit to somebody's
    // recorded provenance, which is worse than a refusal for being invisible.
    for (const char* unwritable : {"two\nlines", " leading", "trailing ",
                                   "carriage\rreturn"}) {
        motion::HumanoidAnimation broken = MakeTrace(3);
        broken.source.sourceId = unwritable;
        std::ostringstream refused;
        assert(!motion::WriteCaptureTrace(refused, broken));
        assert(refused.str().empty());
    }

    // A header key with nothing after it is still an error: the value is
    // rest-of-line, and rest-of-line can be empty.
    motion::HumanoidAnimation empty;
    std::istringstream blank("!motion-capture-trace 2\nsourceId   \n"
                             "t 0.0\nb hips 1 0 0 0\n");
    assert(!motion::ReadCaptureTrace(blank, &empty, &error));
    assert(error.message.find("sourceId") != std::string::npos);
}

void
TestReplayIsDeterministicAndFeedsTheSameInterface()
{
    const motion::HumanoidAnimation trace = MakeTrace(24);

    // A tick schedule that deliberately runs the consumer slightly ahead of
    // delivery, so holds and extrapolations actually occur.
    const auto replay = [&trace](motion::HumanoidAnimation* recorded,
                                 motion::RecordReport* report) {
        motion::LiveCaptureConfig config;
        config.maxExtrapolationSeconds = 0.05;
        motion::LiveCaptureSource source(config);
        motion::MotionSourceMetadata metadata;
        metadata.provider = "example.replay";
        metadata.protocol = "replay";
        metadata.sourceId = "walk-01";
        source.SetSourceMetadata(metadata);

        motion::ReplaySender sender(trace, &source);
        motion::CaptureRecorder recorder(60.0);

        for (std::size_t tick = 0; tick < 40; ++tick) {
            const double now = static_cast<double>(tick) / 60.0;
            // Frames "arrive" a fixed 20 ms behind the tick they belong to.
            sender.Advance(now - 0.02);
            if (source.IsEmpty()) {
                continue;
            }
            recorder.Record(source.Sample(now));
        }
        *report = recorder.GetReport();
        *recorded = recorder.Take();
    };

    motion::HumanoidAnimation firstRun;
    motion::RecordReport firstReport;
    replay(&firstRun, &firstReport);

    motion::HumanoidAnimation secondRun;
    motion::RecordReport secondReport;
    replay(&secondRun, &secondReport);

    assert(!firstRun.samples.empty());
    assert(firstRun.samples.size() == secondRun.samples.size());
    assert(firstReport.ticks == secondReport.ticks);
    assert(firstReport.sampled == secondReport.sampled);
    assert(firstReport.held == secondReport.held);
    assert(firstReport.extrapolated == secondReport.extrapolated);

    for (std::size_t index = 0; index < firstRun.samples.size(); ++index) {
        const motion::HumanoidPose& a = firstRun.samples[index];
        const motion::HumanoidPose& b = secondRun.samples[index];
        assert(a.timestamp == b.timestamp);
        assert(a.validRotations == b.validRotations);
        assert(a.localRotations[kHips] == b.localRotations[kHips]);
        assert(a.root.worldPosition == b.root.worldPosition);
    }

    // Provenance survives the whole path: intake stamps it, the recorder
    // promotes it onto the clip.
    assert(firstRun.source.kind == motion::MotionSourceKind::LiveCapture);
    assert(firstRun.source.provider == "example.replay");

    // The recorded session is a clip like any other, and a ClipSource over it
    // answers the same interface the live source did. From here nothing
    // downstream can tell the two apart -- which is the point of Phase D.
    motion::ClipSource clip(firstRun);
    motion::IMotionSource& asInterface = clip;
    const motion::PoseSampleResult sampled =
        asInterface.Sample(firstRun.samples.front().timestamp);
    assert(sampled.IsValid());
    assert(sampled.status == motion::PoseSampleStatus::Sampled);
    assert(SameOrientation(sampled.pose->localRotations[kHips],
                           firstRun.samples.front().localRotations[kHips]));
    assert(clip.GetSourceMetadata().kind
           == motion::MotionSourceKind::LiveCapture);
}

void
TestAQuantisedTraceStillSamplesOnItsOwnTicks()
{
    // A trace stores six decimals, so 1/30 s reads back as 0.033333 and 2/30 s
    // as 0.066667 -- one just below the exact instant, one just above. Ticking
    // at the same rate in exact arithmetic therefore puts half the frames on
    // the wrong side of the comparison, and each miss costs a whole frame
    // interval of extrapolation.
    //
    // This is what a clean session replayed at its own rate must look like:
    // sampled, not extrapolated. It regressed exactly once, and the only symptom
    // was a statistic -- the output still animated, just from extrapolated
    // poses.
    motion::HumanoidAnimation trace;
    trace.nominalFrameRate = 30.0;
    for (std::size_t index = 0; index < 30; ++index) {
        // Quantise the way the trace writer does.
        const double exact = static_cast<double>(index) / 30.0;
        const double stored = std::round(exact * 1e6) / 1e6;
        trace.samples.push_back(
            MakeFrame(stored, static_cast<float>(index),
                      pxr::GfVec3f(0.0f, 0.9f, static_cast<float>(index))));
    }
    trace.startTime = trace.samples.front().timestamp;
    trace.endTime = trace.samples.back().timestamp;

    motion::LiveCaptureSource source;
    motion::ReplaySender sender(trace, &source);
    motion::CaptureRecorder recorder(30.0);

    for (std::size_t tick = 0; tick < trace.samples.size(); ++tick) {
        const double now = static_cast<double>(tick) / 30.0;
        sender.Advance(now);
        if (source.IsEmpty()) {
            continue;
        }
        recorder.Record(source.Sample(now));
    }

    const motion::RecordReport report = recorder.GetReport();
    assert(report.ticks == trace.samples.size());
    assert(report.sampled == trace.samples.size());
    assert(report.extrapolated == 0);
    assert(report.held == 0);
}

void
TestRecorderCountsWhatItCouldNotSample()
{
    motion::LiveCaptureSource source;
    motion::CaptureRecorder recorder(30.0);

    // Three ticks before any frame arrives: recorded as unavailable, and no
    // pose is invented to paper over them.
    for (std::size_t tick = 0; tick < 3; ++tick) {
        assert(!recorder.Record(source.Sample(static_cast<double>(tick))));
    }
    assert(recorder.GetReport().unavailable == 3);
    assert(recorder.GetFrameCount() == 0);

    assert(source.Push(MakeFrame(3.0, 0.0f, pxr::GfVec3f(0.0f))));
    assert(recorder.Record(source.Sample(3.0)));
    assert(recorder.Record(source.Sample(4.0)));
    assert(recorder.GetReport().held == 1);

    // A tick that does not advance the clock is refused, not appended twice.
    assert(!recorder.Record(source.Sample(4.0)));
    assert(recorder.GetReport().rejected == 1);

    const motion::HumanoidAnimation clip = recorder.Take();
    assert(clip.samples.size() == 2);
    assert(NearlyEqual(static_cast<float>(clip.startTime), 3.0f));
    assert(NearlyEqual(static_cast<float>(clip.endTime), 4.0f));
    assert(NearlyEqual(static_cast<float>(clip.nominalFrameRate), 30.0f));
}

void
TestSmoothingIsOptionalAndDoesNotInventBones()
{
    motion::LiveCaptureConfig config;
    config.smoothingCutoffHz = 4.0f;
    config.missingBones = motion::MissingBonePolicy::LeaveUnbound;
    motion::LiveCaptureSource source(config);

    motion::HumanoidPose first = MakeFrame(0.0, 0.0f, pxr::GfVec3f(0.0f));
    assert(source.Push(first));

    // A frame carrying only the hips: smoothing must not resurrect the spine.
    motion::HumanoidPose second;
    second.timestamp = 0.1;
    second.localRotations[kHips] = RotationX(90.0f);
    second.validRotations.set(kHips);
    assert(source.Push(second));

    const motion::PoseSampleResult result = source.Sample(0.1);
    assert(result.IsValid());
    assert(!result.pose->validRotations.test(kSpine));
    // Smoothed, so the hips lag the raw 90 degrees rather than snapping to it.
    assert(!SameOrientation(result.pose->localRotations[kHips],
                            RotationX(90.0f)));
}

// Corpus mode. Every committed trace must parse, and re-emitting it must
// reproduce the committed bytes exactly -- so a fixture cannot drift from the
// writer without a red test, and a golden comparison downstream stays valid.
int
CheckCorpus(const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory)) {
        std::fprintf(stderr, "corpus directory not found: %s\n",
                     directory.string().c_str());
        return 1;
    }

    std::vector<std::filesystem::path> traces;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".trace") {
            traces.push_back(entry.path());
        }
    }
    std::sort(traces.begin(), traces.end());

    if (traces.empty()) {
        std::fprintf(stderr, "no .trace fixtures in %s\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    for (const std::filesystem::path& path : traces) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "%s: could not open\n",
                         path.filename().string().c_str());
            ++failures;
            continue;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        const std::string original = buffer.str();

        motion::HumanoidAnimation parsed;
        motion::CaptureTraceError error;
        std::istringstream input(original);
        if (!motion::ReadCaptureTrace(input, &parsed, &error)) {
            std::fprintf(stderr, "%s:%zu: %s\n",
                         path.filename().string().c_str(), error.line,
                         error.message.c_str());
            ++failures;
            continue;
        }

        std::ostringstream rewritten;
        if (!motion::WriteCaptureTrace(rewritten, parsed)
            || rewritten.str() != original) {
            std::fprintf(stderr, "%s: does not round trip byte-identically\n",
                         path.filename().string().c_str());
            ++failures;
            continue;
        }

        // A trace nothing can be driven from is not a fixture.
        motion::LiveCaptureSource source;
        motion::ReplaySender sender(parsed, &source);
        if (sender.Flush() != parsed.samples.size()) {
            std::fprintf(stderr, "%s: replay did not accept every frame\n",
                         path.filename().string().c_str());
            ++failures;
            continue;
        }

        std::printf("%s: %zu frames, %.2f Hz, round trip ok\n",
                    path.filename().string().c_str(), parsed.samples.size(),
                    parsed.nominalFrameRate);
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus trace(s) failed\n", failures);
        return 1;
    }
    std::printf("motion corpus: %zu trace(s) verified\n", traces.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestConfidenceGateResolvesThroughTheMissingBonePolicy();
    TestConfidenceIsNotGatedWhenTheAdapterReportsNone();
    TestRootMotionIntakeModes();
    TestOutOfOrderAndStaleFramesAreSeparated();
    TestClockAlignmentAndSampleStatus();
    TestAnEmptySourceIsUnavailableRatherThanWrong();
    TestCaptureTraceRoundTripsByteIdentically();
    TestCaptureTraceRejectsMalformedInput();
    TestCaptureTraceVersioningAndUnwritableNames();
    TestCaptureTraceCarriesProvenanceWithSpaces();
    TestReplayIsDeterministicAndFeedsTheSameInterface();
    TestAQuantisedTraceStillSamplesOnItsOwnTicks();
    TestRecorderCountsWhatItCouldNotSample();
    TestSmoothingIsOptionalAndDoesNotInventBones();
    std::puts("motionRuntime live-capture tests passed");
    return 0;
}
