// SPDX-License-Identifier: Apache-2.0
//
// Canonical, parser-independent intermediate model for a VRM avatar.
//
// The whole point of this type is to absorb the structural differences between
// glTF, VRM 0.x and VRM 1.0 *before* any USD is authored. The reader (cgltf)
// populates it; the authorer (USD) consumes it. Neither the reader's nor USD's
// idiosyncrasies leak across this boundary.
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

enum class VrmVersion {
    Unknown,
    Vrm0,
    Vrm1,
};

// A single morph target (blend shape) on a mesh primitive.
struct VrmMorphTarget {
    std::string name;                 // may be empty; uniquified at author time
    std::vector<GfVec3f> positionDeltas;
    std::vector<GfVec3f> normalDeltas; // optional, may be empty
};

// One renderable mesh. We follow the plan's "one USD Mesh per glTF primitive"
// rule, so this maps 1:1 to a (mesh, primitive) pair.
struct VrmMeshPrimitive {
    std::string name;                 // human-meaningful; sanitized/uniquified later
    int sourceMeshIndex = -1;
    int sourcePrimitiveIndex = -1;
    int sourceNodeIndex = -1;         // node that instantiates the mesh
    std::string sourceNodeName;

    std::vector<GfVec3f> points;
    std::vector<GfVec3f> normals;     // optional
    std::vector<GfVec2f> uvs;         // TEXCOORD_0, optional (flipped to USD)
    std::vector<int> faceVertexIndices;
    std::vector<int> faceVertexCounts; // all 3s for triangles

    int materialIndex = -1;           // index into VrmCanonicalDocument::materials

    // Skinning (optional). When jointIndices is non-empty the mesh is bound to
    // the document's skeleton.
    bool skinned = false;
    std::vector<GfVec4f> jointWeights; // per-point, up to 4
    std::vector<int> jointIndices;     // per-point * 4, flattened (skel joint order)
    GfMatrix4d geomBindTransform = GfMatrix4d(1.0);

    std::vector<VrmMorphTarget> morphTargets;
};

// glTF PBR metallic-roughness, normalized to what UsdPreviewSurface needs.
struct VrmMaterial {
    std::string name;
    int sourceMaterialIndex = -1;

    GfVec3f baseColor = GfVec3f(1.0f);
    float opacity = 1.0f;
    float metallic = 1.0f;
    float roughness = 1.0f;
    GfVec3f emissiveColor = GfVec3f(0.0f);
    bool doubleSided = false;
    std::string alphaMode = "OPAQUE";  // OPAQUE | MASK | BLEND
    float alphaCutoff = 0.5f;

    // MToon / VRM shader metadata is preserved verbatim as JSON for later phases.
    bool isMToon = false;
    std::string rawShaderJson;         // VRM material extension JSON, if any
};

// One joint in the skeleton, in glTF skin joint order.
struct VrmJoint {
    std::string name;                  // sanitized segment used in the joint path
    int sourceNodeIndex = -1;
    int parentJointIndex = -1;         // index into VrmCanonicalDocument::joints, -1 = root
    GfMatrix4d restTransform = GfMatrix4d(1.0);  // local-space, USD convention
    GfMatrix4d bindTransform = GfMatrix4d(1.0);  // world-space, USD convention
};

// VRM humanoid bone -> skeleton joint mapping.
struct VrmHumanoidBone {
    std::string semanticName;          // e.g. "hips", "leftUpperArm"
    int jointIndex = -1;               // index into VrmCanonicalDocument::joints
};

// VRM 0.x BlendShapeGroup / VRM 1.0 Expression (binding info only; evaluation is
// out of scope for the file-format plugin).
struct VrmExpression {
    std::string name;                  // preset or custom name
    bool isPreset = false;
    bool isBinary = false;
    // Morph-target bindings expressed as (mesh primitive index, morph index, weight).
    struct MorphBind { int meshPrimitiveIndex; int morphTargetIndex; float weight; };
    std::vector<MorphBind> morphBinds;
};

struct VrmCanonicalDocument {
    VrmVersion version = VrmVersion::Unknown;
    std::string specVersion;           // e.g. "0.0", "1.0"

    std::vector<VrmMeshPrimitive> meshes;
    std::vector<VrmMaterial> materials;
    std::vector<VrmJoint> joints;      // empty => no skeleton
    std::vector<VrmHumanoidBone> humanoidBones;
    std::vector<VrmExpression> expressions;

    // Raw VRM blocks preserved as JSON on /Asset.customData (lossless preservation).
    std::string metaJson;              // meta / license / permissions
    std::string rawVrmExtensionJson;   // full VRM(C_vrm) extension JSON

    // Non-fatal diagnostics gathered while reading.
    std::vector<std::string> warnings;
};

PXR_NAMESPACE_CLOSE_SCOPE
