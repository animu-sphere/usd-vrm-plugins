// SPDX-License-Identifier: Apache-2.0
//
// execMotion's OpenExec registration: the canonical value type, and the one
// computation this bootstrap registers.
//
// Two registrations live here and nothing else does. Everything a computation
// decides is in ExecMotionIdentity.cpp, over plain values; this file only
// marshals exec's inputs into that call and hands the result back. That split is
// the plan's "a node is a thin wrapper" rule made structural rather than
// intended (motion policy §11.4).
//
// # Why a registered aggregate
//
// `ExecTypeRegistry::RegisterType` static_asserts `!VtIsArray`, so a pose cannot
// cross a computation boundary as a `VtArray`. It also static_asserts equality
// comparability, which `motion::HumanoidPose` has carried since v0.6.0. So the
// canonical type crosses unchanged, which is the only shape under which a node
// stays a wrapper -- the alternatives dissolve the pose into channels and put
// the joint-ordering contract into masks
// (docs/reports/openusd/26.08-openexec-migration.md §4).
//
// # Why UsdSkelAnimation, and why only that
//
// A computation is registered *for a schema*, and 26.08 lets exactly one plugin
// declare a given schema: a second declarer is a coding error and its
// computations for that schema silently never register. `UsdSkelAnimation` is
// therefore this bundle's, and `execVrm` reaches an animation through an input
// accessor rather than by registering on it (WORKSPACE.md §2).

#include "ExecMotionIdentity.h"

#include "pxr/pxr.h"

#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"

#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/exec/typeRegistry.h"
#include "pxr/exec/vdf/context.h"
#include "pxr/exec/vdf/readIterator.h"

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    // The plan's naming: a computation is `<layer>.<verb>`, and the layer is
    // what may not be a product name.
    ((identityPose, "motion.identityPose"))
    // UsdSkelAnimation's own attribute. Declared with its ELEMENT type below
    // and read through an iterator, because an array-valued USD input is boxed
    // into a container of the element type on the way in.
    (joints)
);

TF_REGISTRY_FUNCTION(ExecTypeRegistry)
{
    ExecTypeRegistry::RegisterType(motion::HumanoidPose{});
}

PXR_NAMESPACE_CLOSE_SCOPE

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdSkelAnimation)
{
    self.PrimComputation(_tokens->identityPose)
        .Callback<motion::HumanoidPose>(+[](const VdfContext &ctx) {
            std::vector<std::string> jointPaths;
            VdfReadIterator<TfToken> joint(ctx, _tokens->joints);
            jointPaths.reserve(joint.ComputeSize());
            for (; !joint.IsAtEnd(); ++joint) {
                jointPaths.push_back(joint->GetString());
            }

            // Every input the callback reads arrives through .Inputs() below.
            // A value read from anywhere else -- a clock, a global, a captured
            // reference -- is invisible to invalidation and becomes a stale
            // result no test would see (exec's own "cache safe" contract). This
            // callback reads exactly one thing, and it is declared there.
            //
            // The pose carries NO timestamp, and that is a measurement rather
            // than an omission. A time code is a FRAME; `HumanoidPose::timestamp`
            // is SECONDS; converting needs the stage's `timeCodesPerSecond`, and
            // a computation cannot reach it. `Stage().Metadata<double>()` for
            // that field is accepted by the builder, is not refused even with
            // `.Required()`, and still delivers no value -- so the only rate a
            // callback could apply would be a guess, and a guessed rate is a
            // wrong second that every consumer downstream would take at face
            // value. `tools/motionRetarget` converts with the rate because it
            // holds the stage; exec does not, and `motion.sampleAnimation` will
            // have to be given one rather than find it
            // (docs/reports/openusd/26.08-openexec-mechanism.md §5).
            //
            // The identity pose is the same pose at every time, so this
            // computation declares no time input either: reading `computeTime`
            // and discarding it would tell exec this value changes with the
            // frame, which is a false statement about a computation whose only
            // input is a `uniform` array.
            return execmotion::IdentityPoseForJoints(jointPaths);
        })
        .Inputs(
            AttributeValue<TfToken>(_tokens->joints).Required());
}
