// SPDX-License-Identifier: Apache-2.0
//
// The skeleton map: VMC's names and VMC's axes into canonical humanoid
// semantics.
//
// The unit tests build `VmcMessage` values directly, for the reason the VMC
// layer's tests build `OscMessage` values directly: this layer's input *is* a
// decoded message, and going through the wire again would re-test two decoders
// and hide which of the three refused something.
//
// Corpus mode then runs all three layers over every committed capture, which is
// where the conversion meets recorded bytes instead of hand-written ones. Two
// of its claims are the ones a table of counts cannot make: a neutral pose
// stays a neutral pose through the basis change, and the arm-raise capture's
// rotations about Unity's -Z come out about the canonical +Z, at the angles the
// generator wrote.
#include "vrmAdapterVmc/SkeletonMap.h"

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/OscPacket.h"
#include "vrmAdapterVmc/PacketCapture.h"
#include "vrmAdapterVmc/VmcMessage.h"

#include "motionCore/Compare.h"
#include "motionCore/Humanoid.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using motion::HumanBone;
using motion::HumanBoneCount;
using vrmAdapterVmc::Diagnostic;
using vrmAdapterVmc::DiagnosticCode;
using vrmAdapterVmc::DiagnosticSeverity;
using vrmAdapterVmc::FindVmcHumanBone;
using vrmAdapterVmc::MapVmcBoneTransform;
using vrmAdapterVmc::MapVmcRootTransform;
using vrmAdapterVmc::ToCanonicalPosition;
using vrmAdapterVmc::ToCanonicalRotation;
using vrmAdapterVmc::VmcBoneSample;
using vrmAdapterVmc::VmcHumanBoneName;
using vrmAdapterVmc::VmcMessage;
using vrmAdapterVmc::VmcMessageKind;

constexpr double kPi = 3.14159265358979323846;

// A quaternion in VMC's wire order, (x, y, z, w).
std::array<float, 4>
UnityAbout(const std::array<float, 3>& axis, double degrees)
{
    const double half = degrees * kPi / 180.0 * 0.5;
    const float sine = static_cast<float>(std::sin(half));
    return {axis[0] * sine, axis[1] * sine, axis[2] * sine,
            static_cast<float>(std::cos(half))};
}

constexpr std::array<float, 4> kUnityIdentity = {0.0f, 0.0f, 0.0f, 1.0f};

VmcMessage
BoneMessage(std::string_view name, const std::array<float, 3>& position,
            const std::array<float, 4>& rotation)
{
    VmcMessage message;
    message.kind = VmcMessageKind::BoneTransform;
    message.name = name;
    message.transform.position = position;
    message.transform.rotation = rotation;
    return message;
}

VmcMessage
RootMessage(const std::array<float, 3>& position,
            const std::array<float, 4>& rotation)
{
    VmcMessage message = BoneMessage("root", position, rotation);
    message.kind = VmcMessageKind::RootTransform;
    return message;
}

bool
Near(float a, float b)
{
    return std::abs(a - b) <= 1e-6f;
}

// ---------------------------------------------------------------------------
// The vocabulary
// ---------------------------------------------------------------------------

void
TestTheVocabularyIsWholeAndRoundTrips()
{
    std::set<std::string_view> seen;
    for (std::size_t index = 0; index != HumanBoneCount; ++index) {
        const HumanBone bone = static_cast<HumanBone>(index);
        const std::string_view name = VmcHumanBoneName(bone);
        assert(!name.empty());
        // Unity's spelling is PascalCase throughout, which is the cheap
        // structural check that no VRM 1.0 name leaked into the table.
        assert(name[0] >= 'A' && name[0] <= 'Z');
        assert(seen.insert(name).second);
        assert(FindVmcHumanBone(name) == bone);
    }
    assert(seen.size() == HumanBoneCount);
    assert(VmcHumanBoneName(HumanBone::Count).empty());
    assert(!FindVmcHumanBone(""));
    assert(!FindVmcHumanBone("Hips "));
    assert(!FindVmcHumanBone("LastBone"));
}

void
TestTheThumbIsRenamedAndNotJustRecased()
{
    // The one place a name-based map is actively wrong rather than merely
    // incomplete: VRM 1.0 moved the thumb chain one joint down, so Unity's
    // "Proximal" is VRM 1.0's metacarpal and Unity's "Intermediate" is VRM
    // 1.0's proximal. Both directions, both hands.
    assert(FindVmcHumanBone("LeftThumbProximal")
           == HumanBone::LeftThumbMetacarpal);
    assert(FindVmcHumanBone("LeftThumbIntermediate")
           == HumanBone::LeftThumbProximal);
    assert(FindVmcHumanBone("LeftThumbDistal") == HumanBone::LeftThumbDistal);
    assert(FindVmcHumanBone("RightThumbProximal")
           == HumanBone::RightThumbMetacarpal);
    assert(FindVmcHumanBone("RightThumbIntermediate")
           == HumanBone::RightThumbProximal);
    assert(VmcHumanBoneName(HumanBone::LeftThumbMetacarpal)
           == "LeftThumbProximal");
    assert(VmcHumanBoneName(HumanBone::LeftThumbProximal)
           == "LeftThumbIntermediate");

    // Unity has no name for the joint VRM 1.0 calls a metacarpal, and VRM 1.0
    // has none for the one Unity calls intermediate. A map that fell back to
    // case folding would resolve both and shift the chain by a joint.
    assert(!FindVmcHumanBone("LeftThumbMetacarpal"));
    assert(!motion::FindHumanBone("leftThumbIntermediate"));

    // The fingers that were *not* renamed, so the thumb's shift is visibly the
    // exception rather than the rule.
    assert(FindVmcHumanBone("LeftIndexIntermediate")
           == HumanBone::LeftIndexIntermediate);
}

void
TestTheVrm10SpellingIsNotAccepted()
{
    // A sender writing lowerCamel is not writing VMC. Accepting both would
    // leave the corpus unable to say which spelling the protocol uses -- and
    // for the thumb the two vocabularies disagree about more than case, so
    // "accept either" is not even well defined.
    for (std::size_t index = 0; index != HumanBoneCount; ++index) {
        const HumanBone bone = static_cast<HumanBone>(index);
        assert(!FindVmcHumanBone(motion::HumanBoneName(bone)));
        assert(!motion::FindHumanBone(VmcHumanBoneName(bone)));
    }
}

// ---------------------------------------------------------------------------
// The basis change
// ---------------------------------------------------------------------------

void
TestPositionsReflectThroughX()
{
    const pxr::GfVec3f converted = ToCanonicalPosition({0.09f, 0.9f, -1.5f});
    assert(Near(converted[0], -0.09f));
    assert(Near(converted[1], 0.9f));
    assert(Near(converted[2], -1.5f));

    // A reflection is its own inverse, which is the whole reason this
    // conversion needs no second function for the other direction.
    const pxr::GfVec3f back =
        ToCanonicalPosition({converted[0], converted[1], converted[2]});
    assert(Near(back[0], 0.09f) && Near(back[1], 0.9f) && Near(back[2], -1.5f));
}

void
TestRotationsReflectThroughXAndReverseSense()
{
    const pxr::GfQuatf identity = ToCanonicalRotation(kUnityIdentity);
    assert(Near(identity.GetReal(), 1.0f));
    assert(Near(identity.GetImaginary().GetLength(), 0.0f));

    // About Unity's +Z by 60 degrees comes out about the canonical -Z by 60:
    // the axis survives the reflection unchanged and the sense reverses with
    // the handedness, which is one sign flip and not two.
    const std::array<float, 4> senderAboutZ =
        UnityAbout({0.0f, 0.0f, 1.0f}, 60.0);
    const pxr::GfQuatf aboutZ = ToCanonicalRotation(senderAboutZ);
    const float sixtyHalf = static_cast<float>(std::sin(kPi / 6.0));
    assert(Near(aboutZ.GetImaginary()[0], 0.0f));
    assert(Near(aboutZ.GetImaginary()[1], 0.0f));
    assert(Near(aboutZ.GetImaginary()[2], -sixtyHalf));
    assert(Near(motion::AngleBetween(aboutZ, pxr::GfQuatf(1.0f,
                                                          pxr::GfVec3f(0.0f))),
                static_cast<float>(kPi / 3.0)));

    // About +Y, the same flip.
    const pxr::GfQuatf aboutY =
        ToCanonicalRotation(UnityAbout({0.0f, 1.0f, 0.0f}, 60.0));
    assert(Near(aboutY.GetImaginary()[1], -sixtyHalf));

    // About +X, no flip at all: the axis reverses with the reflection and the
    // sense reverses with the handedness, and the two cancel.
    const pxr::GfQuatf aboutX =
        ToCanonicalRotation(UnityAbout({1.0f, 0.0f, 0.0f}, 60.0));
    assert(Near(aboutX.GetImaginary()[0], sixtyHalf));
    assert(Near(aboutX.GetImaginary()[1], 0.0f));
    assert(Near(aboutX.GetImaginary()[2], 0.0f));

    // And an involution here too: feeding the canonical components back
    // through, in the same wire order, returns what the sender sent.
    const std::array<float, 4> canonicalWire = {
        aboutZ.GetImaginary()[0], aboutZ.GetImaginary()[1],
        aboutZ.GetImaginary()[2], aboutZ.GetReal()};
    const pxr::GfQuatf back = ToCanonicalRotation(canonicalWire);
    assert(Near(back.GetReal(), senderAboutZ[3]));
    assert(Near(back.GetImaginary()[0], senderAboutZ[0]));
    assert(Near(back.GetImaginary()[1], senderAboutZ[1]));
    assert(Near(back.GetImaginary()[2], senderAboutZ[2]));
}

void
TestRotationsAreNormalised()
{
    // Senders emit un-normalised quaternions, and a retarget composing them
    // would skew a joint rather than rotate it. Scaling the input must not
    // change the orientation that comes out.
    const std::array<float, 4> unit = UnityAbout({0.0f, 0.0f, 1.0f}, 40.0);
    std::array<float, 4> scaled = unit;
    for (float& component : scaled) {
        component *= 7.5f;
    }
    const pxr::GfQuatf a = ToCanonicalRotation(unit);
    const pxr::GfQuatf b = ToCanonicalRotation(scaled);
    assert(Near(b.GetLength(), 1.0f));
    // Against the contract's own tolerance rather than a number invented here.
    // The two are not bit-identical -- scaling and dividing round each
    // component -- and how far apart float rounding leaves them is a platform's
    // business, not a claim this test should be making. What it claims is that
    // they are the same orientation by the measure `motionCore` defines.
    assert(motion::AngleBetween(a, b) < motion::MotionTolerance{}.angle);
}

// ---------------------------------------------------------------------------
// Mapping a message
// ---------------------------------------------------------------------------

void
TestABoneTransformBecomesCanonical()
{
    Diagnostic diagnostic;
    VmcBoneSample sample;
    assert(MapVmcBoneTransform(
        BoneMessage("LeftUpperArm", {0.12f, 0.0f, 0.0f},
                    UnityAbout({0.0f, 0.0f, 1.0f}, 30.0)),
        &sample, &diagnostic));
    assert(sample.bone == HumanBone::LeftUpperArm);
    assert(Near(sample.localPosition[0], -0.12f));
    assert(sample.localRotation.GetImaginary()[2] < 0.0f);
    assert(Near(sample.localRotation.GetLength(), 1.0f));
}

void
TestAnUnknownBoneIsUnsupportedNotMalformed()
{
    // The same distinction the message layer draws one level down: this adapter
    // cannot tell a sender's extension from a typo, so an unrecognised name
    // costs that bone and keeps the frame.
    Diagnostic diagnostic;
    VmcBoneSample sample;
    sample.bone = HumanBone::Head;
    assert(!MapVmcBoneTransform(
        BoneMessage("LeftPinkyProximal", {0.0f, 0.0f, 0.0f}, kUnityIdentity),
        &sample, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::UnsupportedMessage);
    assert(diagnostic.severity == DiagnosticSeverity::Info);
    assert(diagnostic.recoverable);
    assert(diagnostic.subject == "LeftPinkyProximal");
    // Untouched on failure, so a caller reusing one sample across a frame
    // cannot mistake the previous bone for this one.
    assert(sample.bone == HumanBone::Head);
}

void
TestAValueThatIsNotATransformIsRefused()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Diagnostic diagnostic;
    VmcBoneSample sample;

    assert(!MapVmcBoneTransform(
        BoneMessage("Hips", {0.0f, nan, 0.0f}, kUnityIdentity), &sample,
        &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(diagnostic.subject == "Hips");

    assert(!MapVmcBoneTransform(
        BoneMessage("Hips", {0.0f, 0.0f, 0.0f}, {0.0f, inf, 0.0f, 1.0f}),
        &sample, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);

    // A quaternion of zero length names no orientation, and the value that
    // would have to be invented to carry on -- identity -- is exactly the one a
    // reader could not tell from a real sample.
    assert(!MapVmcBoneTransform(
        BoneMessage("Hips", {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}),
        &sample, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(diagnostic.detail.find("no orientation") != std::string::npos);
}

void
TestTheGuardsRefuseRatherThanDereference()
{
    Diagnostic diagnostic;
    VmcBoneSample sample;
    motion::RootMotion root;

    // A message of the wrong kind, and a null destination: both are caller
    // bugs, and both are reported rather than trusted.
    assert(!MapVmcBoneTransform(
        RootMessage({0.0f, 0.0f, 0.0f}, kUnityIdentity), &sample, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(!MapVmcBoneTransform(
        BoneMessage("Hips", {0.0f, 0.0f, 0.0f}, kUnityIdentity), nullptr,
        &diagnostic));
    assert(!MapVmcRootTransform(
        BoneMessage("Hips", {0.0f, 0.0f, 0.0f}, kUnityIdentity), &root,
        &diagnostic));
    assert(!MapVmcRootTransform(
        RootMessage({0.0f, 0.0f, 0.0f}, kUnityIdentity), nullptr, &diagnostic));

    // And every one of them survives being given nowhere to report to.
    assert(!MapVmcBoneTransform(
        BoneMessage("Nonexistent", {0.0f, 0.0f, 0.0f}, kUnityIdentity),
        &sample));
    assert(!MapVmcRootTransform(
        BoneMessage("Hips", {0.0f, 0.0f, 0.0f}, kUnityIdentity), &root));
}

void
TestARootTransformCarriesNoVelocity()
{
    motion::RootMotion root;
    // Pre-filled with a velocity from a previous frame: the mapping defines the
    // whole value, so nothing stale survives it.
    root.linearVelocity = pxr::GfVec3f(1.0f, 2.0f, 3.0f);
    root.hasLinearVelocity = true;
    root.hasAngularVelocity = true;

    assert(MapVmcRootTransform(
        RootMessage({0.5f, 0.0f, -2.0f}, UnityAbout({0.0f, 1.0f, 0.0f}, 90.0)), &root));
    assert(root.hasPosition && root.hasOrientation);
    assert(Near(root.worldPosition[0], -0.5f));
    assert(Near(root.worldPosition[2], -2.0f));
    assert(!root.hasLinearVelocity && !root.hasAngularVelocity);
    assert(Near(root.linearVelocity.GetLength(), 0.0f));
    // Deriving one is `LiveCaptureSource`'s intake policy, not an adapter's.
    assert(root.worldOrientation.GetImaginary()[1] < 0.0f);
}

// ---------------------------------------------------------------------------
// Corpus
// ---------------------------------------------------------------------------

// Bone and root message counts per capture, taken from the layer below: they
// are `kinds[BoneTransform]` and `kinds[RootTransform]` of
// test_vmc_message.cpp's table, which are themselves derived from the
// generator's structure. Naming them again here states the claim this suite
// exists for -- *every* transform the VMC layer decoded maps, because no
// committed capture carries a name outside the Unity vocabulary.
struct Expected
{
    const char* file;
    std::size_t bones;
    std::size_t roots;
};

constexpr Expected kExpected[] = {
    {"arm-raise-30hz.vmcpackets", 105, 5},
    {"extended-forms.vmcpackets", 21, 1},
    {"malformed-forms.vmcpackets", 41, 2},
    {"malformed-packets.vmcpackets", 0, 0},
    {"mixed-traffic-30hz.vmcpackets", 63, 3},
    {"neutral-standing-30hz.vmcpackets", 110, 5},
    {"sender-restart-30hz.vmcpackets", 153, 8},
};

struct Mapped
{
    std::size_t bones = 0;
    std::size_t roots = 0;
    std::size_t unsupported = 0;
    std::size_t refused = 0;
    // Every bone the corpus carries, so the check below is not vacuous on a
    // capture whose offsets happen to be zero.
    std::size_t reflectedX = 0;
    bool everyRotationIsIdentity = true;
    bool everyRootIsAtTheOrigin = true;
    std::vector<pxr::GfQuatf> leftUpperArm;
};

int
CheckCorpus(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> captures;
    if (!std::filesystem::is_directory(directory)) {
        std::fprintf(stderr, "corpus directory not found: %s\n",
                     directory.string().c_str());
        return 1;
    }
    for (const std::filesystem::directory_entry& file :
         std::filesystem::directory_iterator(directory)) {
        if (file.is_regular_file()
            && file.path().extension() == ".vmcpackets") {
            captures.push_back(file.path());
        }
    }
    std::sort(captures.begin(), captures.end());
    if (captures.empty()) {
        std::fprintf(stderr, "no .vmcpackets fixtures in %s\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    std::set<std::string> covered;
    std::size_t reflectedAcrossCorpus = 0;

    for (const std::filesystem::path& path : captures) {
        const std::string name = path.filename().string();
        const Expected* entry = nullptr;
        for (const Expected& candidate : kExpected) {
            if (name == candidate.file) {
                entry = &candidate;
                break;
            }
        }
        if (!entry) {
            std::fprintf(stderr,
                         "%s: no expected mapping in this test -- add one, or "
                         "the capture is in the corpus and mapped by nobody\n",
                         name.c_str());
            ++failures;
            continue;
        }
        covered.insert(name);

        vrmAdapterVmc::PacketCapture capture;
        vrmAdapterVmc::PacketCaptureError error;
        if (!vrmAdapterVmc::ReadPacketCaptureFile(path.string(), &capture,
                                                  &error)) {
            std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), error.line,
                         error.message.c_str());
            ++failures;
            continue;
        }

        Mapped actual;
        for (const vrmAdapterVmc::RecordedDatagram& datagram :
             capture.datagrams) {
            vrmAdapterVmc::OscPacket osc;
            if (!vrmAdapterVmc::DecodeOscPacket(datagram.bytes, &osc)) {
                continue;
            }
            vrmAdapterVmc::VmcPacket vmc;
            vrmAdapterVmc::DecodeVmcPacket(osc, &vmc);

            for (const VmcMessage& message : vmc.messages) {
                Diagnostic diagnostic;
                if (message.kind == VmcMessageKind::BoneTransform) {
                    VmcBoneSample sample;
                    if (!MapVmcBoneTransform(message, &sample, &diagnostic)) {
                        if (diagnostic.code
                            == DiagnosticCode::UnsupportedMessage) {
                            ++actual.unsupported;
                        } else {
                            ++actual.refused;
                        }
                        continue;
                    }
                    ++actual.bones;
                    if (sample.localPosition[0]
                        == -message.transform.position[0]) {
                        if (message.transform.position[0] != 0.0f) {
                            ++actual.reflectedX;
                        }
                    } else {
                        std::fprintf(stderr,
                                     "%s: %s local X %f became %f\n",
                                     name.c_str(),
                                     std::string(message.name).c_str(),
                                     static_cast<double>(
                                         message.transform.position[0]),
                                     static_cast<double>(
                                         sample.localPosition[0]));
                        ++failures;
                    }
                    if (motion::AngleBetween(
                            sample.localRotation,
                            pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)))
                        > motion::MotionTolerance{}.angle) {
                        actual.everyRotationIsIdentity = false;
                    }
                    if (sample.bone == HumanBone::LeftUpperArm) {
                        actual.leftUpperArm.push_back(sample.localRotation);
                    }
                } else if (message.kind == VmcMessageKind::RootTransform) {
                    motion::RootMotion root;
                    if (!MapVmcRootTransform(message, &root, &diagnostic)) {
                        ++actual.refused;
                        continue;
                    }
                    ++actual.roots;
                    if (root.worldPosition.GetLength() != 0.0f) {
                        actual.everyRootIsAtTheOrigin = false;
                    }
                }
            }
        }
        reflectedAcrossCorpus += actual.reflectedX;

        if (actual.bones != entry->bones || actual.roots != entry->roots
            || actual.unsupported != 0 || actual.refused != 0) {
            std::fprintf(stderr,
                         "%s: %zu bone(s), %zu root(s), %zu unsupported, %zu "
                         "refused -- expected %zu, %zu, 0, 0\n",
                         name.c_str(), actual.bones, actual.roots,
                         actual.unsupported, actual.refused, entry->bones,
                         entry->roots);
            ++failures;
            continue;
        }

        // A neutral pose survives the basis change as a neutral pose. The
        // layer below already checks the recorded bytes are identity; this
        // checks the conversion did not turn them into something else.
        if (name == "neutral-standing-30hz.vmcpackets") {
            if (!actual.everyRotationIsIdentity
                || !actual.everyRootIsAtTheOrigin) {
                std::fprintf(stderr,
                             "%s: identity=%d origin=%d -- expected 1, 1\n",
                             name.c_str(),
                             actual.everyRotationIsIdentity ? 1 : 0,
                             actual.everyRootIsAtTheOrigin ? 1 : 0);
                ++failures;
            }
        }

        // The sign flip against recorded bytes rather than against a
        // hand-written quaternion. The generator raises the left arm in five
        // 15-degree steps, and the arm is at -X where Unity's axes put a left
        // side, so it writes them as *negative* rotations about +Z; every one
        // must come out about the canonical +Z, at the angle it was written
        // with. Both signs move the same arm the same way -- that is the point
        // of the reflection -- so a conversion that dropped it lands here.
        if (name == "arm-raise-30hz.vmcpackets") {
            const pxr::GfQuatf identity(1.0f, pxr::GfVec3f(0.0f));
            bool ok = actual.leftUpperArm.size() == 5;
            for (std::size_t index = 0;
                 ok && index != actual.leftUpperArm.size(); ++index) {
                const pxr::GfQuatf& rotation = actual.leftUpperArm[index];
                const float expected =
                    static_cast<float>(15.0 * index * kPi / 180.0);
                ok = rotation.GetImaginary()[0] == 0.0f
                    && rotation.GetImaginary()[1] == 0.0f
                    && rotation.GetImaginary()[2] >= 0.0f
                    && std::abs(motion::AngleBetween(rotation, identity)
                                - expected)
                        < 1e-4f;
            }
            if (!ok) {
                std::fprintf(stderr,
                             "%s: %zu leftUpperArm sample(s), not five "
                             "rotations about +Z at 0/15/30/45/60 degrees\n",
                             name.c_str(), actual.leftUpperArm.size());
                ++failures;
            }
        }

        std::printf("%s: %zu bone(s), %zu root(s) mapped\n", name.c_str(),
                    actual.bones, actual.roots);
    }

    for (const Expected& entry : kExpected) {
        if (covered.find(entry.file) == covered.end()) {
            std::fprintf(stderr, "%s: expected in this test, absent from %s\n",
                         entry.file, directory.string().c_str());
            ++failures;
        }
    }
    // Without one recorded bone off the X axis, the reflection check above
    // passes on a conversion that negates nothing.
    if (reflectedAcrossCorpus == 0) {
        std::fprintf(stderr,
                     "no capture carries a bone off the X axis; the reflection "
                     "check would pass on a conversion that copied\n");
        ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("VMC skeleton map: %zu capture(s) verified, %zu bone(s) "
                "reflected through X\n",
                captures.size(), reflectedAcrossCorpus);
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestTheVocabularyIsWholeAndRoundTrips();
    TestTheThumbIsRenamedAndNotJustRecased();
    TestTheVrm10SpellingIsNotAccepted();
    TestPositionsReflectThroughX();
    TestRotationsReflectThroughXAndReverseSense();
    TestRotationsAreNormalised();
    TestABoneTransformBecomesCanonical();
    TestAnUnknownBoneIsUnsupportedNotMalformed();
    TestAValueThatIsNotATransformIsRefused();
    TestTheGuardsRefuseRatherThanDereference();
    TestARootTransformCarriesNoVelocity();
    std::puts("vrmAdapterVmc skeleton map tests passed");
    return 0;
}
