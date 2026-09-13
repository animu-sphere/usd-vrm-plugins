// SPDX-License-Identifier: Apache-2.0
//
// exec_parity -- the OpenExec plan's P0-6 harness: one `motion_retarget` bake
// against the same avatar and clip evaluated through `execMotion` + `execVrm`,
// sample by sample.
//
//     exec_parity --avatar A --animation C --bake B --report R.json
//                 [motion_retarget's mapping and root-motion flags]
//
// The bake is the tool's output for the same `--avatar`, `--animation` and
// flags; this program does not run the tool. The driver beside it does, so
// that the two are handed one argument list and cannot drift apart.
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
// quaternion's sign only, within `motion::MotionTolerance` -- the contract's
// tolerance, not one chosen here -- or a divergence. Only a divergence, a
// refusal, or two values that do not have the same shape fail the run.
//
// # What it links
//
// OpenUSD and exec, `vrmRetarget` for the result type, and `motionCore` for
// the bone vocabulary and `AngleBetween`. Neither plugin: both are found
// through `PXR_PLUGINPATH_NAME`, the path a packaged bundle takes.

#include "pxr/pxr.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"
#include "pxr/base/tf/diagnosticMgr.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/status.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/warning.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/types.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"

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

#include <motionCore/Compare.h>
#include <motionCore/Humanoid.h>
#include <vrmRetarget/PoseRetargeter.h>

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

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const TfToken kJointTransforms("vrm.computeJointLocalTransforms");
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
bool Parse(int argc, char** argv, Arguments* out, std::string* error)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& flag = args[i];
        auto value = [&](std::string* into) {
            if (i + 1 >= args.size()) {
                *error = flag + " requires a value";
                return false;
            }
            *into = args[++i];
            return true;
        };
        std::string text;
        if (flag == "--avatar") {
            if (!value(&out->avatar)) return false;
        } else if (flag == "--animation") {
            if (!value(&out->animation)) return false;
        } else if (flag == "--bake") {
            if (!value(&out->bake)) return false;
        } else if (flag == "--report") {
            if (!value(&out->report)) return false;
        } else if (flag == "--humanoid-map") {
            if (!value(&out->humanoidMap)) return false;
        } else if (flag == "--skeleton") {
            if (!value(&out->skeleton)) return false;
        } else if (flag == "--clip-skeleton") {
            if (!value(&out->clipSkeleton)) return false;
        } else if (flag == "--root-motion") {
            if (!value(&text)) return false;
            out->rootMotion = text;
        } else if (flag == "--root-joint") {
            if (!value(&text)) return false;
            out->rootJoint = text;
        } else if (flag == "--translation-scale") {
            if (!value(&text)) return false;
            try {
                std::size_t used = 0;
                // Parsed as the tool parses it -- a double, then narrowed --
                // so the statement is the float the tool's option holds.
                const double scale = std::stod(text, &used);
                if (used != text.size()) {
                    throw std::invalid_argument(text);
                }
                out->translationScale = static_cast<float>(scale);
            } catch (const std::exception&) {
                *error = "--translation-scale expects a number, got '" + text + "'";
                return false;
            }
        } else if (flag == "--preserve-target-height") {
            out->preserveTargetHeight = true;
        } else if (flag == "--no-look-at" || flag == "--no-expressions"
                   || flag == "--quiet") {
            // Narrow the tool towards exec; nothing to state.
        } else if (flag == "--animation-name") {
            if (!value(&text)) return false;
        } else if (flag == "--resample") {
            *error = "--resample moves the bake onto instants the clip never "
                     "keyed, and exec answers a clip's own keys; a parity run "
                     "does not take it";
            return false;
        } else {
            *error = "unknown argument '" + flag + "'";
            return false;
        }
    }
    if (out->avatar.empty() || out->animation.empty() || out->bake.empty()
        || out->report.empty()) {
        *error = "--avatar, --animation, --bake and --report are required";
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
    Diagnostics() { TfDiagnosticMgr::GetInstance().AddDelegate(this); }
    ~Diagnostics() override
    {
        TfDiagnosticMgr::GetInstance().RemoveDelegate(this);
    }

    void IssueError(const TfError&) override {}
    void IssueFatalError(const TfCallContext&, const std::string& message) override
    {
        std::fprintf(stderr, "fatal: %s\n", message.c_str());
    }
    void IssueStatus(const TfStatus&) override {}
    void IssueWarning(const TfWarning& warning) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ++_warnings[warning.GetCommentary()];
    }

    std::map<std::string, int> Warnings()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _warnings;
    }

private:
    std::mutex _mutex;
    std::map<std::string, int> _warnings;
};

std::vector<std::string> ErrorsIn(const TfErrorMark& mark)
{
    std::vector<std::string> errors;
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it) {
        errors.push_back(it->GetCommentary());
    }
    return errors;
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
    SdfPath humanoid;  // empty when no prim states vrm:humanBones:hips
    SdfPath skeleton;
};

struct ClipShape
{
    UsdStageRefPtr stage;
    double rate = 0.0;
    SdfPath skeleton;
    SdfPath animation;
};

SdfPath FirstSkeleton(const UsdStageRefPtr& stage)
{
    for (const UsdPrim& prim : stage->Traverse()) {
        if (prim.IsA<UsdSkelSkeleton>()) {
            return prim.GetPath();
        }
    }
    return SdfPath();
}

bool DiscoverAvatar(const Arguments& args, AvatarShape* avatar,
                    std::string* error)
{
    avatar->stage = UsdStage::Open(args.avatar);
    if (!avatar->stage) {
        *error = "could not open the avatar " + args.avatar;
        return false;
    }
    // The tool's humanoid is the first prim stating a hips binding -- by
    // attribute, not by schema. exec's is a prim with the schema applied; the
    // two are reconciled below rather than assumed to agree.
    for (const UsdPrim& prim : avatar->stage->Traverse()) {
        if (prim.HasAttribute(kHumanBonesHips)) {
            avatar->humanoid = prim.GetPath();
            break;
        }
    }
    if (!args.skeleton.empty()) {
        avatar->skeleton = SdfPath(args.skeleton);
    } else if (!avatar->humanoid.IsEmpty()) {
        SdfPathVector targets;
        const UsdRelationship rel =
            avatar->stage->GetPrimAtPath(avatar->humanoid)
                .GetRelationship(kSkeletonRel);
        if (rel && rel.GetTargets(&targets) && !targets.empty()) {
            avatar->skeleton = targets.front();
        }
    }
    if (avatar->skeleton.IsEmpty()) {
        avatar->skeleton = FirstSkeleton(avatar->stage);
    }
    if (avatar->skeleton.IsEmpty()
        || !UsdSkelSkeleton(avatar->stage->GetPrimAtPath(avatar->skeleton))) {
        *error = "the avatar has no UsdSkelSkeleton the tool would retarget onto";
        return false;
    }
    return true;
}

bool DiscoverClip(const Arguments& args, ClipShape* clip, std::string* error)
{
    clip->stage = UsdStage::Open(args.animation);
    if (!clip->stage) {
        *error = "could not open the clip " + args.animation;
        return false;
    }
    clip->rate = clip->stage->GetTimeCodesPerSecond();
    if (clip->rate <= 0.0) {
        clip->rate = 30.0;  // the tool's fallback
    }
    clip->skeleton = args.clipSkeleton.empty() ? FirstSkeleton(clip->stage)
                                               : SdfPath(args.clipSkeleton);
    const UsdPrim skeleton = clip->stage->GetPrimAtPath(clip->skeleton);
    if (!UsdSkelSkeleton(skeleton)) {
        *error = "the clip has no UsdSkelSkeleton";
        return false;
    }
    UsdPrim animation;
    if (!UsdSkelBindingAPI(skeleton).GetAnimationSource(&animation)
        || !animation) {
        // The tool falls back to "the one SkelAnimation on the stage". exec
        // follows a binding or nothing, so that is a stage only one of the two
        // reads -- a P0-6 row, not something to paper over here.
        *error = "the clip's skeleton <" + clip->skeleton.GetString()
            + "> binds no animation; motion_retarget would fall back to the "
              "stage's only SkelAnimation and exec cannot";
        return false;
    }
    clip->animation = animation.GetPath();
    return true;
}

std::set<std::string> RootPrimNames(const UsdStageRefPtr& stage)
{
    std::set<std::string> names;
    for (const UsdPrim& prim : stage->GetPseudoRoot().GetAllChildren()) {
        names.insert(prim.GetName().GetString());
    }
    return names;
}

// --humanoid-map, parsed as the tool parses it: an object of human bone name
// to joint token, and a key that is not a bone of the vocabulary is refused.
bool ReadMap(const std::string& path,
             std::vector<std::pair<std::string, std::string>>* entries,
             std::string* error)
{
    std::ifstream file(path);
    if (!file) {
        *error = "could not open " + path;
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    JsParseError parseError;
    const JsValue parsed = JsParseString(text.str(), &parseError);
    if (!parsed.IsObject()) {
        *error = path + " is not a JSON object";
        return false;
    }
    for (const auto& [bone, token] : parsed.GetJsObject()) {
        if (!token.IsString() || !motion::FindHumanBone(bone)) {
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
    std::vector<std::string> authored;  // what the harness stated, for the report
};

bool ComposeParityStage(const Arguments& args, const AvatarShape& avatar,
                        const ClipShape& clip, ParityStage* parity,
                        std::string* error)
{
    const std::set<std::string> avatarRoots = RootPrimNames(avatar.stage);
    for (const std::string& name : RootPrimNames(clip.stage)) {
        if (avatarRoots.count(name) || name == "Parity") {
            *error = "the avatar and the clip both have a root prim named '"
                + name + "', so sublayering them would compose one prim "
                  "neither file describes";
            return false;
        }
    }
    if (avatarRoots.count("Parity")) {
        *error = "the avatar has a root prim named 'Parity', which this "
                 "harness reserves for what it states";
        return false;
    }

    SdfLayerRefPtr root = SdfLayer::CreateAnonymous("parity.usda");
    root->SetSubLayerPaths({avatar.stage->GetRootLayer()->GetIdentifier(),
                            clip.stage->GetRootLayer()->GetIdentifier()});
    root->SetTimeCodesPerSecond(clip.rate);
    parity->stage = UsdStage::Open(root);
    if (!parity->stage) {
        *error = "could not open the parity stage";
        return false;
    }
    UsdStageRefPtr stage = parity->stage;

    // The humanoid. The avatar's own when the tool found one, and then it has
    // to carry the schema exec computes on -- a prim stating the bindings
    // without it is read by the tool and invisible to exec (the humanoid
    // report, §7), which is a divergence of input, not of evaluation.
    std::vector<std::pair<std::string, std::string>> map;
    if (!args.humanoidMap.empty() && !ReadMap(args.humanoidMap, &map, error)) {
        return false;
    }
    if (!avatar.humanoid.IsEmpty()) {
        parity->humanoid = stage->GetPrimAtPath(avatar.humanoid);
        const TfTokenVector applied = parity->humanoid.GetAppliedSchemas();
        if (std::find(applied.begin(), applied.end(), kHumanoidApi)
            == applied.end()) {
            *error = "<" + avatar.humanoid.GetString()
                + "> states human bones without VrmHumanoidAPI applied; the "
                  "tool reads it and exec cannot";
            return false;
        }
    } else if (!map.empty()) {
        parity->humanoid =
            UsdGeomScope::Define(stage, kParityHumanoid).GetPrim();
        parity->humanoid.AddAppliedSchema(kHumanoidApi);
        parity->authored.push_back("defined <" + kParityHumanoid.GetString()
                                   + "> with VrmHumanoidAPI");
    } else {
        *error = "the avatar states no humanoid and no --humanoid-map was "
                 "given; the tool refuses that too";
        return false;
    }

    // `vrm:skeleton` is stated when the avatar did not, or when --skeleton
    // chose another rig -- the tool's precedence, with the flag first.
    SdfPathVector named;
    const UsdRelationship skeletonRel =
        parity->humanoid.GetRelationship(kSkeletonRel);
    if (!skeletonRel || !skeletonRel.GetTargets(&named) || named.empty()
        || named.front() != avatar.skeleton) {
        parity->humanoid.CreateRelationship(kSkeletonRel, /*custom*/ false)
            .SetTargets({avatar.skeleton});
        parity->authored.push_back("vrm:skeleton = <"
                                   + avatar.skeleton.GetString() + ">");
    }

    // The map, over whatever the avatar stated -- the tool merges the file
    // over the stage the same way.
    for (const auto& [bone, token] : map) {
        parity->humanoid
            .CreateAttribute(TfToken(kHumanBonesPrefix + bone),
                             SdfValueTypeNames->Token, /*custom*/ false,
                             SdfVariabilityUniform)
            .Set(TfToken(token));
    }
    if (!map.empty()) {
        parity->authored.push_back(std::to_string(map.size())
                                   + " vrm:humanBones:* from --humanoid-map");
    }

    parity->humanoid.CreateRelationship(kSourceSkeleton, /*custom*/ true)
        .SetTargets({clip.skeleton});
    parity->authored.push_back("vrm:retarget:sourceSkeleton = <"
                               + clip.skeleton.GetString() + ">");

    // The four statements, only for the flags the tool was given: an absent
    // statement keeps the library's default, which is what an absent flag
    // gives the tool.
    if (args.rootMotion) {
        parity->humanoid
            .CreateAttribute(kRootMotion, SdfValueTypeNames->Token, true)
            .Set(TfToken(*args.rootMotion));
        parity->authored.push_back("vrm:retarget:rootMotion = "
                                   + *args.rootMotion);
    }
    if (args.rootJoint) {
        parity->humanoid
            .CreateAttribute(kRootJoint, SdfValueTypeNames->Token, true)
            .Set(TfToken(*args.rootJoint));
        parity->authored.push_back("vrm:retarget:rootJoint = "
                                   + *args.rootJoint);
    }
    if (args.translationScale) {
        parity->humanoid
            .CreateAttribute(kTranslationScale, SdfValueTypeNames->Float, true)
            .Set(*args.translationScale);
        parity->authored.push_back("vrm:retarget:translationScale = "
                                   + std::to_string(*args.translationScale));
    }
    if (args.preserveTargetHeight) {
        parity->humanoid
            .CreateAttribute(kPreserveTargetHeight, SdfValueTypeNames->Bool, true)
            .Set(true);
        parity->authored.push_back("vrm:retarget:preserveTargetHeight = true");
    }

    // The rate, on the clip's animation. Only when the clip states none: one
    // that does is the clip's own statement, and if it disagrees with the
    // stage's the two implementations are handed two clocks, which the
    // placement comparison will show.
    const UsdPrim animation = stage->GetPrimAtPath(clip.animation);
    if (!animation.GetAttribute(kClipRate).HasAuthoredValue()) {
        animation.CreateAttribute(kClipRate, SdfValueTypeNames->Double, true)
            .Set(clip.rate);
        std::ostringstream said;
        said << "motion:timeCodesPerSecond = " << clip.rate << " on <"
             << clip.animation.GetString() << ">";
        parity->authored.push_back(said.str());
    }
    return true;
}

// The clip's keys, as exec's sampler sees them: the union of the animation's
// rotation and translation time samples on the parity stage. The tool's union
// adds a clip's expression and gaze keys; a clip with either is refused below
// when the counts disagree, rather than paired by guesswork.
std::vector<double> ClipKeys(const ParityStage& parity, const ClipShape& clip)
{
    const UsdSkelAnimation animation(
        parity.stage->GetPrimAtPath(clip.animation));
    std::vector<double> rotations;
    std::vector<double> translations;
    animation.GetRotationsAttr().GetTimeSamples(&rotations);
    animation.GetTranslationsAttr().GetTimeSamples(&translations);
    std::set<double> keys(rotations.begin(), rotations.end());
    keys.insert(translations.begin(), translations.end());
    if (keys.empty()) {
        keys.insert(clip.stage->GetStartTimeCode());  // the tool's rule
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

bool ReadBake(const std::string& path, const SdfPath& skeleton, Bake* bake,
              std::string* error)
{
    bake->stage = UsdStage::Open(path);
    if (!bake->stage) {
        *error = "could not open the bake " + path;
        return false;
    }
    UsdPrim animationPrim;
    if (!UsdSkelBindingAPI(bake->stage->GetPrimAtPath(skeleton))
             .GetAnimationSource(&animationPrim)
        || !animationPrim) {
        *error = "the bake binds no animation to <" + skeleton.GetString()
            + ">, the skeleton exec's humanoid names";
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
    if (rotationTimes != translationTimes) {
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
enum class Kind { Exact, Sign, Rounding, Divergence };

const char* Name(Kind kind)
{
    switch (kind) {
    case Kind::Exact: return "exact";
    case Kind::Sign: return "sign";
    case Kind::Rounding: return "rounding";
    case Kind::Divergence: return "divergence";
    }
    return "?";
}

struct Tally
{
    std::map<Kind, std::size_t> counts;
    double worst = 0.0;
    std::string firstDivergence;

    void Add(Kind kind, double amount, const std::string& where)
    {
        ++counts[kind];
        if (kind != Kind::Exact && kind != Kind::Sign) {
            worst = std::max(worst, amount);
        }
        if (kind == Kind::Divergence && firstDivergence.empty()) {
            firstDivergence = where;
        }
    }
    std::size_t Of(Kind kind) const
    {
        const auto found = counts.find(kind);
        return found == counts.end() ? 0 : found->second;
    }
};

const motion::MotionTolerance kTolerance{};

Kind ClassifyRotation(const GfQuatf& exec, const GfQuatf& baked, double* angle)
{
    *angle = 0.0;
    if (exec == baked) {
        return Kind::Exact;
    }
    if (exec == -baked) {
        return Kind::Sign;
    }
    *angle = motion::AngleBetween(exec, baked);
    // A NaN angle is not within anything.
    return *angle <= kTolerance.angle ? Kind::Rounding : Kind::Divergence;
}

Kind ClassifyTranslation(const GfVec3f& exec, const GfVec3f& baked,
                         double* distance)
{
    *distance = 0.0;
    if (exec == baked) {
        return Kind::Exact;
    }
    *distance = (exec - baked).GetLength();
    return *distance <= kTolerance.distance ? Kind::Rounding : Kind::Divergence;
}

// Where the bake put a sample against the key exec evaluated, in seconds: the
// same instant, a rounding of it within the contract's time quantum, or
// another instant.
Kind ClassifyPlacement(double key, double bakedTime, double rate,
                       double* seconds)
{
    *seconds = std::abs(bakedTime - key) / rate;
    if (bakedTime == key) {
        return Kind::Exact;
    }
    return *seconds <= kTolerance.time ? Kind::Rounding : Kind::Divergence;
}

JsObject TallyJson(const Tally& tally, const char* unit)
{
    JsObject out;
    for (const Kind kind :
         {Kind::Exact, Kind::Sign, Kind::Rounding, Kind::Divergence}) {
        out[Name(kind)] = JsValue(static_cast<int64_t>(tally.Of(kind)));
    }
    out[std::string("worst_") + unit] = JsValue(tally.worst);
    if (!tally.firstDivergence.empty()) {
        out["first_divergence"] = JsValue(tally.firstDivergence);
    }
    return out;
}

void PrintTally(const char* what, const Tally& tally, const char* unit)
{
    std::printf("  %-13s exact %zu  sign %zu  rounding %zu  divergence %zu"
                "  (worst %.3g %s)\n",
                what, tally.Of(Kind::Exact), tally.Of(Kind::Sign),
                tally.Of(Kind::Rounding), tally.Of(Kind::Divergence),
                tally.worst, unit);
    if (!tally.firstDivergence.empty()) {
        std::printf("    first divergence: %s\n",
                    tally.firstDivergence.c_str());
    }
}

}  // namespace

int main(int argc, char** argv)
{
    Arguments args;
    std::string error;
    if (!Parse(argc, argv, &args, &error)) {
        std::fprintf(stderr, "exec_parity: %s\n", error.c_str());
        return 2;
    }

    Diagnostics diagnostics;

    AvatarShape avatar;
    ClipShape clip;
    ParityStage parity;
    if (!DiscoverAvatar(args, &avatar, &error)
        || !DiscoverClip(args, &clip, &error)
        || !ComposeParityStage(args, avatar, clip, &parity, &error)) {
        std::fprintf(stderr, "exec_parity: %s\n", error.c_str());
        return 1;
    }

    Bake bake;
    if (!ReadBake(args.bake, avatar.skeleton, &bake, &error)) {
        std::fprintf(stderr, "exec_parity: %s\n", error.c_str());
        return 1;
    }

    const std::vector<double> keys = ClipKeys(parity, clip);
    if (keys.size() != bake.times.size()) {
        std::fprintf(stderr,
                     "exec_parity: the clip's body is keyed at %zu instants "
                     "and the bake has %zu samples; this harness pairs a "
                     "sample with a body key, and a clip keying expressions "
                     "or a gaze between them is not paired by guesswork\n",
                     keys.size(), bake.times.size());
        return 1;
    }

    // -- exec, one request armed once and moved through the keys ------------
    ExecUsdSystem system(parity.stage);
    ExecUsdRequest request = system.BuildRequest(
        {ExecUsdValueKey(parity.humanoid, kJointTransforms)});
    {
        // The first compute arms the request, at the default time code, where
        // the retarget refuses by design (the retarget report, §5).
        TfErrorMark mark;
        system.Compute(request);
        mark.Clear();
    }

    std::vector<vrmRetarget::JointLocalTransforms> answers;
    answers.reserve(keys.size());
    std::size_t refusals = 0;
    std::vector<std::string> refusalReasons;
    const auto started = std::chrono::steady_clock::now();
    for (const double key : keys) {
        TfErrorMark mark;
        system.ChangeTime(UsdTimeCode(key));
        const VtValue value = system.Compute(request).Get(0);
        if (value.IsEmpty()
            || !value.IsHolding<vrmRetarget::JointLocalTransforms>()) {
            ++refusals;
            if (refusalReasons.empty()) {
                refusalReasons = ErrorsIn(mark);
            }
            answers.emplace_back();
        } else {
            answers.push_back(
                value.UncheckedGet<vrmRetarget::JointLocalTransforms>());
        }
        mark.Clear();
    }
    const double execSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();

    // -- the comparison ------------------------------------------------------
    Tally rotations;
    Tally translations;
    Tally placement;
    Tally naiveRotations;     // the bake read at the clip's key instead
    Tally naiveTranslations;
    std::size_t jointsEqual = 0, jointsReordered = 0, jointsMissing = 0;
    std::size_t scalesEqual = 0, scalesDiffer = 0;
    std::size_t shapeMismatches = 0;
    std::size_t moving = 0;

    const std::vector<std::string> bakedJoints = [&] {
        std::vector<std::string> names;
        for (const TfToken& token : bake.joints) {
            names.push_back(token.GetString());
        }
        return names;
    }();

    for (std::size_t i = 0; i < keys.size(); ++i) {
        const vrmRetarget::JointLocalTransforms& answer = answers[i];
        if (answer.joints.empty() && !bakedJoints.empty()) {
            continue;  // a refusal, counted above
        }

        // Each classification is taken before its amount is read: as one
        // call's two arguments the order would be unspecified, and MSVC reads
        // the amount first.
        double seconds = 0.0;
        std::ostringstream where;
        where.precision(17);
        where << "key " << keys[i] << " / bake " << bake.times[i];
        const Kind placed =
            ClassifyPlacement(keys[i], bake.times[i], clip.rate, &seconds);
        placement.Add(placed, seconds, where.str());

        if (answer.joints == bakedJoints) {
            ++jointsEqual;
        } else {
            std::vector<std::string> a = answer.joints, b = bakedJoints;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            ++(a == b ? jointsReordered : jointsMissing);
            continue;  // arrays indexed by two different orders
        }
        if (answer.scales.size() == bake.scales.size()
            && std::equal(answer.scales.begin(), answer.scales.end(),
                          bake.scales.begin())) {
            ++scalesEqual;
        } else {
            ++scalesDiffer;
        }

        VtQuatfArray bakedRotations, naiveR;
        VtVec3fArray bakedTranslations, naiveT;
        bake.rotations.Get(&bakedRotations, UsdTimeCode(bake.times[i]));
        bake.translations.Get(&bakedTranslations, UsdTimeCode(bake.times[i]));
        bake.rotations.Get(&naiveR, UsdTimeCode(keys[i]));
        bake.translations.Get(&naiveT, UsdTimeCode(keys[i]));
        const std::size_t n = answer.joints.size();
        if (answer.rotations.size() != n || answer.translations.size() != n
            || bakedRotations.size() != n || bakedTranslations.size() != n
            || naiveR.size() != n || naiveT.size() != n) {
            ++shapeMismatches;
            continue;
        }

        if (i > 0 && !answers[0].rotations.empty()
            && (answer.rotations != answers[0].rotations
                || answer.translations != answers[0].translations)) {
            ++moving;
        }

        for (std::size_t j = 0; j < n; ++j) {
            std::ostringstream at;
            at << "key " << keys[i] << ", " << answer.joints[j];
            double amount = 0.0;
            {
                const Kind kind = ClassifyRotation(answer.rotations[j],
                                                   bakedRotations[j], &amount);
                std::ostringstream said;
                said << at.str() << ": exec " << answer.rotations[j]
                     << ", bake " << bakedRotations[j] << " (" << amount
                     << " rad)";
                rotations.Add(kind, amount, said.str());
            }
            {
                const Kind kind = ClassifyTranslation(
                    answer.translations[j], bakedTranslations[j], &amount);
                std::ostringstream said;
                said << at.str() << ": exec " << answer.translations[j]
                     << ", bake " << bakedTranslations[j] << " (" << amount
                     << " m)";
                translations.Add(kind, amount, said.str());
            }
            {
                const Kind kind =
                    ClassifyRotation(answer.rotations[j], naiveR[j], &amount);
                naiveRotations.Add(kind, amount, at.str());
            }
            {
                const Kind kind = ClassifyTranslation(answer.translations[j],
                                                      naiveT[j], &amount);
                naiveTranslations.Add(kind, amount, at.str());
            }
        }
    }

    // -- the report ----------------------------------------------------------
    const std::size_t jointCount = bakedJoints.size();
    std::printf("exec_parity: %zu samples x %zu joints at %g per second\n",
                keys.size(), jointCount, clip.rate);
    for (const std::string& line : parity.authored) {
        std::printf("  stated: %s\n", line.c_str());
    }
    std::printf("  exec: %zu refusal(s), %.3f s for %zu evaluations\n",
                refusals, execSeconds, keys.size());
    std::printf("  joints: %zu equal, %zu reordered, %zu missing; scales %zu "
                "equal, %zu differ\n",
                jointsEqual, jointsReordered, jointsMissing, scalesEqual,
                scalesDiffer);
    PrintTally("rotations", rotations, "rad");
    PrintTally("translations", translations, "m");
    PrintTally("placement", placement, "s");
    std::printf("  read at the clip's key instead of the bake's sample:\n");
    PrintTally("rotations", naiveRotations, "rad");
    PrintTally("translations", naiveTranslations, "m");
    for (const std::string& reason : refusalReasons) {
        std::printf("  refused: %s\n", reason.c_str());
    }

    JsObject report;
    report["samples"] = JsValue(static_cast<int64_t>(keys.size()));
    report["joints"] = JsValue(static_cast<int64_t>(jointCount));
    report["rate"] = JsValue(clip.rate);
    report["moving_samples"] = JsValue(static_cast<int64_t>(moving));
    report["exec_refusals"] = JsValue(static_cast<int64_t>(refusals));
    report["exec_seconds"] = JsValue(execSeconds);
    {
        JsArray reasons;
        for (const std::string& reason : refusalReasons) {
            reasons.emplace_back(reason);
        }
        report["refusal_reasons"] = JsValue(reasons);
        JsArray stated;
        for (const std::string& line : parity.authored) {
            stated.emplace_back(line);
        }
        report["stated"] = JsValue(stated);
        JsObject warnings;
        for (const auto& [text, count] : diagnostics.Warnings()) {
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
    {
        JsObject naive;
        naive["rotations"] = JsValue(TallyJson(naiveRotations, "radians"));
        naive["translations"] = JsValue(TallyJson(naiveTranslations, "meters"));
        report["read_at_clip_keys"] = JsValue(naive);
    }
    {
        std::ofstream out(args.report);
        out << JsWriteToString(JsValue(report)) << "\n";
        if (!out) {
            std::fprintf(stderr, "exec_parity: could not write %s\n",
                         args.report.c_str());
            return 1;
        }
    }

    const bool failed = refusals != 0 || jointsReordered != 0
        || jointsMissing != 0 || scalesDiffer != 0 || shapeMismatches != 0
        || rotations.Of(Kind::Divergence) != 0
        || translations.Of(Kind::Divergence) != 0
        || placement.Of(Kind::Divergence) != 0;
    std::puts(failed ? "exec_parity: DIVERGED" : "exec_parity: parity holds");
    return failed ? 1 : 0;
}
