// SPDX-License-Identifier: Apache-2.0
//
// The decoded-packet builders the two upper-layer tests share.
//
// `test_frame_assembler.cpp` and `test_live_source.cpp` both take
// `MotionPacket` values rather than bytes — that is the line those layers draw,
// and a test that went through the wire format would be re-testing the decoder
// (see either file's header). So both need the same rig, the same rest-pose
// frame, and the same way to damage one, and they held byte-identical copies of
// all of it until this header existed.
//
// **The rest-offset table stays a stated table, and that argument is unchanged
// by moving it here.** It is duplicated from `tools/generate_packets.py` on
// purpose: what a pose is compared against has to be written down where a
// reviewer reads it, rather than read out of the capture the assertion is about,
// or the fixture is only agreeing with itself. Stating it once for the adapter's
// tests rather than once per file does not weaken that — the copy that matters
// is the one across the generator boundary, and it is still there.
//
// What is deliberately **not** here is anything that decides. Every tolerance,
// every expected count and every assertion lives in the file making the claim,
// because two tests that shared an expected number would agree with each other
// rather than with the layer. `test_skeleton_map.cpp` keeps its own builders for
// the same reason: it needs `MotionSkeleton` and `MotionFrame` directly rather
// than packets, and folding the two shapes together would give every caller a
// parameter it does not want.
#pragma once

#include "vrmAdapterMocopi/MotionPacket.h"
#include "vrmAdapterMocopi/SkeletonMap.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vrmAdapterMocopiTests
{

// The corpus generator's invented proportions, in bone order — round numbers
// that are nobody's body.
inline constexpr float kRestOffsets[vrmAdapterMocopi::MeasuredBoneCount][3] = {
    {0.0f, 0.90f, 0.0f},     {0.0f, 0.06f, 0.0f},   {0.0f, 0.06f, 0.0f},
    {0.0f, 0.06f, 0.0f},     {0.0f, 0.06f, 0.0f},   {0.0f, 0.06f, 0.0f},
    {0.0f, 0.06f, 0.0f},     {0.0f, 0.10f, 0.0f},   {0.0f, 0.05f, 0.0f},
    {0.0f, 0.05f, 0.0f},     {0.0f, 0.05f, 0.0f},   {0.02f, -0.08f, 0.08f},
    {0.14f, 0.0f, 0.0f},     {0.30f, 0.0f, 0.0f},   {0.25f, 0.0f, 0.0f},
    {-0.02f, -0.08f, 0.08f}, {-0.14f, 0.0f, 0.0f},  {-0.30f, 0.0f, 0.0f},
    {-0.25f, 0.0f, 0.0f},    {0.09f, -0.05f, 0.0f}, {0.0f, -0.40f, 0.0f},
    {0.0f, -0.42f, 0.0f},    {0.0f, -0.10f, 0.13f}, {-0.09f, -0.05f, 0.0f},
    {0.0f, -0.40f, 0.0f},    {0.0f, -0.42f, 0.0f},  {0.0f, -0.10f, 0.13f},
};

// The canonical bones the measured rig carries: 27 joints, five of which are on
// the path and carry none.
inline constexpr std::size_t kCanonicalBoneCount = 22;

// The measured sender rate, in all five sessions.
inline constexpr double kFrameRate = 60.0;

// The corpus generator's fixed instant, 2026-08-12T00:00:00Z, so a drift
// assertion is made against the same absolute clock the captures carry.
inline constexpr double kEpoch = 1786492800.0;

// Scalar-last, as this protocol serialises a rotation.
inline std::array<float, 4>
WireIdentity()
{
    return {{0.0f, 0.0f, 0.0f, 1.0f}};
}

inline std::array<float, 3>
RestOffset(std::size_t jointId)
{
    return {{kRestOffsets[jointId][0], kRestOffsets[jointId][1],
             kRestOffsets[jointId][2]}};
}

inline vrmAdapterMocopi::MotionPacket
SkeletonPacket()
{
    vrmAdapterMocopi::MotionSkeleton skeleton;
    for (std::size_t jointId = 0; jointId < vrmAdapterMocopi::MeasuredBoneCount;
         ++jointId) {
        vrmAdapterMocopi::BoneDefinition joint;
        joint.boneId = static_cast<std::uint16_t>(jointId);
        joint.parentBoneId = vrmAdapterMocopi::MeasuredParentColumn[jointId];
        joint.restTransform.rotation = WireIdentity();
        joint.restTransform.translation = RestOffset(jointId);
        skeleton.bones.push_back(joint);
    }
    vrmAdapterMocopi::MotionPacket packet;
    packet.kind = vrmAdapterMocopi::MotionPacketKind::Skeleton;
    packet.skeleton = std::move(skeleton);
    return packet;
}

// A frame that restates the rest pose, which is what every measured frame does
// for every joint but the root.
inline vrmAdapterMocopi::MotionPacket
FramePacket(std::uint32_t frameNumber, double streamSeconds,
            double senderUnixSeconds)
{
    vrmAdapterMocopi::MotionFrame frame;
    frame.frameNumber = frameNumber;
    frame.streamSeconds = static_cast<float>(streamSeconds);
    frame.senderUnixSeconds = senderUnixSeconds;
    for (std::size_t jointId = 0; jointId < vrmAdapterMocopi::MeasuredBoneCount;
         ++jointId) {
        vrmAdapterMocopi::BoneFrame bone;
        bone.boneId = static_cast<std::uint16_t>(jointId);
        bone.transform.rotation = WireIdentity();
        bone.transform.translation = RestOffset(jointId);
        frame.bones.push_back(bone);
    }
    vrmAdapterMocopi::MotionPacket packet;
    packet.kind = vrmAdapterMocopi::MotionPacketKind::Frame;
    packet.frame = std::move(frame);
    return packet;
}

// A frame whose two clocks agree, which is what the device sends *within a
// session*. Not for the first frame after a restart: the stream clock begins
// again there and the wall clock does not, so a caller building one states
// `uttm` itself through `FramePacket`.
inline vrmAdapterMocopi::MotionPacket
FrameAt(std::uint32_t frameNumber, double streamSeconds)
{
    return FramePacket(frameNumber, streamSeconds, kEpoch + streamSeconds);
}

// Puts the root joint's translation somewhere other than its rest offset, which
// is the one thing every measured frame does and `FramePacket` does not: the
// builder restates rest for all 27 joints, so a session built from it stands
// still. A test about the body's placement needs a body that moved.
inline void
MoveHips(vrmAdapterMocopi::MotionPacket* packet, float x, float y, float z)
{
    for (vrmAdapterMocopi::BoneFrame& bone : packet->frame->bones) {
        if (bone.boneId == 0) {
            bone.transform.translation = {x, y, z};
            return;
        }
    }
}

// Removes a joint's record, so the bones on its path cannot be formed.
inline void
DropJoint(vrmAdapterMocopi::MotionPacket* packet, std::uint16_t boneId)
{
    std::vector<vrmAdapterMocopi::BoneFrame>& bones = packet->frame->bones;
    bones.erase(std::remove_if(bones.begin(), bones.end(),
                               [boneId](const vrmAdapterMocopi::BoneFrame& bone) {
                                   return bone.boneId == boneId;
                               }),
                bones.end());
}

} // namespace vrmAdapterMocopiTests
