// SPDX-License-Identifier: Apache-2.0
//
// What a run of motion_retarget exits with, frozen by the OpenExec plan's P1-1
// (docs/design/VRM_MOTION_POLICY.md §7.1).
//
// A code says which input is at fault, so a script knows what to change
// without reading the sentence; the sentence says what. Each refusal is
// classified where it is raised rather than at the exit, because only the
// raiser knows whether it was handed a path the user typed or found a stage
// that lacks something.
#pragma once

#include <string>
#include <utility>

namespace motionRetargetTool
{

enum class ExitCode : int
{
    Success = 0,
    // The command line is wrong: an option, a path that is not there, a
    // --humanoid-map file, or a prim or joint an option names that the stage
    // does not have. Fixed by changing the arguments.
    InvalidUserInput = 1,
    // The clip opened and is not a semantic humanoid clip this tool reads.
    // Fixed by a different source, or a converter in front of this one.
    UnsupportedSourceFeature = 2,
    // OpenUSD could not open a layer that is there: no file format is
    // registered for it, or the one that is refused it. Fixed in the
    // environment -- a plugin path -- or in the file.
    StageFailure = 3,
    // The avatar opened and is not a rig the retarget contract can bake onto.
    // Fixed in the avatar, or with --humanoid-map / --skeleton.
    RetargetContractViolation = 4,
    // The inputs were good and the output could not be written.
    OutputAuthoringFailure = 5,
    // Reserved in the same table for a tool that evaluates through OpenExec.
    // motion_retarget evaluates nothing there and never returns it.
    OpenExecEvaluationFailure = 6,
};

// A refusal: the code the process exits with and the line it prints.
struct Failure
{
    ExitCode code = ExitCode::Success;
    std::string message;
};

// Records a refusal and returns false, so a refusal is one statement.
inline bool
Fail(Failure* failure, ExitCode code, std::string message)
{
    failure->code = code;
    failure->message = std::move(message);
    return false;
}

} // namespace motionRetargetTool
