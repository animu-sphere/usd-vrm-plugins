// SPDX-License-Identifier: Apache-2.0
//
// ExecDriver -- the OpenExec driver contract as code, and the one place the
// `VRM_OPENEXEC_*` codes are raised (docs/design/MOTION_CONTRACT.md, "OpenExec
// driver contract"; the OpenExec plan's P0-4 and P1-1).
//
// # Why a driver needs a contract
//
// Every computation `execMotion` and `execVrm` register is pure, and what a
// caller has to do around them is stated by none of them: a request is armed by
// its first `Compute`, a recurrence is stepped through `ComputeWithOverrides`,
// the default time code is refused by the retarget, and an override of the
// wrong type is dropped with a coding error while its key computes the ordinary
// value. Six reports found those one at a time. This class is them, in the
// order a frame meets them, so a caller follows the contract by calling it
// rather than by reading six reports.
//
// # Why it raises codes
//
// 26.08 classifies none of its own request failures: a computation nobody
// registered, an override of the wrong type, and a request `InvalidateAll`
// expired all arrive as free-text coding errors, and an expired request goes
// on reporting itself valid. So the three codes come from the driver's own
// checks around the request -- a provider looked up by path under exec's own
// rule, the type each key is declared with, and a one-key probe run only when
// a compute posted a coding error -- never from matching exec's text. Exec's
// text is kept, verbatim, in a diagnostic's detail, for a person.
//
// # Why it lives beside the parity harness
//
// The parity harness is its first client, and no library in this workspace
// needs one yet: a bundle is driven by its caller, and `motion_retarget`
// evaluates nothing through OpenExec. So it is a test-support target and not a
// workspace identity, and it moves into one when the `ExecIr` track's
// evaluation client (that plan's P0-6) needs batch requests and invalidation
// callbacks as well.
//
// # What it does not do
//
// Attribute keys (`computeValue` on an attribute) are not declared through it:
// every value this workspace's harness asks for is a prim computation. And it
// holds no pose history of its own -- which previous answer and which snapshot
// to hand in is the caller's, since only the caller knows what its source is.
#pragma once

#include "pxr/pxr.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueOverride.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

namespace execdriver
{

// ---------------------------------------------------------------------------
// The `VRM_OPENEXEC_*` namespace
// ---------------------------------------------------------------------------

// Values are stable array indices; append only before Count, and only with a
// contract review. The strings, not these spellings, are the contract.
enum class OpenExecDiagnosticCode : std::uint8_t
{
    // A key this session cannot compute: no active prim at its provider's
    // path, or no computation of its name for that prim -- a bundle not in the
    // session, a schema the prim does not have. Subject: the key.
    ComputationUnavailable,
    // A value whose type is not the one its key is declared with: an override
    // handed in (refused before the call, since exec would drop it and compute
    // a plausible ordinary value), an override exec itself rejected, or an
    // answer. Subject: the key.
    TypeMismatch,
    // A request exec stopped answering while every key's provider is still
    // there: `InvalidateAll` expired it, or a provider was resynced between two
    // frames. Exec says so through `IsValid()` only while some key is still
    // live; a request whose every key expired is discarded and reports itself
    // valid, like one `InvalidateAll` expired. The driver rebuilt it, restated
    // the time and re-armed it, and the frame is the rebuilt request's answer.
    // Subject: the key.
    Invalidated,

    Count,
};

inline constexpr std::size_t OpenExecDiagnosticCodeCount =
    static_cast<std::size_t>(OpenExecDiagnosticCode::Count);

enum class OpenExecDiagnosticSeverity : std::uint8_t
{
    Warning,
    Error,
};

// The stable string, e.g. "VRM_OPENEXEC_TYPE_MISMATCH". Empty for a value
// outside the enum.
std::string_view OpenExecDiagnosticCodeString(OpenExecDiagnosticCode code) noexcept;

std::optional<OpenExecDiagnosticCode> FindOpenExecDiagnosticCode(std::string_view name) noexcept;

OpenExecDiagnosticSeverity OpenExecDiagnosticDefaultSeverity(OpenExecDiagnosticCode code) noexcept;

// Whether the frame it is reported in still answered. Only `Invalidated` is:
// the driver recovers from it itself.
bool OpenExecDiagnosticIsRecoverable(OpenExecDiagnosticCode code) noexcept;

std::string_view OpenExecDiagnosticSeverityString(OpenExecDiagnosticSeverity severity) noexcept;

struct OpenExecDiagnostic
{
    OpenExecDiagnosticCode code = OpenExecDiagnosticCode::ComputationUnavailable;
    OpenExecDiagnosticSeverity severity = OpenExecDiagnosticSeverity::Error;
    bool recoverable = false;

    // The value key, spelled as exec spells one in its own messages:
    // `/Clip [motion.filterPose]`. It is what two runs are compared on.
    std::string subject;
    std::string detail;
};

bool operator==(const OpenExecDiagnostic& a, const OpenExecDiagnostic& b) noexcept;
bool operator!=(const OpenExecDiagnostic& a, const OpenExecDiagnostic& b) noexcept;

// Fills `severity` and `recoverable` from the code's defaults.
OpenExecDiagnostic MakeOpenExecDiagnostic(OpenExecDiagnosticCode code, std::string subject,
                                          std::string detail = {});

// One line, in the retarget's and the adapters' shape:
//
//     [VRM_OPENEXEC_TYPE_MISMATCH] error subject=/Clip [motion.priorPose]:
//     the override holds 'motion::HumanoidAnimation' and the key is declared
//     'motion::HumanoidPose'; it was not handed to exec
std::string FormatOpenExecDiagnostic(const OpenExecDiagnostic& diagnostic);

// What was raised, in order, each code and subject once -- the retarget's rule,
// for the retarget's reason: a key that is unavailable is one fact about a
// session, however many frames ask for it.
struct OpenExecDiagnostics
{
    std::vector<OpenExecDiagnostic> reported;

    bool Report(OpenExecDiagnostic diagnostic);
    void Merge(const OpenExecDiagnostics& other);
    bool Has(OpenExecDiagnosticCode code, std::string_view subject) const;
    std::vector<std::string> Subjects(OpenExecDiagnosticCode code) const;
    bool HasError() const noexcept;
    bool
    IsClean() const noexcept
    {
        return reported.empty();
    }
};

// ---------------------------------------------------------------------------
// Keys, overrides and frames
// ---------------------------------------------------------------------------

// A prim computation, by path, with the type its computation answers.
//
// By path rather than by `UsdPrim`: a prim deactivated and activated again is
// a new object behind the same path, and the handle exec's own key holds
// expires with the first -- the driver re-resolves the path every frame.
//
// The type is a `std::type_info` rather than a `TfType`, because a `TfType` for
// a bundle's value type does not exist until the bundle's library has loaded,
// and a key is declared before the first compute loads it.
struct Key
{
    PXR_NS::SdfPath provider;
    PXR_NS::TfToken computation;
    const std::type_info* type = nullptr;

    template <class T>
    static Key
    Of(PXR_NS::SdfPath provider, PXR_NS::TfToken computation)
    {
        return Key{std::move(provider), std::move(computation), &typeid(T)};
    }

    // `/Clip [motion.filterPose]`.
    std::string Name() const;
    // The declared type, demangled.
    std::string TypeName() const;
};

bool operator==(const Key& a, const Key& b) noexcept;

// A value substituted for a key's computed value for one call.
struct Override
{
    Key key;
    PXR_NS::VtValue value;
};

// One evaluation's answer.
struct Frame
{
    // One per requested key, in the order the request declared them. An empty
    // value is no answer: a computation refused (its reason is in
    // `refusals`), or the driver withheld it (its reason is in `diagnostics`).
    std::vector<PXR_NS::VtValue> values;

    // What computations posted as runtime errors -- a node's refusal. The
    // arming compute's are here too, in the frame that armed: a node nothing
    // invalidates posts its refusal there and never again.
    std::vector<std::string> refusals;

    // Every other error exec posted that no code accounts for, verbatim. A
    // frame with one did not answer as asked, so it is a failed frame.
    std::vector<std::string> errors;

    // What the driver raised about this frame, standing ones included: a key
    // that is unavailable is reported in every frame that asks for it.
    OpenExecDiagnostics diagnostics;

    bool
    Failed() const noexcept
    {
        return diagnostics.HasError() || !errors.empty();
    }

    template <class T>
    const T*
    Get(std::size_t index) const
    {
        if (index >= values.size() || !values[index].IsHolding<T>())
        {
            return nullptr;
        }
        return &values[index].UncheckedGet<T>();
    }
};

// ---------------------------------------------------------------------------
// The driver
// ---------------------------------------------------------------------------

// One stage, one `ExecUsdSystem` for the driver's lifetime, and requests built
// once and reused -- upstream's own rule (the migration report §6).
class Driver
{
  public:
    using RequestId = std::size_t;

    explicit Driver(const PXR_NS::UsdStageRefPtr& stage);
    ~Driver();

    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;

    // Declares a request and arms it: one compute at the system's current
    // time, which for a new driver is the default time code. That compute's
    // answer is the arming frame, kept rather than discarded -- its refusals
    // are the one place a node nothing invalidates ever reports.
    RequestId Add(std::vector<Key> keys, Frame* arming = nullptr);

    // Names the instant, then computes the request, with `overrides`
    // substituted when there are any. A key an override names joins the
    // request the first time it is named (exec skips, without a word, an
    // override of a key no compiled output is), which costs one rebuild.
    Frame Evaluate(RequestId request, PXR_NS::UsdTimeCode time,
                   std::vector<Override> overrides = {});

    // Everything raised over the driver's life, each code and subject once.
    const OpenExecDiagnostics& Reported() const noexcept;

    // The system, for a caller that has to reach past the driver -- a test
    // that expires a request behind its back, a harness that times it.
    PXR_NS::ExecUsdSystem& System() noexcept;

  private:
    struct Request;

    // One compute's answer, in exec's index space.
    struct Computed
    {
        std::vector<PXR_NS::VtValue> values;
        std::vector<std::string> refusals;   // runtime errors
        std::vector<std::string> complaints; // every other error
    };

    // Builds `request` over its available keys and arms it with the frame's
    // own compute -- the current time, `overrides` included -- probing a key
    // that answered nothing when the arm posted a coding error. Appends the
    // refusals to `frame` and returns the arm, whose complaints are the
    // caller's to classify.
    Computed _Build(Request& request, const std::vector<Override>& overrides, Frame* frame);

    // Computes `request` as built, with `overrides` whose keys it holds.
    Computed _Compute(Request& request, const std::vector<Override>& overrides);

    PXR_NS::ExecUsdValueOverrideVector _Handed(const Request& request,
                                               const std::vector<Override>& overrides,
                                               std::vector<Key>* keys) const;

    PXR_NS::UsdPrim _Provider(const Key& key) const;
    std::vector<bool> _Availability(const std::vector<Key>& keys) const;

    PXR_NS::UsdStageRefPtr _stage;
    std::unique_ptr<PXR_NS::ExecUsdSystem> _system;
    std::vector<std::unique_ptr<Request>> _requests;
    OpenExecDiagnostics _reported;
};

} // namespace execdriver
