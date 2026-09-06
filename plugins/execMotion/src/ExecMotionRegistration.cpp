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

#include "pxr/exec/ef/time.h"
#include "pxr/exec/exec/builtinComputations.h"
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
            // result no test would see (exec's own "cache safe" contract).
            const EfTime &time = ctx.GetInputValue<EfTime>(
                ExecBuiltinComputations->computeTime);

            // UsdTimeCode::GetValue() is a coding error on the default time
            // code, and the default is what a stage evaluates at until someone
            // calls ChangeTime -- so it is the normal case, not an edge one.
            const UsdTimeCode timeCode = time.GetTimeCode();
            const double timestamp =
                timeCode.IsNumeric() ? timeCode.GetValue() : 0.0;

            return execmotion::IdentityPoseForJoints(jointPaths, timestamp);
        })
        .Inputs(
            AttributeValue<TfToken>(_tokens->joints).Required(),
            Stage().Computation<EfTime>(
                ExecBuiltinComputations->computeTime).Required());
}
