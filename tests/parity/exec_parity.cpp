// SPDX-License-Identifier: Apache-2.0
//
// exec_parity -- the OpenExec plan's P0-6 harness: one `motion_retarget` bake
// against the same avatar and clip evaluated through `execMotion` + `execVrm`,
// sample by sample.
//
//     exec_parity --avatar A --animation C --bake B --report R.json
//                 [--tool-log L] [motion_retarget's mapping and root-motion flags]
//
// The bake is the tool's output for the same `--avatar`, `--animation` and
// flags, and the log is what the tool printed while writing it; this program
// does not run the tool. The driver beside it does, so that the two are handed
// one argument list and cannot drift apart.
//
// # The stage it evaluates
//
// The tool opens the avatar and the clip as two stages; exec evaluates one. So
// the parity stage is an anonymous layer that **sublayers** the avatar's layer
// and the clip's layer, which keeps every path the tool saw at the path exec
// sees -- a reference would move one of them under a new prim. Its
// `timeCodesPerSecond` is the clip's, so the clip's time samples cross the
// sublayer unscaled. The two layers must not share a root prim name, or they
// would compose into one prim that neither file describes.
//
// Onto that layer it authors the statements the tool takes from its command
// line or from the clip's own stage, and exec can only take from a prim
// (docs/roadmap/openexec-foundation.md §9):
//
//   * `motion:timeCodesPerSecond` on the clip's animation, equal to the clip
//     stage's rate, unless the clip already states one;
//   * `vrm:retarget:sourceSkeleton` on the humanoid, naming the skeleton the
//     tool would read the clip's rest from;
//   * `--humanoid-map`'s bindings as `vrm:humanBones:*` on the humanoid -- on a
//     `/Parity/Humanoid` with `VrmHumanoidAPI` applied when the avatar has
//     none -- and `--skeleton` as its `vrm:skeleton`;
//   * the four root-motion flags as the four `vrm:retarget:*` statements,
//     word for word, and nothing for a flag the tool was not given.
//
// Every one of those is the harness acting as the producer the contract does
// not yet name, which the report says rather than hides.
//
// # How a sample is compared
//
// At the bake's **own** time samples, paired by index with the clip's keys.
// The tool rebuilds a time code as `(frame / rate) * rate`, which is not
// always `frame` (the joint-transforms report, §8), so reading the bake at the
// clip's frame asks USD to interpolate between two of its samples. The value
// exec answers at the clip's key `f` is compared with the value the bake
// states at its sample `t`, and how far `t` is from `f` is reported as a
// placement difference of its own. The comparison at `f` is measured beside
// it, so the cost of the rule is a number rather than a warning.
//
// Each difference is classified, never widened (the plan's P0-6): exact, a
// quaternion's sign only, within `openstrata::motion::MotionTolerance` -- the contract's
// tolerance, not one chosen here -- or a divergence. Only a divergence, a
// refusal, two values that do not have the same shape, or two diagnostics
// lists that differ fail the run.
//
// # How the diagnostics are compared
//
// As lines, exactly, in order. Exec answers `vrm.computeRetargetDiagnostics`
// at each key -- the rig's report, then the pose's -- and the answers merged
// over the keys in order are the list the library's clip overload reports,
// which is what the tool prints, one `FormatRetargetDiagnostic` line each. So
// exec's list goes through the same formatter and is compared, line for line,
// with the tool's lines whose code the library raises. The ones a caller
// raises -- a time range the clip did not state, an output naming an input --
// say what the tool's own stage and file system added, and exec has neither;
// they are reported beside the comparison and never counted in it.
//
// Comparing whole lines rather than codes and subjects is the stricter choice
// and costs nothing: both sides format one library's values, so a detail that
// differs is a difference in what the retarget was told.
//
// # How it drives exec
//
// Through `ExecDriver` beside it, the driver contract as code: one system for
// the stage, each request armed once and its arming refusals kept, each key's
// instant named before it is computed. The driver's `VRM_OPENEXEC_*` lines are
// reported, and an error among them fails the run.
//
// # What it links
//
// OpenUSD and exec, the driver, `motionRetarget` for the result type, and
// `motionCore` for the bone vocabulary and `AngleBetween`. Neither plugin:
// both are found through `PXR_PLUGINPATH_NAME`, the path a packaged bundle
// takes.
//
// # Where what it ran came from
//
// The report names every plugin the registry loaded and every module the
// process mapped, by path. Both libraries it links are static, so a run
// against an installed product can be held to "nothing but this executable
// came from a build tree" (scripts/artifact_only_exec_smoke.py) -- a claim an
// environment variable alone cannot make, because a loader that found a
// plugin's dependency somewhere else says nothing about it.

#include "pxr/pxr.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnosticMgr.h"
#include "pxr/base/tf/status.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/warning.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/types.h"
#include "pxr/base/vt/value.h"

#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdSkel/animation.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/skeleton.h"

#include "ExecDriver.h"

#include <motionCore/Compare.h>
#include <motionCore/MotionPose.h>
#include <motionRetarget/Diagnostics.h>
#include <motionRetarget/PoseRetargeter.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

const TfToken kJointTransforms("vrm.computeJointLocalTransforms");
const TfToken kRetargetDiagnostics("vrm.computeRetargetDiagnostics");
const TfToken kHumanoidApi("VrmHumanoidAPI");
const TfToken kHumanBonesHips("vrm:humanBones:hips");
const TfToken kSkeletonRel("vrm:skeleton");
const TfToken kSourceSkeleton("vrm:retarget:sourceSkeleton");
const TfToken kRootMotion("vrm:retarget:rootMotion");
const TfToken kRootJoint("vrm:retarget:rootJoint");
const TfToken kTranslationScale("vrm:retarget:translationScale");
const TfToken kPreserveTargetHeight("vrm:retarget:preserveTargetHeight");
const TfToken kClipRate("motion:timeCodesPerSecond");
const std::string kHumanBonesPrefix = "vrm:humanBones:";
const SdfPath kParityHumanoid("/Parity/Humanoid");

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

struct Arguments
{
    std::string avatar;
    std::string animation;
    std::string bake;
    std::string report;
    std::string toolLog;
    bool quiet = false;
    std::string humanoidMap;
    std::string skeleton;
    std::string clipSkeleton;
    std::optional<std::string> rootMotion;
    std::optional<std::string> rootJoint;
    std::optional<float> translationScale;
    bool preserveTargetHeight = false;
};

// The tool's flags. The mapping and root-motion ones become statements on the
// parity stage; the ones that only narrow what the tool writes are accepted
// and change nothing here, because they narrow it towards what exec computes:
// `--no-look-at` keeps a bone look-at's eye rotations out of the joint arrays,
// `--no-expressions` touches blend-shape weights only, and `--animation-name`
// renames a prim the harness reaches through the binding anyway. `--resample`
// is refused by name: it moves the bake onto instants the clip never keyed,
// and exec answers a clip's own keys.
bool
Parse(int argc, char** argv, Arguments* out, std::string* error)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string& flag = args[i];
        auto value = [&](std::string* into)
        {
            if (i + 1 >= args.size())
            {
                *error = flag + " requires a value";
                return false;
            }
            *into = args[++i];
            return true;
        };
        std::string text;
        if (flag == "--avatar")
        {
            if (!value(&out->avatar))
                return false;
        }
        else if (flag == "--animation")
        {
            if (!value(&out->animation))
                return false;
        }
        else if (flag == "--bake")
        {
            if (!value(&out->bake))
                return false;
        }
        else if (flag == "--report")
        {
            if (!value(&out->report))
                return false;
        }
        else if (flag == "--tool-log")
        {
            if (!value(&out->toolLog))
                return false;
        }
        else if (flag == "--humanoid-map")
        {
            if (!value(&out->humanoidMap))
                return false;
        }
        else if (flag == "--skeleton")
        {
            if (!value(&out->skeleton))
                return false;
        }
        else if (flag == "--clip-skeleton")
        {
            if (!value(&out->clipSkeleton))
                return false;
        }
        else if (flag == "--root-motion")
        {
            if (!value(&text))
                return false;
            out->rootMotion = text;
        }
        else if (flag == "--root-joint")
        {
            if (!value(&text))
                return false;
            out->rootJoint = text;
        }
        else if (flag == "--translation-scale")
        {
            if (!value(&text))
                return false;
            try
            {
                std::size_t used = 0;
                // Parsed as the tool parses it -- a double, then narrowed --
                // so the statement is the float the tool's option holds.
                const double scale = std::stod(text, &used);
                if (used != text.size())
                {
                    throw std::invalid_argument(text);
                }
                out->translationScale = static_cast<float>(scale);
            }
            catch (const std::exception&)
            {
                *error = "--translation-scale expects a number, got '" + text + "'";
                return false;
            }
        }
        else if (flag == "--preserve-target-height")
        {
            out->preserveTargetHeight = true;
        }
        else if (flag == "--no-look-at" || flag == "--no-expressions")
        {
            // Narrow the tool towards exec; nothing to state.
        }
        else if (flag == "--quiet")
        {
            // Nothing to state either, and it silences the half of the
            // diagnostics comparison the tool prints.
            out->quiet = true;
        }
        else if (flag == "--animation-name")
        {
            if (!value(&text))
                return false;
        }
        else if (flag == "--resample")
        {
            *error = "--resample moves the bake onto instants the clip never "
                     "keyed, and exec answers a clip's own keys; a parity run "
                     "does not take it";
            return false;
        }
        else
        {
            *error = "unknown argument '" + flag + "'";
            return false;
        }
    }
    if (out->avatar.empty() || out->animation.empty() || out->bake.empty() || out->report.empty())
    {
        *error = "--avatar, --animation, --bake and --report are required";
        return false;
    }
    if (out->quiet && !out->toolLog.empty())
    {
        // An empty log beside a rig with something to say would read as the
        // tool disagreeing, when it was only told to keep quiet.
        *error = "--quiet silences the tool's diagnostics, so --tool-log has "
                 "nothing to compare";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

// Every warning and status line exec posts, counted by text. A parity run
// reports what each side *said* as well as what it answered, and the executor
// repeats one warning per unbound bone each time the map is computed.
class Diagnostics : public TfDiagnosticMgr::Delegate
{
  public:
    Diagnostics()
    {
        TfDiagnosticMgr::GetInstance().AddDelegate(this);
    }
    ~Diagnostics() override
    {
        TfDiagnosticMgr::GetInstance().RemoveDelegate(this);
    }

    void
    IssueError(const TfError&) override
    {
    }
    void
    IssueFatalError(const TfCallContext&, const std::string& message) override
    {
        std::fprintf(stderr, "fatal: %s\n", message.c_str());
    }
    void
    IssueStatus(const TfStatus&) override
    {
    }
    void
    IssueWarning(const TfWarning& warning) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ++_warnings[warning.GetCommentary()];
    }

    std::map<std::string, int>
    Warnings()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _warnings;
    }

  private:
    std::mutex _mutex;
    std::map<std::string, int> _warnings;
};

// What the tool printed as coded diagnostics, split at the layer boundary.
struct ToolDiagnostics
{
    std::vector<std::string> library; // a code the library raises
    std::vector<std::string> caller;  // a code only a stage or a file system can
};

// The tool prints each diagnostic as `motion_retarget: ` followed by the
// library's own line, and prints nothing else in brackets. A line whose
// bracket names no frozen code is not a diagnostic and is left alone.
bool
ReadToolLog(const std::string& path, ToolDiagnostics* out, std::string* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        *error = "could not open the tool log " + path;
        return false;
    }
    const std::string prefix = "motion_retarget: [";
    for (std::string line; std::getline(file, line);)
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.compare(0, prefix.size(), prefix) != 0)
        {
            continue;
        }
        const std::size_t close = line.find(']', prefix.size());
        if (close == std::string::npos)
        {
            continue;
        }
        const std::optional<openstrata::motion::RetargetDiagnosticCode> code =
            openstrata::motion::FindRetargetDiagnosticCode(
                line.substr(prefix.size(), close - prefix.size()));
        if (!code)
        {
            continue;
        }
        std::string formatted = line.substr(prefix.size() - 1);
        (openstrata::motion::RetargetDiagnosticIsLibraryRaised(*code) ? out->library : out->caller)
            .push_back(std::move(formatted));
    }
    return true;
}

// The entries of `a` that `b` does not have, in `a`'s order.
std::vector<std::string>
Missing(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    const std::set<std::string> in(b.begin(), b.end());
    std::vector<std::string> out;
    for (const std::string& line : a)
    {
        if (!in.count(line))
        {
            out.push_back(line);
        }
    }
    return out;
}

JsArray
JsLines(const std::vector<std::string>& lines)
{
    JsArray out;
    for (const std::string& line : lines)
    {
        out.emplace_back(line);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Discovery: the tool's rules, applied to the stages the tool opens
// ---------------------------------------------------------------------------
//
// These three are `tools/motionRetarget/src/StageIo.cpp`'s, restated because a
// harness cannot call a tool. They are the questions "which humanoid, which
// rig, which clip", and a harness that answered them differently from the tool
// would be comparing two bakes of two different inputs. Each is applied to the
// avatar or the clip opened ALONE, as the tool opens them: on the composed
// parity stage, "the first skeleton" could be the clip's.

struct AvatarShape
{
    UsdStageRefPtr stage;
    SdfPath humanoid; // empty when no prim states vrm:humanBones:hips
    SdfPath skeleton;
};

struct ClipShape
{
    UsdStageRefPtr stage;
    double rate = 0.0;
    SdfPath skeleton;
    SdfPath animation;
};

SdfPath
FirstSkeleton(const UsdStageRefPtr& stage)
{
    for (const UsdPrim& prim : stage->Traverse())
    {
        if (prim.IsA<UsdSkelSkeleton>())
        {
            return prim.GetPath();
        }
    }
    return SdfPath();
}

bool
DiscoverAvatar(const Arguments& args, AvatarShape* avatar, std::string* error)
{
    avatar->stage = UsdStage::Open(args.avatar);
    if (!avatar->stage)
    {
        *error = "could not open the avatar " + args.avatar;
        return false;
    }
    // The tool's humanoid is the first prim stating a hips binding -- by
    // attribute, not by schema. exec's is a prim with the schema applied; the
    // two are reconciled below rather than assumed to agree.
    for (const UsdPrim& prim : avatar->stage->Traverse())
    {
        if (prim.HasAttribute(kHumanBonesHips))
        {
            avatar->humanoid = prim.GetPath();
            break;
        }
    }
    if (!args.skeleton.empty())
    {
        avatar->skeleton = SdfPath(args.skeleton);
    }
    else if (!avatar->humanoid.IsEmpty())
    {
        SdfPathVector targets;
        const UsdRelationship rel =
            avatar->stage->GetPrimAtPath(avatar->humanoid).GetRelationship(kSkeletonRel);
        if (rel && rel.GetTargets(&targets) && !targets.empty())
        {
            avatar->skeleton = targets.front();
        }
    }
    if (avatar->skeleton.IsEmpty())
    {
        avatar->skeleton = FirstSkeleton(avatar->stage);
    }
    if (avatar->skeleton.IsEmpty() ||
        !UsdSkelSkeleton(avatar->stage->GetPrimAtPath(avatar->skeleton)))
    {
        *error = "the avatar has no UsdSkelSkeleton the tool would retarget onto";
        return false;
    }
    return true;
}

bool
DiscoverClip(const Arguments& args, ClipShape* clip, std::string* error)
{
    clip->stage = UsdStage::Open(args.animation);
    if (!clip->stage)
    {
        *error = "could not open the clip " + args.animation;
        return false;
    }
    clip->rate = clip->stage->GetTimeCodesPerSecond();
    if (clip->rate <= 0.0)
    {
        clip->rate = 30.0; // the tool's fallback
    }
    clip->skeleton =
        args.clipSkeleton.empty() ? FirstSkeleton(clip->stage) : SdfPath(args.clipSkeleton);
    const UsdPrim skeleton = clip->stage->GetPrimAtPath(clip->skeleton);
    if (!UsdSkelSkeleton(skeleton))
    {
        *error = "the clip has no UsdSkelSkeleton";
        return false;
    }
    UsdPrim animation;
    if (!UsdSkelBindingAPI(skeleton).GetAnimationSource(&animation) || !animation)
    {
        // The tool falls back to "the one SkelAnimation on the stage". exec
        // follows a binding or nothing, so that is a stage only one of the two
        // reads -- a P0-6 row, not something to paper over here.
        *error = "the clip's skeleton <" + clip->skeleton.GetString() +
                 "> binds no animation; motion_retarget would fall back to the "
                 "stage's only SkelAnimation and exec cannot";
        return false;
    }
    clip->animation = animation.GetPath();
    return true;
}

std::set<std::string>
RootPrimNames(const UsdStageRefPtr& stage)
{
    std::set<std::string> names;
    for (const UsdPrim& prim : stage->GetPseudoRoot().GetAllChildren())
    {
        names.insert(prim.GetName().GetString());
    }
    return names;
}

// --humanoid-map, parsed as the tool parses it: an object of human bone name
// to joint token, and a key that is not a bone of the vocabulary is refused.
bool
ReadMap(const std::string& path, std::vector<std::pair<std::string, std::string>>* entries,
        std::string* error)
{
    std::ifstream file(path);
    if (!file)
    {
        *error = "could not open " + path;
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    JsParseError parseError;
    const JsValue parsed = JsParseString(text.str(), &parseError);
    if (!parsed.IsObject())
    {
        *error = path + " is not a JSON object";
        return false;
    }
    for (const auto& [bone, token] : parsed.GetJsObject())
    {
        if (!token.IsString() || !openstrata::motion::FindHumanJoint(bone))
        {
            *error = path + ": '" + bone + "' is not a bone bound to a token";
            return false;
        }
        entries->emplace_back(bone, token.GetString());
    }
    return true;
}

// ---------------------------------------------------------------------------
// The parity stage
// ---------------------------------------------------------------------------

struct ParityStage
{
    UsdStageRefPtr stage;
    UsdPrim humanoid;
    std::vector<std::string> authored; // what the harness stated, for the report
};

bool
ComposeParityStage(const Arguments& args, const AvatarShape& avatar, const ClipShape& clip,
                   ParityStage* parity, std::string* error)
{
    const std::set<std::string> avatarRoots = RootPrimNames(avatar.stage);
    for (const std::string& name : RootPrimNames(clip.stage))
    {
        if (avatarRoots.count(name) || name == "Parity")
        {
            *error = "the avatar and the clip both have a root prim named '" + name +
                     "', so sublayering them would compose one prim "
                     "neither file describes";
            return false;
        }
    }
    if (avatarRoots.count("Parity"))
    {
        *error = "the avatar has a root prim named 'Parity', which this "
                 "harness reserves for what it states";
        return false;
    }

    SdfLayerRefPtr root = SdfLayer::CreateAnonymous("parity.usda");
    root->SetSubLayerPaths({avatar.stage->GetRootLayer()->GetIdentifier(),
                            clip.stage->GetRootLayer()->GetIdentifier()});
    root->SetTimeCodesPerSecond(clip.rate);
    parity->stage = UsdStage::Open(root);
    if (!parity->stage)
    {
        *error = "could not open the parity stage";
        return false;
    }
    UsdStageRefPtr stage = parity->stage;

    // The humanoid. The avatar's own when the tool found one, and then it has
    // to carry the schema exec computes on -- a prim stating the bindings
    // without it is read by the tool and invisible to exec (the humanoid
    // report, §7), which is a divergence of input, not of evaluation.
    std::vector<std::pair<std::string, std::string>> map;
    if (!args.humanoidMap.empty() && !ReadMap(args.humanoidMap, &map, error))
    {
        return false;
    }
    if (!avatar.humanoid.IsEmpty())
    {
        parity->humanoid = stage->GetPrimAtPath(avatar.humanoid);
        const TfTokenVector applied = parity->humanoid.GetAppliedSchemas();
        if (std::find(applied.begin(), applied.end(), kHumanoidApi) == applied.end())
        {
            *error = "<" + avatar.humanoid.GetString() +
                     "> states human bones without VrmHumanoidAPI applied; the "
                     "tool reads it and exec cannot";
            return false;
        }
    }
    else if (!map.empty())
    {
        parity->humanoid = UsdGeomScope::Define(stage, kParityHumanoid).GetPrim();
        parity->humanoid.AddAppliedSchema(kHumanoidApi);
        parity->authored.push_back("defined <" + kParityHumanoid.GetString() +
                                   "> with VrmHumanoidAPI");
    }
    else
    {
        *error = "the avatar states no humanoid and no --humanoid-map was "
                 "given; the tool refuses that too";
        return false;
    }

    // `vrm:skeleton` is stated when the avatar did not, or when --skeleton
    // chose another rig -- the tool's precedence, with the flag first.
    SdfPathVector named;
    const UsdRelationship skeletonRel = parity->humanoid.GetRelationship(kSkeletonRel);
    if (!skeletonRel || !skeletonRel.GetTargets(&named) || named.empty() ||
        named.front() != avatar.skeleton)
    {
        parity->humanoid.CreateRelationship(kSkeletonRel, /*custom*/ false)
            .SetTargets({avatar.skeleton});
        parity->authored.push_back("vrm:skeleton = <" + avatar.skeleton.GetString() + ">");
    }

    // The map, over whatever the avatar stated -- the tool merges the file
    // over the stage the same way.
    for (const auto& [bone, token] : map)
    {
        parity->humanoid
            .CreateAttribute(TfToken(kHumanBonesPrefix + bone), SdfValueTypeNames->Token,
                             /*custom*/ false, SdfVariabilityUniform)
            .Set(TfToken(token));
    }
    if (!map.empty())
    {
        parity->authored.push_back(std::to_string(map.size()) +
                                   " vrm:humanBones:* from --humanoid-map");
    }

    parity->humanoid.CreateRelationship(kSourceSkeleton, /*custom*/ true)
        .SetTargets({clip.skeleton});
    parity->authored.push_back("vrm:retarget:sourceSkeleton = <" + clip.skeleton.GetString() + ">");

    // The four statements, only for the flags the tool was given: an absent
    // statement keeps the library's default, which is what an absent flag
    // gives the tool.
    if (args.rootMotion)
    {
        parity->humanoid.CreateAttribute(kRootMotion, SdfValueTypeNames->Token, true)
            .Set(TfToken(*args.rootMotion));
        parity->authored.push_back("vrm:retarget:rootMotion = " + *args.rootMotion);
    }
    if (args.rootJoint)
    {
        parity->humanoid.CreateAttribute(kRootJoint, SdfValueTypeNames->Token, true)
            .Set(TfToken(*args.rootJoint));
        parity->authored.push_back("vrm:retarget:rootJoint = " + *args.rootJoint);
    }
    if (args.translationScale)
    {
        parity->humanoid.CreateAttribute(kTranslationScale, SdfValueTypeNames->Float, true)
            .Set(*args.translationScale);
        parity->authored.push_back("vrm:retarget:translationScale = " +
                                   std::to_string(*args.translationScale));
    }
    if (args.preserveTargetHeight)
    {
        parity->humanoid.CreateAttribute(kPreserveTargetHeight, SdfValueTypeNames->Bool, true)
            .Set(true);
        parity->authored.push_back("vrm:retarget:preserveTargetHeight = true");
    }

    // The rate, on the clip's animation. Only when the clip states none: one
    // that does is the clip's own statement, and if it disagrees with the
    // stage's the two implementations are handed two clocks. No array depends
    // on which, so only the timestamp comparison shows it -- placement does
    // not, since neither the clip's keys nor the bake's samples read the
    // attribute.
    const UsdPrim animation = stage->GetPrimAtPath(clip.animation);
    if (!animation.GetAttribute(kClipRate).HasAuthoredValue())
    {
        animation.CreateAttribute(kClipRate, SdfValueTypeNames->Double, true).Set(clip.rate);
        std::ostringstream said;
        said << "motion:timeCodesPerSecond = " << clip.rate << " on <" << clip.animation.GetString()
             << ">";
        parity->authored.push_back(said.str());
    }
    return true;
}

// The clip's keys, as exec's sampler sees them: the union of the animation's
// rotation and translation time samples on the parity stage. The tool's union
// adds a clip's expression and gaze keys; a clip with either is refused below
// when the counts disagree, rather than paired by guesswork.
std::vector<double>
ClipKeys(const ParityStage& parity, const ClipShape& clip)
{
    const UsdSkelAnimation animation(parity.stage->GetPrimAtPath(clip.animation));
    std::vector<double> rotations;
    std::vector<double> translations;
    animation.GetRotationsAttr().GetTimeSamples(&rotations);
    animation.GetTranslationsAttr().GetTimeSamples(&translations);
    std::set<double> keys(rotations.begin(), rotations.end());
    keys.insert(translations.begin(), translations.end());
    if (keys.empty())
    {
        keys.insert(clip.stage->GetStartTimeCode()); // the tool's rule
    }
    return std::vector<double>(keys.begin(), keys.end());
}

// ---------------------------------------------------------------------------
// The bake
// ---------------------------------------------------------------------------

struct Bake
{
    UsdStageRefPtr stage;
    VtTokenArray joints;
    VtVec3hArray scales;
    UsdAttribute rotations;
    UsdAttribute translations;
    std::vector<double> times;
};

bool
ReadBake(const std::string& path, const SdfPath& skeleton, Bake* bake, std::string* error)
{
    bake->stage = UsdStage::Open(path);
    if (!bake->stage)
    {
        *error = "could not open the bake " + path;
        return false;
    }
    UsdPrim animationPrim;
    if (!UsdSkelBindingAPI(bake->stage->GetPrimAtPath(skeleton))
             .GetAnimationSource(&animationPrim) ||
        !animationPrim)
    {
        *error = "the bake binds no animation to <" + skeleton.GetString() +
                 ">, the skeleton exec's humanoid names";
        return false;
    }
    const UsdSkelAnimation animation(animationPrim);
    animation.GetJointsAttr().Get(&bake->joints);
    animation.GetScalesAttr().Get(&bake->scales);
    bake->rotations = animation.GetRotationsAttr();
    bake->translations = animation.GetTranslationsAttr();
    std::vector<double> rotationTimes;
    std::vector<double> translationTimes;
    bake->rotations.GetTimeSamples(&rotationTimes);
    bake->translations.GetTimeSamples(&translationTimes);
    if (rotationTimes != translationTimes)
    {
        *error = "the bake keys its rotations and translations at different "
                 "times";
        return false;
    }
    bake->times = rotationTimes;
    return true;
}

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

// The plan's categories that can be told from two values, cheapest first.
// Ordering, missing fields and time sampling are about the shape of the two
// answers and are counted separately.
enum class Kind
{
    Exact,
    Sign,
    Rounding,
    Divergence
};

const char*
Name(Kind kind)
{
    switch (kind)
    {
    case Kind::Exact:
        return "exact";
    case Kind::Sign:
        return "sign";
    case Kind::Rounding:
        return "rounding";
    case Kind::Divergence:
        return "divergence";
    }
    return "?";
}

struct Tally
{
    std::map<Kind, std::size_t> counts;
    double worst = 0.0;
    std::string firstDivergence;

    void
    Add(Kind kind, double amount, const std::string& where)
    {
        ++counts[kind];
        if (kind != Kind::Exact && kind != Kind::Sign)
        {
            worst = std::max(worst, amount);
        }
        if (kind == Kind::Divergence && firstDivergence.empty())
        {
            firstDivergence = where;
        }
    }
    std::size_t
    Of(Kind kind) const
    {
        const auto found = counts.find(kind);
        return found == counts.end() ? 0 : found->second;
    }
};

const openstrata::motion::MotionTolerance kTolerance{};

Kind
ClassifyRotation(const GfQuatf& exec, const GfQuatf& baked, double* angle)
{
    *angle = 0.0;
    if (exec == baked)
    {
        return Kind::Exact;
    }
    if (exec == -baked)
    {
        return Kind::Sign;
    }
    *angle = openstrata::motion::AngleBetween(exec, baked);
    // A NaN angle is not within anything.
    return *angle <= kTolerance.angle ? Kind::Rounding : Kind::Divergence;
}

Kind
ClassifyTranslation(const GfVec3f& exec, const GfVec3f& baked, double* distance)
{
    *distance = 0.0;
    if (exec == baked)
    {
        return Kind::Exact;
    }
    *distance = (exec - baked).GetLength();
    return *distance <= kTolerance.distance ? Kind::Rounding : Kind::Divergence;
}

// Where the bake put a sample against the key exec evaluated, in seconds: the
// same instant, a rounding of it within the contract's time quantum, or
// another instant.
Kind
ClassifyPlacement(double key, double bakedTime, double rate, double* seconds)
{
    *seconds = std::abs(bakedTime - key) / rate;
    if (bakedTime == key)
    {
        return Kind::Exact;
    }
    return *seconds <= kTolerance.time ? Kind::Rounding : Kind::Divergence;
}

JsObject
TallyJson(const Tally& tally, const char* unit)
{
    JsObject out;
    for (const Kind kind : {Kind::Exact, Kind::Sign, Kind::Rounding, Kind::Divergence})
    {
        out[Name(kind)] = JsValue(static_cast<int64_t>(tally.Of(kind)));
    }
    out[std::string("worst_") + unit] = JsValue(tally.worst);
    if (!tally.firstDivergence.empty())
    {
        out["first_divergence"] = JsValue(tally.firstDivergence);
    }
    return out;
}

void
PrintTally(const char* what, const Tally& tally, const char* unit)
{
    std::printf("  %-13s exact %zu  sign %zu  rounding %zu  divergence %zu"
                "  (worst %.3g %s)\n",
                what, tally.Of(Kind::Exact), tally.Of(Kind::Sign), tally.Of(Kind::Rounding),
                tally.Of(Kind::Divergence), tally.worst, unit);
    if (!tally.firstDivergence.empty())
    {
        std::printf("    first divergence: %s\n", tally.firstDivergence.c_str());
    }
}

// ---------------------------------------------------------------------------
// Provenance: what the process actually loaded
// ---------------------------------------------------------------------------

// Every file the process has mapped as a module, by the path the loader
// resolved -- the executable, OpenUSD, the plugins and whatever each of them
// pulled in. Sorted and unique, so two runs over one product compare.
std::vector<std::string>
LoadedModules()
{
    std::set<std::string> paths;
#if defined(_WIN32)
    const HANDLE snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W entry;
        entry.dwSize = sizeof(entry);
        for (BOOL more = Module32FirstW(snapshot, &entry); more;
             more = Module32NextW(snapshot, &entry))
        {
            const int size =
                WideCharToMultiByte(CP_UTF8, 0, entry.szExePath, -1, nullptr, 0, nullptr, nullptr);
            if (size > 1)
            {
                std::string path(static_cast<std::size_t>(size - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, entry.szExePath, -1, path.data(), size, nullptr,
                                    nullptr);
                paths.insert(path);
            }
        }
        CloseHandle(snapshot);
    }
#elif defined(__APPLE__)
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i)
    {
        if (const char* name = _dyld_get_image_name(i))
        {
            paths.insert(name);
        }
    }
#else
    // A mapping's path is the last field of a line, and only a line that has
    // one names a file; the executable is listed like any other mapping.
    std::ifstream maps("/proc/self/maps");
    for (std::string line; std::getline(maps, line);)
    {
        const std::size_t slash = line.find('/');
        if (slash != std::string::npos)
        {
            paths.insert(line.substr(slash));
        }
    }
#endif
    return {paths.begin(), paths.end()};
}

// The plugins the registry loaded, by name and path. A plugin the registry
// knows and never loaded is left out: registered is a statement about a
// plugInfo.json, loaded is the one about which library answered.
JsObject
LoadedPlugins()
{
    JsObject plugins;
    for (const PlugPluginPtr& plugin : PlugRegistry::GetInstance().GetAllPlugins())
    {
        if (plugin && plugin->IsLoaded())
        {
            plugins[plugin->GetName()] = JsValue(plugin->GetPath());
        }
    }
    return plugins;
}

} // namespace

int
main(int argc, char** argv)
{
    Arguments args;
    std::string error;
    if (!Parse(argc, argv, &args, &error))
    {
        std::fprintf(stderr, "exec_parity: %s\n", error.c_str());
        return 2;
    }

    Diagnostics diagnostics;

    AvatarShape avatar;
    ClipShape clip;
    ParityStage parity;
    if (!DiscoverAvatar(args, &avatar, &error) || !DiscoverClip(args, &clip, &error) ||
        !ComposeParityStage(args, avatar, clip, &parity, &error))
    {
        std::fprintf(stderr, "exec_parity: %s\n", error.c_str());
        return 1;
    }

    Bake bake;
    if (!ReadBake(args.bake, avatar.skeleton, &bake, &error))
    {
        std::fprintf(stderr, "exec_parity: %s\n", error.c_str());
        return 1;
    }

    const std::vector<double> keys = ClipKeys(parity, clip);
    if (keys.size() != bake.times.size())
    {
        std::fprintf(stderr,
                     "exec_parity: the clip's body is keyed at %zu instants "
                     "and the bake has %zu samples; this harness pairs a "
                     "sample with a body key, and a clip keying expressions "
                     "or a gaze between them is not paired by guesswork\n",
                     keys.size(), bake.times.size());
        return 1;
    }

    ToolDiagnostics tool;
    if (!args.toolLog.empty() && !ReadToolLog(args.toolLog, &tool, &error))
    {
        std::fprintf(stderr, "exec_parity: %s\n", error.c_str());
        return 1;
    }

    // -- exec, two requests armed once and moved through the keys ------------
    // Through the driver, which is the driver contract as code
    // (docs/design/MOTION_CONTRACT.md): it names each instant before
    // computing, keeps what the arming compute posted, and raises the
    // `VRM_OPENEXEC_*` codes -- so a bundle missing from the session is a
    // named key here rather than an empty value and a coding error.
    //
    // The values and the diagnostics are asked for apart, so each is timed
    // alone: the second compute at a key finds the rig, the map and the pose
    // already cached by the first, and what it costs is what diagnosing adds.
    execdriver::Driver driver(parity.stage);
    const SdfPath humanoid = parity.humanoid.GetPath();
    // The first compute arms a request, at the default time code, where the
    // retarget refuses by design (the retarget report, §5) -- so its errors
    // are not a failure. They are kept anyway: a node nothing invalidates
    // computes only here, so this is the one place its refusal is ever posted
    // (the diagnostics report, §4).
    execdriver::Frame armedValues;
    execdriver::Frame armedDiagnostics;
    const execdriver::Driver::RequestId request = driver.Add(
        {execdriver::Key::Of<openstrata::motion::JointLocalTransforms>(humanoid, kJointTransforms)},
        &armedValues);
    const execdriver::Driver::RequestId diagnosticsRequest =
        driver.Add({execdriver::Key::Of<openstrata::motion::RetargetDiagnostics>(
                       humanoid, kRetargetDiagnostics)},
                   &armedDiagnostics);
    std::vector<std::string> armingErrors;
    for (const execdriver::Frame* armed : {&armedValues, &armedDiagnostics})
    {
        armingErrors.insert(armingErrors.end(), armed->refusals.begin(), armed->refusals.end());
        armingErrors.insert(armingErrors.end(), armed->errors.begin(), armed->errors.end());
    }

    std::vector<openstrata::motion::JointLocalTransforms> answers;
    answers.reserve(keys.size());
    // Which samples refused, kept beside the answers rather than read back off
    // them: an answer with no joints is a legitimate value (the animation of a
    // skeleton with no joints), so emptiness cannot stand for a refusal here
    // any more than it can in the bundle.
    std::vector<bool> refused;
    refused.reserve(keys.size());
    std::size_t refusals = 0;
    std::vector<std::string> refusalReasons;
    // Merged over the keys in order: the clip overload's list.
    openstrata::motion::RetargetDiagnostics execDiagnostics;
    std::size_t diagnosticsRefusals = 0;
    double execSeconds = 0.0;
    double diagnosticsSeconds = 0.0;
    for (const double key : keys)
    {
        const auto started = std::chrono::steady_clock::now();
        const execdriver::Frame valued = driver.Evaluate(request, UsdTimeCode(key));
        const auto between = std::chrono::steady_clock::now();
        const execdriver::Frame diagnosed = driver.Evaluate(diagnosticsRequest, UsdTimeCode(key));
        const auto finished = std::chrono::steady_clock::now();
        execSeconds += std::chrono::duration<double>(between - started).count();
        diagnosticsSeconds += std::chrono::duration<double>(finished - between).count();

        if (const auto* reported = diagnosed.Get<openstrata::motion::RetargetDiagnostics>(0))
        {
            execDiagnostics.Merge(*reported);
        }
        else
        {
            ++diagnosticsRefusals;
        }

        const auto* answer = valued.Get<openstrata::motion::JointLocalTransforms>(0);
        refused.push_back(answer == nullptr);
        if (!answer)
        {
            ++refusals;
            if (refusalReasons.empty())
            {
                // What the two frames said, the driver's own lines last.
                for (const execdriver::Frame* frame : {&valued, &diagnosed})
                {
                    refusalReasons.insert(refusalReasons.end(), frame->refusals.begin(),
                                          frame->refusals.end());
                    refusalReasons.insert(refusalReasons.end(), frame->errors.begin(),
                                          frame->errors.end());
                    for (const execdriver::OpenExecDiagnostic& d : frame->diagnostics.reported)
                    {
                        refusalReasons.push_back(execdriver::FormatOpenExecDiagnostic(d));
                    }
                }
            }
            answers.emplace_back();
        }
        else
        {
            answers.push_back(*answer);
        }
    }

    std::vector<std::string> execLines;
    for (const openstrata::motion::RetargetDiagnostic& d : execDiagnostics.reported)
    {
        execLines.push_back(openstrata::motion::FormatRetargetDiagnostic(d));
    }
    const bool diagnosticsCompared = !args.toolLog.empty();
    const bool diagnosticsAgree = diagnosticsRefusals == 0 && execLines == tool.library;

    // -- the comparison ------------------------------------------------------
    Tally rotations;
    Tally translations;
    Tally placement;
    Tally stamps;         // exec's timestamp against the tool's
    Tally naiveRotations; // the bake read at the clip's key instead
    Tally naiveTranslations;
    std::size_t jointsEqual = 0, jointsReordered = 0, jointsMissing = 0;
    std::size_t scalesEqual = 0, scalesDiffer = 0;
    std::size_t shapeMismatches = 0;
    std::size_t moving = 0;

    const std::vector<std::string> bakedJoints = [&]
    {
        std::vector<std::string> names;
        for (const TfToken& token : bake.joints)
        {
            names.push_back(token.GetString());
        }
        return names;
    }();

    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        const openstrata::motion::JointLocalTransforms& answer = answers[i];
        if (refused[i])
        {
            continue; // counted above
        }
        // An answer that came back with no joints under a bake that has some
        // is not skipped: it reaches the joint comparison below and is counted
        // there as missing, so a regression that answers empty instead of
        // refusing cannot pass by being compared with nothing.

        // The instant exec stamped the answer with, against the one the tool
        // stamped the same key with: `key / rate`, the clip stage's rate. exec
        // divides by `motion:timeCodesPerSecond` instead, and no array depends
        // on the stamp, so this is the only comparison that sees a clip whose
        // attribute disagrees with its stage -- or a sampler computing the
        // wrong second.
        {
            const double expected = keys[i] / clip.rate;
            const double seconds = std::abs(answer.timestamp - expected);
            Kind kind = Kind::Exact;
            if (answer.timestamp != expected)
            {
                // A NaN stamp is not within anything.
                kind = seconds <= kTolerance.time ? Kind::Rounding : Kind::Divergence;
            }
            std::ostringstream said;
            said.precision(17);
            said << "key " << keys[i] << ": exec " << answer.timestamp << " s, the tool "
                 << expected << " s";
            stamps.Add(kind, seconds, said.str());
        }

        // Each classification is taken before its amount is read: as one
        // call's two arguments the order would be unspecified, and MSVC reads
        // the amount first.
        double seconds = 0.0;
        std::ostringstream where;
        where.precision(17);
        where << "key " << keys[i] << " / bake " << bake.times[i];
        const Kind placed = ClassifyPlacement(keys[i], bake.times[i], clip.rate, &seconds);
        placement.Add(placed, seconds, where.str());

        if (answer.joints == bakedJoints)
        {
            ++jointsEqual;
        }
        else
        {
            std::vector<std::string> a = answer.joints, b = bakedJoints;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            ++(a == b ? jointsReordered : jointsMissing);
            continue; // arrays indexed by two different orders
        }
        if (answer.scales.size() == bake.scales.size() &&
            std::equal(answer.scales.begin(), answer.scales.end(), bake.scales.begin()))
        {
            ++scalesEqual;
        }
        else
        {
            ++scalesDiffer;
        }

        VtQuatfArray bakedRotations, naiveR;
        VtVec3fArray bakedTranslations, naiveT;
        bake.rotations.Get(&bakedRotations, UsdTimeCode(bake.times[i]));
        bake.translations.Get(&bakedTranslations, UsdTimeCode(bake.times[i]));
        bake.rotations.Get(&naiveR, UsdTimeCode(keys[i]));
        bake.translations.Get(&naiveT, UsdTimeCode(keys[i]));
        const std::size_t n = answer.joints.size();
        if (answer.rotations.size() != n || answer.translations.size() != n ||
            bakedRotations.size() != n || bakedTranslations.size() != n || naiveR.size() != n ||
            naiveT.size() != n)
        {
            ++shapeMismatches;
            continue;
        }

        if (i > 0 && !answers[0].rotations.empty() &&
            (answer.rotations != answers[0].rotations ||
             answer.translations != answers[0].translations))
        {
            ++moving;
        }

        for (std::size_t j = 0; j < n; ++j)
        {
            std::ostringstream at;
            at << "key " << keys[i] << ", " << answer.joints[j];
            double amount = 0.0;
            {
                const Kind kind = ClassifyRotation(answer.rotations[j], bakedRotations[j], &amount);
                std::ostringstream said;
                said << at.str() << ": exec " << answer.rotations[j] << ", bake "
                     << bakedRotations[j] << " (" << amount << " rad)";
                rotations.Add(kind, amount, said.str());
            }
            {
                const Kind kind =
                    ClassifyTranslation(answer.translations[j], bakedTranslations[j], &amount);
                std::ostringstream said;
                said << at.str() << ": exec " << answer.translations[j] << ", bake "
                     << bakedTranslations[j] << " (" << amount << " m)";
                translations.Add(kind, amount, said.str());
            }
            {
                const Kind kind = ClassifyRotation(answer.rotations[j], naiveR[j], &amount);
                naiveRotations.Add(kind, amount, at.str());
            }
            {
                const Kind kind = ClassifyTranslation(answer.translations[j], naiveT[j], &amount);
                naiveTranslations.Add(kind, amount, at.str());
            }
        }
    }

    // -- the report ----------------------------------------------------------
    const std::size_t jointCount = bakedJoints.size();
    std::printf("exec_parity: %zu samples x %zu joints at %g per second\n", keys.size(), jointCount,
                clip.rate);
    for (const std::string& line : parity.authored)
    {
        std::printf("  stated: %s\n", line.c_str());
    }
    std::printf("  exec: %zu refusal(s), %.3f s for %zu evaluations\n", refusals, execSeconds,
                keys.size());
    std::printf("  joints: %zu equal, %zu reordered, %zu missing; scales %zu "
                "equal, %zu differ\n",
                jointsEqual, jointsReordered, jointsMissing, scalesEqual, scalesDiffer);
    PrintTally("rotations", rotations, "rad");
    PrintTally("translations", translations, "m");
    PrintTally("placement", placement, "s");
    PrintTally("timestamps", stamps, "s");
    std::printf("  read at the clip's key instead of the bake's sample:\n");
    PrintTally("rotations", naiveRotations, "rad");
    PrintTally("translations", naiveTranslations, "m");
    for (const std::string& reason : refusalReasons)
    {
        std::printf("  refused: %s\n", reason.c_str());
    }
    // What the driver raised about the requests themselves, over the run. A
    // bundle missing from the session is the likely one, and it is an error
    // whatever the values did.
    std::vector<std::string> driverLines;
    for (const execdriver::OpenExecDiagnostic& d : driver.Reported().reported)
    {
        driverLines.push_back(execdriver::FormatOpenExecDiagnostic(d));
        std::printf("  driver: %s\n", driverLines.back().c_str());
    }

    // The diagnostics: what exec answered, and against the tool when it was
    // handed the tool's log.
    const std::vector<std::string> execOnly = Missing(execLines, tool.library);
    const std::vector<std::string> toolOnly = Missing(tool.library, execLines);
    std::printf("  diagnostics: exec %zu, %.3f s for %zu evaluations", execLines.size(),
                diagnosticsSeconds, keys.size());
    if (diagnosticsCompared)
    {
        std::printf("; the tool %zu (+%zu a caller raises): %s\n", tool.library.size(),
                    tool.caller.size(),
                    diagnosticsAgree ? "the same lines, in order" : "they DIFFER");
    }
    else
    {
        std::printf("; not compared, no --tool-log\n");
    }
    if (diagnosticsRefusals != 0)
    {
        std::printf("    exec refused to diagnose %zu sample(s)\n", diagnosticsRefusals);
    }
    for (const std::string& line : execLines)
    {
        std::printf("    exec: %s\n", line.c_str());
    }
    for (const std::string& line : toolOnly)
    {
        std::printf("    only the tool: %s\n", line.c_str());
    }
    if (diagnosticsCompared && execOnly.empty() && toolOnly.empty() && !diagnosticsAgree &&
        diagnosticsRefusals == 0)
    {
        std::printf("    the same lines in a different order\n");
    }

    JsObject report;
    report["samples"] = JsValue(static_cast<int64_t>(keys.size()));
    report["joints"] = JsValue(static_cast<int64_t>(jointCount));
    report["rate"] = JsValue(clip.rate);
    report["moving_samples"] = JsValue(static_cast<int64_t>(moving));
    report["exec_refusals"] = JsValue(static_cast<int64_t>(refusals));
    report["exec_seconds"] = JsValue(execSeconds);
    {
        JsObject diagnostics;
        diagnostics["compared"] = JsValue(diagnosticsCompared);
        diagnostics["agree"] = JsValue(diagnosticsCompared && diagnosticsAgree);
        diagnostics["exec_refusals"] = JsValue(static_cast<int64_t>(diagnosticsRefusals));
        diagnostics["seconds"] = JsValue(diagnosticsSeconds);
        diagnostics["exec"] = JsValue(JsLines(execLines));
        diagnostics["tool"] = JsValue(JsLines(tool.library));
        diagnostics["tool_caller_raised"] = JsValue(JsLines(tool.caller));
        diagnostics["exec_only"] = JsValue(JsLines(execOnly));
        diagnostics["tool_only"] = JsValue(JsLines(toolOnly));
        report["diagnostics"] = JsValue(diagnostics);
        report["arming_errors"] = JsValue(JsLines(armingErrors));
        report["openexec_diagnostics"] = JsValue(JsLines(driverLines));
    }
    {
        JsArray reasons;
        for (const std::string& reason : refusalReasons)
        {
            reasons.emplace_back(reason);
        }
        report["refusal_reasons"] = JsValue(reasons);
        JsArray stated;
        for (const std::string& line : parity.authored)
        {
            stated.emplace_back(line);
        }
        report["stated"] = JsValue(stated);
        JsObject warnings;
        for (const auto& [text, count] : diagnostics.Warnings())
        {
            warnings[text] = JsValue(static_cast<int64_t>(count));
        }
        report["exec_warnings"] = JsValue(warnings);
    }
    {
        JsObject joints;
        joints["equal"] = JsValue(static_cast<int64_t>(jointsEqual));
        joints["reordered"] = JsValue(static_cast<int64_t>(jointsReordered));
        joints["missing"] = JsValue(static_cast<int64_t>(jointsMissing));
        report["joint_order"] = JsValue(joints);
        JsObject scales;
        scales["equal"] = JsValue(static_cast<int64_t>(scalesEqual));
        scales["differ"] = JsValue(static_cast<int64_t>(scalesDiffer));
        report["scales"] = JsValue(scales);
    }
    report["shape_mismatches"] = JsValue(static_cast<int64_t>(shapeMismatches));
    report["rotations"] = JsValue(TallyJson(rotations, "radians"));
    report["translations"] = JsValue(TallyJson(translations, "meters"));
    report["placement"] = JsValue(TallyJson(placement, "seconds"));
    report["timestamps"] = JsValue(TallyJson(stamps, "seconds"));
    {
        JsObject naive;
        naive["rotations"] = JsValue(TallyJson(naiveRotations, "radians"));
        naive["translations"] = JsValue(TallyJson(naiveTranslations, "meters"));
        report["read_at_clip_keys"] = JsValue(naive);
    }
    {
        // Read last, after every evaluation, so a plugin loaded lazily by the
        // final compute is counted.
        report["loaded_plugins"] = JsValue(LoadedPlugins());
        JsArray modules;
        for (const std::string& path : LoadedModules())
        {
            modules.emplace_back(path);
        }
        report["loaded_modules"] = JsValue(modules);
    }
    {
        std::ofstream out(args.report);
        out << JsWriteToString(JsValue(report)) << "\n";
        if (!out)
        {
            std::fprintf(stderr, "exec_parity: could not write %s\n", args.report.c_str());
            return 1;
        }
    }

    const bool failed = refusals != 0 || jointsReordered != 0 || jointsMissing != 0 ||
                        scalesDiffer != 0 || shapeMismatches != 0 ||
                        rotations.Of(Kind::Divergence) != 0 ||
                        translations.Of(Kind::Divergence) != 0 ||
                        placement.Of(Kind::Divergence) != 0 || stamps.Of(Kind::Divergence) != 0 ||
                        (diagnosticsCompared && !diagnosticsAgree) || driver.Reported().HasError();
    std::puts(failed ? "exec_parity: DIVERGED" : "exec_parity: parity holds");
    return failed ? 1 : 0;
}
