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
#include <cstdio>
#include <set>
#include <sstream>
#include <thread>

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
    // TEMPORARY instrumentation (PR #191's Linux hang): bound the walk and say
    // how it ended, against the count libtf itself reports.
    std::size_t counted = 0;
    const TfErrorMark::Iterator first = mark.GetBegin(&counted);
    const TfErrorMark::Iterator last = mark.GetEnd();
    std::set<const void*> seen;
    std::size_t walked = 0;
    bool cycled = false;
    for (TfErrorMark::Iterator it = first; it != mark.GetEnd(); ++it) {
        if (!seen.insert(static_cast<const void*>(&*it)).second) {
            cycled = true;
            break;
        }
        if (++walked > 100000) {
            break;
        }
        if (walked <= 12 && counted != 0 && walked > counted) {
            std::fprintf(stderr, "[drain] past the count: #%zu code %s: %s\n",
                         walked, it->GetErrorCodeAsString().c_str(),
                         it->GetCommentary().c_str());
        }
        (it->GetErrorCode() == TF_DIAGNOSTIC_RUNTIME_ERROR_TYPE ? posted.runtime
                                                                : posted.other)
            .push_back(it->GetCommentary());
    }
    if (walked != counted || cycled) {
        std::fprintf(stderr,
                     "[drain] libtf counted %zu, walked %zu, cycle %d, end "
                     "stable %d, first==end %d, thread %zu\n",
                     counted, walked, int(cycled),
                     int(last == mark.GetEnd()), int(first == last),
                     std::hash<std::thread::id>()(std::this_thread::get_id()));
        std::fflush(stderr);
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

// The prim a key names, or none. The pseudo-root is a prim here, as it is to
// exec: the stage's own computations (`computeTime`) are provided by it.
UsdPrim Driver::_Provider(const Key& key) const
{
    return key.provider.IsAbsoluteRootOrPrimPath()
        ? _stage->GetPrimAtPath(key.provider)
        : UsdPrim();
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
        const UsdPrim prim = _Provider(key);
        available.push_back(prim && UsdPrimDefaultPredicate(prim));
    }
    return available;
}

// The overrides a compute hands exec: every one whose key the build handed
// over. A key found unavailable is not in exec's request, and neither is an
// override of it.
ExecUsdValueOverrideVector Driver::_Handed(
    const Request& request, const std::vector<Override>& overrides,
    std::vector<Key>* keys) const
{
    ExecUsdValueOverrideVector handed;
    for (const Override& override : overrides) {
        const std::optional<std::size_t> index = request.IndexOf(override.key);
        if (!index || request.unavailable[*index]) {
            continue;
        }
        handed.push_back(ExecUsdValueOverride{
            ExecUsdValueKey(_Provider(override.key), override.key.computation),
            override.value});
        if (keys) {
            keys->push_back(override.key);
        }
    }
    return handed;
}

Driver::Computed Driver::_Compute(Request& request,
                                  const std::vector<Override>& overrides)
{
    Computed computed;
    computed.values.resize(HandedCount(request.execIndex));
    ExecUsdValueOverrideVector handed = _Handed(request, overrides, nullptr);
    TfErrorMark mark;
    const ExecUsdCacheView view = handed.empty()
        ? _system->Compute(*request.exec)
        : _system->ComputeWithOverrides(*request.exec, std::move(handed));
    for (std::size_t j = 0; j < computed.values.size(); ++j) {
        computed.values[j] = view.Get(static_cast<int>(j));
    }
    Posted posted = Drain(mark);
    computed.refusals = std::move(posted.runtime);
    computed.complaints = std::move(posted.other);
    return computed;
}

Driver::Computed Driver::_Build(Request& request,
                                const std::vector<Override>& overrides,
                                Frame* frame)
{
    const std::vector<Key> all = request.All();
    request.availability = _Availability(all);
    request.unavailable.assign(all.size(), std::nullopt);
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (request.availability[i]) {
            continue;
        }
        const UsdPrim prim = _Provider(all[i]);
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
    // nobody registers, and it builds without it. A pass arms the request with
    // the frame's own compute -- at the time the system holds, with the
    // frame's overrides -- so what it posts is the frame's and nobody else's.
    // Every refusal posted is kept, the first pass's included, because a
    // rebuilt request over nodes already computed reaches their cached
    // outputs and posts nothing.
    Computed armed;
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<ExecUsdValueKey> valueKeys;
        request.execIndex.assign(all.size(), -1);
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (request.unavailable[i]) {
                continue;
            }
            request.execIndex[i] = static_cast<int>(valueKeys.size());
            valueKeys.emplace_back(_Provider(all[i]), all[i].computation);
        }
        request.exec = std::make_unique<ExecUsdRequest>(
            _system->BuildRequest(std::move(valueKeys)));

        armed = _Compute(request, overrides);
        AppendOnce(&frame->refusals, armed.refusals);
        if (armed.complaints.empty() || pass == 1) {
            break;
        }

        // Something about the request, not a node. Asked of each key that
        // answered nothing, alone, so the complaint is attributed to a key
        // by the driver's own question rather than by exec's wording.
        bool found = false;
        for (std::size_t i = 0; i < all.size(); ++i) {
            const int index = request.execIndex[i];
            if (index < 0
                || !armed.values[static_cast<std::size_t>(index)].IsEmpty()) {
                continue;
            }
            std::vector<ExecUsdValueKey> one;
            one.emplace_back(_Provider(all[i]), all[i].computation);
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
        if (!found) {
            break;  // the complaints are the caller's to classify
        }
    }
    request.dirty = false;
    return armed;
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
    const Computed armed = _Build(*request, {}, &frame);
    AppendOnce(&frame.errors, armed.complaints);
    Fill(request->keys, request->execIndex, request->unavailable, armed.values,
         /*withhold*/ !frame.errors.empty(), &frame);

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
    //
    // Only the keys the last build knew are compared, so a key an override
    // names for the first time is not mistaken for a provider that moved; and
    // the expiry is looked for whether or not the request is being rebuilt
    // anyway, or a frame that joins a key would rebuild it in silence.
    bool resynced = false;
    const std::vector<bool> availability = _Availability(request.All());
    if (!std::equal(request.availability.begin(), request.availability.end(),
                    availability.begin())) {
        request.dirty = true;
    } else if (!request.exec->IsValid()) {
        request.dirty = true;
        resynced = true;
    }

    // The contract's instant, named before anything computes -- and restated,
    // as a side effect, after an InvalidateAll reset the system to the default.
    _system->ChangeTime(time);

    // Each override, held to its key before exec sees it. Exec would drop a
    // mistyped one -- an empty value included, so an absence cannot be pushed
    // -- with a coding error and compute the key's ordinary value, which is a
    // plausible answer to a question nobody asked. A frame refused here
    // computes nothing, and arms nothing: an arm's refusals belong to the frame
    // that computed them, and this one would discard them.
    bool refused = false;
    for (const Override& override : overrides) {
        if (Holds(override.value, override.key)) {
            continue;
        }
        frame.diagnostics.Report(MakeOpenExecDiagnostic(
            OpenExecDiagnosticCode::TypeMismatch, override.key.Name(),
            override.value.IsEmpty()
                ? "the override is an empty value, and an absence cannot be "
                  "pushed into a key: exec drops it with a coding error and "
                  "computes the key's ordinary value, so the driver did not "
                  "hand it over and the frame has no answer"
                : "the override holds '" + HeldTypeName(override.value)
                      + "' and the key is declared '" + override.key.TypeName()
                      + "': exec drops a mistyped override with a coding error "
                        "and computes the key's ordinary value, so the driver "
                        "did not hand it over and the frame has no answer"));
        refused = true;
    }
    if (refused) {
        // What the last build said about the requested keys still describes
        // them only if nothing has moved since; otherwise the next frame that
        // builds says it.
        Fill(request.keys, request.execIndex,
             request.dirty ? std::vector<std::optional<OpenExecDiagnostic>>(
                                 request.keys.size())
                           : request.unavailable,
             {}, /*withhold*/ true, &frame);
        _reported.Merge(frame.diagnostics);
        return frame;
    }

    // The frame's compute. When the request has to be built, the arm is this
    // compute, overrides and all.
    bool built = false;
    Computed computed;
    if (request.dirty) {
        computed = _Build(request, overrides, &frame);
        built = true;
    } else {
        computed = _Compute(request, overrides);
        AppendOnce(&frame.refusals, computed.refusals);
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

    auto unavailableCount = [&] {
        return std::count_if(request.unavailable.begin(),
                             request.unavailable.end(),
                             [](const auto& u) { return u.has_value(); });
    };

    // Exec complained about the request rather than a node. First, the one
    // cause the request cannot show: exec stopped answering it. Rebuilding
    // tells the two apart -- the rebuilt request answers cleanly, or the
    // rebuild's own probe finds a key nobody can compute.
    bool withhold = false;
    if (!computed.complaints.empty() && !built) {
        const bool answeredNothing =
            std::all_of(computed.values.begin(), computed.values.end(),
                        [](const VtValue& v) { return v.IsEmpty(); });
        const auto unavailableBefore = unavailableCount();
        computed = _Build(request, overrides, &frame);
        built = true;
        const auto unavailableAfter = unavailableCount();
        if (answeredNothing && computed.complaints.empty()
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
    }

    // An override of a key the build found nobody can compute was not handed
    // over, so the frame did not answer the question it was asked.
    for (const Override& override : overrides) {
        const std::optional<std::size_t> index = request.IndexOf(override.key);
        if (index && request.unavailable[*index]) {
            frame.diagnostics.Report(*request.unavailable[*index]);
            withhold = true;
        }
    }

    // Then the overrides, each alone: one exec rejects although it holds the
    // declared type says the declaration is not what the computation answers.
    std::vector<Key> handedKeys;
    const ExecUsdValueOverrideVector handed =
        _Handed(request, overrides, &handedKeys);
    if (!computed.complaints.empty() && !handed.empty()) {
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
            computed.complaints.clear();
            withhold = true;
        }
    }
    AppendOnce(&frame.errors, computed.complaints);
    if (!frame.errors.empty()) {
        withhold = true;
    }

    Fill(request.keys, request.execIndex, request.unavailable, computed.values,
         withhold, &frame);
    _reported.Merge(frame.diagnostics);
    return frame;
}

} // namespace execdriver
