// SPDX-License-Identifier: Apache-2.0

#include "ExecDriver.h"

#include "pxr/base/arch/demangle.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/tf/error.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/safeTypeCompare.h"

#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/exec/execUsd/valueOverride.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primFlags.h"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace execdriver
{

// ---------------------------------------------------------------------------
// The code table
// ---------------------------------------------------------------------------

namespace
{

struct CodeRow
{
    std::string_view name;
    OpenExecDiagnosticSeverity severity;
    bool recoverable;
};

// Indexed by OpenExecDiagnosticCode, and written out for the retarget table's
// reason: a rename in the enum must not rename a code something matches on.
constexpr std::array<CodeRow, OpenExecDiagnosticCodeCount> kCodes = {{
    {"VRM_OPENEXEC_COMPUTATION_UNAVAILABLE",
     OpenExecDiagnosticSeverity::Error, false},
    {"VRM_OPENEXEC_TYPE_MISMATCH", OpenExecDiagnosticSeverity::Error, false},
    // A warning, and recoverable: the driver rebuilt the request and the frame
    // answered. It is reported at all because a caller that holds requests of
    // its own is holding one exec has stopped answering.
    {"VRM_OPENEXEC_INVALIDATED", OpenExecDiagnosticSeverity::Warning, true},
}};

const CodeRow* Row(OpenExecDiagnosticCode code) noexcept
{
    const auto index = static_cast<std::size_t>(code);
    return index < kCodes.size() ? &kCodes[index] : nullptr;
}

} // namespace

std::string_view OpenExecDiagnosticCodeString(
    OpenExecDiagnosticCode code) noexcept
{
    const CodeRow* row = Row(code);
    return row ? row->name : std::string_view();
}

std::optional<OpenExecDiagnosticCode> FindOpenExecDiagnosticCode(
    std::string_view name) noexcept
{
    for (std::size_t index = 0; index < kCodes.size(); ++index) {
        if (kCodes[index].name == name) {
            return static_cast<OpenExecDiagnosticCode>(index);
        }
    }
    return std::nullopt;
}

OpenExecDiagnosticSeverity OpenExecDiagnosticDefaultSeverity(
    OpenExecDiagnosticCode code) noexcept
{
    const CodeRow* row = Row(code);
    return row ? row->severity : OpenExecDiagnosticSeverity::Error;
}

bool OpenExecDiagnosticIsRecoverable(OpenExecDiagnosticCode code) noexcept
{
    const CodeRow* row = Row(code);
    return row ? row->recoverable : false;
}

std::string_view OpenExecDiagnosticSeverityString(
    OpenExecDiagnosticSeverity severity) noexcept
{
    switch (severity) {
    case OpenExecDiagnosticSeverity::Warning:
        return "warning";
    case OpenExecDiagnosticSeverity::Error:
        return "error";
    }
    return "error";
}

bool operator==(const OpenExecDiagnostic& a,
                const OpenExecDiagnostic& b) noexcept
{
    return a.code == b.code && a.severity == b.severity
        && a.recoverable == b.recoverable && a.subject == b.subject
        && a.detail == b.detail;
}

bool operator!=(const OpenExecDiagnostic& a,
                const OpenExecDiagnostic& b) noexcept
{
    return !(a == b);
}

OpenExecDiagnostic MakeOpenExecDiagnostic(OpenExecDiagnosticCode code,
                                          std::string subject,
                                          std::string detail)
{
    OpenExecDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = OpenExecDiagnosticDefaultSeverity(code);
    diagnostic.recoverable = OpenExecDiagnosticIsRecoverable(code);
    diagnostic.subject = std::move(subject);
    diagnostic.detail = std::move(detail);
    return diagnostic;
}

std::string FormatOpenExecDiagnostic(const OpenExecDiagnostic& diagnostic)
{
    std::string line;
    line += '[';
    line += OpenExecDiagnosticCodeString(diagnostic.code);
    line += "] ";
    line += OpenExecDiagnosticSeverityString(diagnostic.severity);
    if (diagnostic.recoverable) {
        line += " recoverable";
    }
    if (!diagnostic.subject.empty()) {
        line += " subject=";
        line += diagnostic.subject;
    }
    if (!diagnostic.detail.empty()) {
        line += ": ";
        line += diagnostic.detail;
    }
    return line;
}

bool OpenExecDiagnostics::Report(OpenExecDiagnostic diagnostic)
{
    if (Has(diagnostic.code, diagnostic.subject)) {
        return false;
    }
    reported.push_back(std::move(diagnostic));
    return true;
}

void OpenExecDiagnostics::Merge(const OpenExecDiagnostics& other)
{
    for (const OpenExecDiagnostic& diagnostic : other.reported) {
        Report(diagnostic);
    }
}

bool OpenExecDiagnostics::Has(OpenExecDiagnosticCode code,
                              std::string_view subject) const
{
    return std::any_of(reported.begin(), reported.end(),
                       [&](const OpenExecDiagnostic& d) {
                           return d.code == code && d.subject == subject;
                       });
}

std::vector<std::string> OpenExecDiagnostics::Subjects(
    OpenExecDiagnosticCode code) const
{
    std::vector<std::string> subjects;
    for (const OpenExecDiagnostic& diagnostic : reported) {
        if (diagnostic.code == code) {
            subjects.push_back(diagnostic.subject);
        }
    }
    return subjects;
}

bool OpenExecDiagnostics::HasError() const noexcept
{
    return std::any_of(reported.begin(), reported.end(),
                       [](const OpenExecDiagnostic& d) {
                           return d.severity == OpenExecDiagnosticSeverity::Error;
                       });
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

std::string Key::Name() const
{
    return provider.GetString() + " [" + computation.GetString() + "]";
}

std::string Key::TypeName() const
{
    return type ? ArchGetDemangled(*type) : std::string("(undeclared)");
}

bool operator==(const Key& a, const Key& b) noexcept
{
    return a.provider == b.provider && a.computation == b.computation
        && a.type && b.type && TfSafeTypeCompare(*a.type, *b.type);
}

namespace
{

// One key names one computed value, whatever type it is declared with.
bool SameValue(const Key& a, const Key& b)
{
    return a.provider == b.provider && a.computation == b.computation;
}

bool Holds(const VtValue& value, const Key& key)
{
    return key.type && TfSafeTypeCompare(value.GetTypeid(), *key.type);
}

std::string HeldTypeName(const VtValue& value)
{
    return value.IsEmpty() ? std::string("void")
                           : ArchGetDemangled(value.GetTypeid());
}

std::string Said(UsdTimeCode time)
{
    std::ostringstream out;
    out.precision(17);
    out << time;
    return out.str();
}

// What exec posted, split the one way 26.08 lets it be split without reading
// its text: a computation refuses with a runtime error (every node in both
// bundles does, beside an empty output), and exec's complaints about the
// request itself -- a computation it cannot find, an override it rejects, a
// request it has expired -- are coding errors, a failed `TF_VERIFY` included.
struct Posted
{
    std::vector<std::string> runtime;
    std::vector<std::string> other;
};

Posted Drain(TfErrorMark& mark)
{
    Posted posted;
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it) {
        (it->GetErrorCode() == TF_DIAGNOSTIC_RUNTIME_ERROR_TYPE ? posted.runtime
                                                                : posted.other)
            .push_back(it->GetCommentary());
    }
    mark.Clear();
    return posted;
}

std::string Joined(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& line : lines) {
        if (!out.empty()) {
            out += "; ";
        }
        out += line;
    }
    return out;
}

void AppendOnce(std::vector<std::string>* into,
                const std::vector<std::string>& lines)
{
    for (const std::string& line : lines) {
        if (std::find(into->begin(), into->end(), line) == into->end()) {
            into->push_back(line);
        }
    }
}

// How many keys a build handed to exec: the size of exec's index space.
std::size_t HandedCount(const std::vector<int>& execIndex)
{
    return static_cast<std::size_t>(std::count_if(
        execIndex.begin(), execIndex.end(), [](int index) { return index >= 0; }));
}

} // namespace

// ---------------------------------------------------------------------------
// The driver
// ---------------------------------------------------------------------------

struct Driver::Request
{
    // What the caller asked for, in its order, then the keys its overrides
    // named that it did not ask for -- requested too, so that exec has
    // compiled them (docs/design/MOTION_CONTRACT.md, the driver contract).
    std::vector<Key> keys;
    std::vector<Key> overrideKeys;

    // As of the last build.
    std::unique_ptr<ExecUsdRequest> exec;
    std::vector<int> execIndex;  // per key in All(); -1 when not handed over
    std::vector<bool> availability;
    std::vector<std::optional<OpenExecDiagnostic>> unavailable;
    bool dirty = true;

    std::vector<Key> All() const
    {
        std::vector<Key> all = keys;
        all.insert(all.end(), overrideKeys.begin(), overrideKeys.end());
        return all;
    }

    std::optional<std::size_t> IndexOf(const Key& key) const
    {
        const std::vector<Key> all = All();
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (SameValue(all[i], key)) {
                return i;
            }
        }
        return std::nullopt;
    }
};

Driver::Driver(const UsdStageRefPtr& stage)
    : _stage(stage)
    , _system(std::make_unique<ExecUsdSystem>(stage))
{
}

Driver::~Driver() = default;

const OpenExecDiagnostics& Driver::Reported() const noexcept
{
    return _reported;
}

ExecUsdSystem& Driver::System() noexcept
{
    return *_system;
}

// Exec's own rule for a provider (`_IsValidVisitor` in execUsd's request):
// a valid prim that the default predicate admits -- active, loaded, defined,
// not abstract. A key on anything else is expired by exec on construction and
// posts a coding error at every compile, so the driver never hands one over.
std::vector<bool> Driver::_Availability(const std::vector<Key>& keys) const
{
    std::vector<bool> available;
    available.reserve(keys.size());
    for (const Key& key : keys) {
        const UsdPrim prim = key.provider.IsPrimPath()
            ? _stage->GetPrimAtPath(key.provider)
            : UsdPrim();
        available.push_back(prim && UsdPrimDefaultPredicate(prim));
    }
    return available;
}

void Driver::_Build(Request& request, Frame* frame)
{
    const std::vector<Key> all = request.All();
    request.availability = _Availability(all);
    request.unavailable.assign(all.size(), std::nullopt);
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (request.availability[i]) {
            continue;
        }
        const UsdPrim prim = all[i].provider.IsPrimPath()
            ? _stage->GetPrimAtPath(all[i].provider)
            : UsdPrim();
        request.unavailable[i] = MakeOpenExecDiagnostic(
            OpenExecDiagnosticCode::ComputationUnavailable, all[i].Name(),
            prim ? "the prim at <" + all[i].provider.GetString()
                       + "> is not active, loaded and defined, which exec "
                         "requires of a provider, so the key was not handed "
                         "to exec"
                 : "there is no prim at <" + all[i].provider.GetString()
                       + ">, so the key was not handed to exec");
    }

    // Two passes at most: the second only when the first found a computation
    // nobody registers, and it builds without it. A pass arms the request --
    // its first compute, at whatever time the system holds -- and a refusal
    // posted there is kept, because a rebuilt request over nodes already
    // computed reaches their cached outputs and posts nothing.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<ExecUsdValueKey> valueKeys;
        request.execIndex.assign(all.size(), -1);
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (request.unavailable[i]) {
                continue;
            }
            request.execIndex[i] = static_cast<int>(valueKeys.size());
            valueKeys.emplace_back(_stage->GetPrimAtPath(all[i].provider),
                                   all[i].computation);
        }
        const std::size_t count = valueKeys.size();
        request.exec = std::make_unique<ExecUsdRequest>(
            _system->BuildRequest(std::move(valueKeys)));

        std::vector<VtValue> armed(count);
        Posted posted;
        {
            TfErrorMark mark;
            const ExecUsdCacheView view = _system->Compute(*request.exec);
            for (std::size_t j = 0; j < count; ++j) {
                armed[j] = view.Get(static_cast<int>(j));
            }
            posted = Drain(mark);
        }
        AppendOnce(&frame->refusals, posted.runtime);
        if (posted.other.empty()) {
            break;
        }

        // Something about the request, not a node. Asked of each key that
        // answered nothing, alone, so the complaint is attributed to a key
        // by the driver's own question rather than by exec's wording.
        bool found = false;
        for (std::size_t i = 0; i < all.size(); ++i) {
            const int index = request.execIndex[i];
            if (index < 0 || !armed[static_cast<std::size_t>(index)].IsEmpty()) {
                continue;
            }
            std::vector<ExecUsdValueKey> one;
            one.emplace_back(_stage->GetPrimAtPath(all[i].provider),
                             all[i].computation);
            ExecUsdRequest probe = _system->BuildRequest(std::move(one));
            TfErrorMark mark;
            _system->Compute(probe).Get(0);
            const Posted said = Drain(mark);
            if (said.other.empty()) {
                continue;
            }
            found = true;
            request.unavailable[i] = MakeOpenExecDiagnostic(
                OpenExecDiagnosticCode::ComputationUnavailable, all[i].Name(),
                "no computation of that name answers for the prim: its "
                "bundle is not in this session, or the prim lacks the schema "
                "it is registered on. exec: " + Joined(said.other));
        }
        if (!found || pass == 1) {
            // Nothing a key accounts for: kept, verbatim, and the frame fails.
            AppendOnce(&frame->errors, posted.other);
            break;
        }
    }
    request.dirty = false;
}

namespace
{

// The values a frame answers for its requested keys, from exec's index space,
// with each key's standing unavailability reported and each answer's type held
// to its declaration.
void Fill(const std::vector<Key>& keys, const std::vector<int>& execIndex,
          const std::vector<std::optional<OpenExecDiagnostic>>& unavailable,
          const std::vector<VtValue>& execValues, bool withhold, Frame* frame)
{
    frame->values.assign(keys.size(), VtValue());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (unavailable[i]) {
            frame->diagnostics.Report(*unavailable[i]);
            continue;
        }
        const int index = execIndex[i];
        if (index < 0 || static_cast<std::size_t>(index) >= execValues.size()) {
            continue;
        }
        const VtValue& value = execValues[static_cast<std::size_t>(index)];
        if (value.IsEmpty()) {
            continue;  // a refusal, and its reason is in `refusals`
        }
        if (!Holds(value, keys[i])) {
            frame->diagnostics.Report(MakeOpenExecDiagnostic(
                OpenExecDiagnosticCode::TypeMismatch, keys[i].Name(),
                "the answer holds '" + HeldTypeName(value)
                    + "' and the key is declared '" + keys[i].TypeName()
                    + "'; the value was withheld"));
            continue;
        }
        if (!withhold) {
            frame->values[i] = value;
        }
    }
}

} // namespace

Driver::RequestId Driver::Add(std::vector<Key> keys, Frame* arming)
{
    auto request = std::make_unique<Request>();
    request->keys = std::move(keys);

    Frame frame;
    _Build(*request, &frame);
    // The arm's values, read back from the request it left armed: a second
    // compute at the same time reaches the cache and posts nothing new.
    std::vector<VtValue> values(HandedCount(request->execIndex));
    {
        TfErrorMark mark;
        const ExecUsdCacheView view = _system->Compute(*request->exec);
        for (std::size_t j = 0; j < values.size(); ++j) {
            values[j] = view.Get(static_cast<int>(j));
        }
        const Posted posted = Drain(mark);
        AppendOnce(&frame.refusals, posted.runtime);
        AppendOnce(&frame.errors, posted.other);
    }
    Fill(request->keys, request->execIndex, request->unavailable, values,
         /*withhold*/ false, &frame);

    _reported.Merge(frame.diagnostics);
    if (arming) {
        *arming = std::move(frame);
    }
    _requests.push_back(std::move(request));
    return _requests.size() - 1;
}

Frame Driver::Evaluate(RequestId id, UsdTimeCode time,
                       std::vector<Override> overrides)
{
    Request& request = *_requests.at(id);
    Frame frame;
    frame.values.assign(request.keys.size(), VtValue());

    // An override of a key nothing compiled is skipped by exec without a word
    // (ExecSystem::_ComputeWithOverrides: "silently skip this override"). So a
    // key an override names is requested as well, which guarantees it is.
    for (const Override& override : overrides) {
        if (!request.IndexOf(override.key)) {
            request.overrideKeys.push_back(override.key);
            request.dirty = true;
        }
    }

    // A provider that went or came back since the build changes which keys
    // exec can be handed. One that was resynced in between leaves every path
    // answering and exec's request expired -- the one expiry exec reports,
    // and only while some key is still live: a request whose EVERY key
    // expired is discarded, which clears the bits `IsValid()` reads, so it
    // reports itself valid again and is found below, as an InvalidateAll is.
    bool resynced = false;
    if (_Availability(request.All()) != request.availability) {
        request.dirty = true;
    } else if (!request.dirty && !request.exec->IsValid()) {
        request.dirty = true;
        resynced = true;
    }

    // The contract's instant, named before anything computes -- and restated,
    // as a side effect, after an InvalidateAll reset the system to the default.
    _system->ChangeTime(time);

    bool built = false;
    if (request.dirty) {
        _Build(request, &frame);
        built = true;
    }
    if (resynced) {
        for (std::size_t i = 0; i < request.keys.size(); ++i) {
            if (request.execIndex[i] >= 0) {
                frame.diagnostics.Report(MakeOpenExecDiagnostic(
                    OpenExecDiagnosticCode::Invalidated, request.keys[i].Name(),
                    "exec reported the request invalid: a provider was "
                    "resynced since the last frame, and each one is at its "
                    "path again; the driver rebuilt the request and re-armed "
                    "it at time code " + Said(time)));
            }
        }
    }

    // Each override, held to its key before exec sees it. Exec would drop a
    // mistyped one -- an empty value included, so an absence cannot be pushed
    // -- with a coding error and compute the key's ordinary value, which is a
    // plausible answer to a question nobody asked.
    ExecUsdValueOverrideVector handed;
    std::vector<Key> handedKeys;
    bool refused = false;
    for (const Override& override : overrides) {
        const std::size_t index = *request.IndexOf(override.key);
        if (request.unavailable[index]) {
            frame.diagnostics.Report(*request.unavailable[index]);
            refused = true;
            continue;
        }
        if (!Holds(override.value, override.key)) {
            frame.diagnostics.Report(MakeOpenExecDiagnostic(
                OpenExecDiagnosticCode::TypeMismatch, override.key.Name(),
                override.value.IsEmpty()
                    ? "the override is an empty value, and an absence cannot "
                      "be pushed into a key: exec drops it with a coding error "
                      "and computes the key's ordinary value, so the driver "
                      "did not hand it over and the frame has no answer"
                    : "the override holds '" + HeldTypeName(override.value)
                          + "' and the key is declared '"
                          + override.key.TypeName()
                          + "': exec drops a mistyped override with a coding "
                            "error and computes the key's ordinary value, so "
                            "the driver did not hand it over and the frame has "
                            "no answer"));
            refused = true;
            continue;
        }
        handed.push_back(ExecUsdValueOverride{
            ExecUsdValueKey(_stage->GetPrimAtPath(override.key.provider),
                            override.key.computation),
            override.value});
        handedKeys.push_back(override.key);
    }
    if (refused) {
        Fill(request.keys, request.execIndex, request.unavailable, {},
             /*withhold*/ true, &frame);
        _reported.Merge(frame.diagnostics);
        return frame;
    }

    // One compute of the request as it stands, in exec's index space. The
    // handed overrides name their keys by UsdPrim, and a rebuild re-resolves
    // the same paths on an unchanged stage, so they stay good across one.
    auto compute = [&](Posted* posted) {
        const std::size_t count = HandedCount(request.execIndex);
        std::vector<VtValue> values(count);
        TfErrorMark mark;
        const ExecUsdCacheView view = handed.empty()
            ? _system->Compute(*request.exec)
            : _system->ComputeWithOverrides(*request.exec,
                                            ExecUsdValueOverrideVector(handed));
        for (std::size_t j = 0; j < count; ++j) {
            values[j] = view.Get(static_cast<int>(j));
        }
        *posted = Drain(mark);
        return values;
    };
    auto unavailableCount = [&] {
        return std::count_if(request.unavailable.begin(),
                             request.unavailable.end(),
                             [](const auto& u) { return u.has_value(); });
    };

    Posted posted;
    std::vector<VtValue> values = compute(&posted);
    AppendOnce(&frame.refusals, posted.runtime);

    // Exec complained about the request rather than a node. First, the one
    // cause the request cannot show: exec stopped answering it. Rebuilding
    // tells the two apart -- the rebuilt request answers cleanly, or the
    // rebuild's own probe finds a key nobody can compute.
    bool withhold = false;
    if (!posted.other.empty() && !built) {
        const bool answeredNothing =
            std::all_of(values.begin(), values.end(),
                        [](const VtValue& v) { return v.IsEmpty(); });
        const auto unavailableBefore = unavailableCount();
        _Build(request, &frame);
        built = true;
        const auto unavailableAfter = unavailableCount();
        Posted again;
        values = compute(&again);
        AppendOnce(&frame.refusals, again.runtime);
        if (answeredNothing && again.other.empty()
            && unavailableAfter == unavailableBefore) {
            for (std::size_t i = 0; i < request.keys.size(); ++i) {
                if (request.execIndex[i] >= 0) {
                    frame.diagnostics.Report(MakeOpenExecDiagnostic(
                        OpenExecDiagnosticCode::Invalidated,
                        request.keys[i].Name(),
                        "exec stopped answering the request while it still "
                        "reported itself valid, which is what an "
                        "InvalidateAll leaves, and what a request whose "
                        "every provider was resynced leaves; the driver "
                        "rebuilt it, restated time code " + Said(time)
                        + " and re-armed it, and this frame is the rebuilt "
                          "request's answer"));
                }
            }
        }
        posted.other = again.other;
    }

    // Then the overrides, each alone: one exec rejects although it holds the
    // declared type says the declaration is not what the computation answers.
    if (!posted.other.empty() && !handed.empty()) {
        bool attributed = false;
        for (std::size_t k = 0; k < handed.size(); ++k) {
            TfErrorMark mark;
            _system->ComputeWithOverrides(*request.exec,
                                          ExecUsdValueOverrideVector{handed[k]});
            const Posted said = Drain(mark);
            if (said.other.empty()) {
                continue;
            }
            attributed = true;
            frame.diagnostics.Report(MakeOpenExecDiagnostic(
                OpenExecDiagnosticCode::TypeMismatch, handedKeys[k].Name(),
                "exec rejected the override although it holds '"
                    + handedKeys[k].TypeName()
                    + "', the type the key is declared with, so the "
                      "declaration is not what the computation answers; the "
                      "frame has no answer. exec: " + Joined(said.other)));
        }
        if (attributed) {
            posted.other.clear();
            withhold = true;
        }
    }
    AppendOnce(&frame.errors, posted.other);
    if (!frame.errors.empty()) {
        withhold = true;
    }

    Fill(request.keys, request.execIndex, request.unavailable, values, withhold,
         &frame);
    _reported.Merge(frame.diagnostics);
    return frame;
}

} // namespace execdriver
