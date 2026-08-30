// SPDX-License-Identifier: Apache-2.0
#include "motionTracking/TrackerAssignment.h"

#include <array>
#include <string>

namespace motionTracking
{

namespace
{

constexpr std::array<std::string_view,
                     static_cast<std::size_t>(UnplacedTrackerPolicy::Count)>
    kPolicyNames = {"refuse", "ignore", "hold"};

constexpr std::array<std::string_view, TrackerAssignmentRefusalCount>
    kRefusalNames = {"None",         "SpecInvalid",   "ObservationInvalid",
                     "UnplacedTracker", "Held",       "NothingPlaced"};

static_assert(kRefusalNames.size() == TrackerAssignmentRefusalCount,
              "every refusal needs a name a report can print");

bool
IsSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'
           || c == '\v';
}

// An identity a `ParseTrackerAssignmentSpec` round trip can carry. See
// `ValidateTrackerAssignmentSpec`'s note: a statement that cannot be written
// down is not an operator's statement.
bool
IsWritableIdentity(std::string_view tracker) noexcept
{
    if (tracker.empty())
    {
        return false;
    }
    for (const char c : tracker)
    {
        if (IsSpace(c) || c == '=' || c == ',' || c == '#')
        {
            return false;
        }
    }
    return true;
}

void
Fail(std::string* reason, std::string text)
{
    if (reason != nullptr)
    {
        *reason = std::move(text);
    }
}

std::string
Quote(std::string_view text)
{
    std::string quoted;
    quoted.reserve(text.size() + 2);
    quoted.push_back('`');
    quoted.append(text);
    quoted.push_back('`');
    return quoted;
}

} // namespace

std::string_view
UnplacedTrackerPolicyName(UnplacedTrackerPolicy policy) noexcept
{
    const auto index = static_cast<std::size_t>(policy);
    if (index >= kPolicyNames.size())
    {
        return {};
    }
    return kPolicyNames[index];
}

std::optional<UnplacedTrackerPolicy>
ParseUnplacedTrackerPolicy(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kPolicyNames.size(); ++i)
    {
        if (kPolicyNames[i] == name)
        {
            return static_cast<UnplacedTrackerPolicy>(i);
        }
    }
    return std::nullopt;
}

std::string_view
TrackerAssignmentRefusalName(TrackerAssignmentRefusal refusal) noexcept
{
    const auto index = static_cast<std::size_t>(refusal);
    if (index >= TrackerAssignmentRefusalCount)
    {
        return {};
    }
    return kRefusalNames[index];
}

bool
ValidateTrackerAssignmentSpec(const TrackerAssignmentSpec& spec,
                              std::string* reason)
{
    if (spec.statements.empty())
    {
        Fail(reason, "a statement with no lines places nothing; every "
                     "observation would be NothingPlaced");
        return false;
    }
    if (static_cast<std::size_t>(spec.unplaced)
        >= static_cast<std::size_t>(UnplacedTrackerPolicy::Count))
    {
        Fail(reason, "the unplaced-tracker policy is outside the enum");
        return false;
    }

    for (std::size_t i = 0; i < spec.statements.size(); ++i)
    {
        const TrackerRegionStatement& statement = spec.statements[i];
        if (statement.tracker.empty())
        {
            Fail(reason, "statement " + std::to_string(i)
                             + " names no tracker");
            return false;
        }
        if (!IsWritableIdentity(statement.tracker))
        {
            Fail(reason, "tracker " + Quote(statement.tracker)
                             + " carries whitespace or a separator, so no "
                               "operator could have written this statement "
                               "down");
            return false;
        }
        if (TrackerRegionName(statement.region).empty())
        {
            Fail(reason, "tracker " + Quote(statement.tracker)
                             + " is stated onto no region this vocabulary "
                               "carries");
            return false;
        }

        for (std::size_t j = 0; j < i; ++j)
        {
            if (spec.statements[j].tracker == statement.tracker)
            {
                Fail(reason, "tracker " + Quote(statement.tracker)
                                 + " is stated twice");
                return false;
            }
            if (spec.statements[j].region == statement.region)
            {
                Fail(reason,
                     "region "
                         + Quote(TrackerRegionName(statement.region))
                         + " is stated for two trackers");
                return false;
            }
        }
    }
    return true;
}

bool
ParseTrackerAssignmentSpec(std::string_view text, TrackerAssignmentSpec* out,
                           std::string* reason)
{
    if (out == nullptr)
    {
        Fail(reason, "no spec to parse into");
        return false;
    }

    TrackerAssignmentSpec parsed;
    // The policy is not in this syntax, so it is carried over from whatever the
    // caller had rather than reset to the default: a caller that set `Hold` and
    // then read a statement did not ask to be put back on `Refuse`.
    parsed.unplaced = out->unplaced;

    std::size_t i = 0;
    while (i < text.size())
    {
        const char c = text[i];
        if (IsSpace(c) || c == ',')
        {
            ++i;
            continue;
        }
        if (c == '#')
        {
            while (i < text.size() && text[i] != '\n')
            {
                ++i;
            }
            continue;
        }

        const std::size_t begin = i;
        while (i < text.size() && !IsSpace(text[i]) && text[i] != ','
               && text[i] != '#')
        {
            ++i;
        }
        const std::string_view token = text.substr(begin, i - begin);

        const std::size_t split = token.find('=');
        if (split == std::string_view::npos)
        {
            Fail(reason, "expected tracker=region, got " + Quote(token));
            return false;
        }
        const std::string_view tracker = token.substr(0, split);
        // Everything after the FIRST `=`, and a second one needs no rule of its
        // own: `t1=head=hips` leaves `head=hips`, which is not a region this
        // vocabulary carries, so the refusal below already names what it saw. A
        // guard here was written first and removed when a mutation showed no
        // input could reach it.
        const std::string_view region = token.substr(split + 1);

        const std::optional<TrackerRegion> resolved = ParseTrackerRegion(region);
        if (!resolved.has_value())
        {
            Fail(reason, Quote(region)
                             + " is not a region this vocabulary carries");
            return false;
        }

        TrackerRegionStatement statement;
        statement.tracker.assign(tracker);
        statement.region = *resolved;
        parsed.statements.push_back(std::move(statement));
    }

    // A parsed statement that cannot be used is a refusal, not a result: see
    // the header note.
    if (!ValidateTrackerAssignmentSpec(parsed, reason))
    {
        return false;
    }

    *out = std::move(parsed);
    return true;
}

std::optional<std::size_t>
TrackerAssignment::ObservedFor(TrackerRegion region) const
{
    for (const TrackerAssignmentBinding& binding : bound)
    {
        if (binding.region == region)
        {
            return binding.observedIndex;
        }
    }
    return std::nullopt;
}

std::optional<TrackerRegion>
TrackerAssignment::RegionFor(std::size_t observedIndex) const
{
    for (const TrackerAssignmentBinding& binding : bound)
    {
        if (binding.observedIndex == observedIndex)
        {
            return binding.region;
        }
    }
    return std::nullopt;
}

TrackerAssignment
AssignTrackers(const TrackerAssignmentSpec& spec,
               const std::vector<std::string_view>& observed)
{
    TrackerAssignment assignment;

    std::string reason;
    if (!ValidateTrackerAssignmentSpec(spec, &reason))
    {
        assignment.refusal = TrackerAssignmentRefusal::SpecInvalid;
        assignment.detail = std::move(reason);
        return assignment;
    }

    // Bind first, refuse afterwards. Every vector is filled whatever the
    // outcome (header note), so the checks below read the same evidence a
    // report does rather than a private copy of it.
    for (const TrackerRegionStatement& statement : spec.statements)
    {
        bool found = false;
        for (std::size_t i = 0; i < observed.size(); ++i)
        {
            if (observed[i] == statement.tracker)
            {
                TrackerAssignmentBinding binding;
                binding.region = statement.region;
                binding.observedIndex = i;
                assignment.bound.push_back(binding);
                found = true;
                break;
            }
        }
        if (!found)
        {
            assignment.absent.push_back(statement.region);
        }
    }

    for (std::size_t i = 0; i < observed.size(); ++i)
    {
        bool placed = false;
        for (const TrackerAssignmentBinding& binding : assignment.bound)
        {
            if (binding.observedIndex == i)
            {
                placed = true;
                break;
            }
        }
        if (!placed)
        {
            assignment.unplaced.push_back(i);
        }
    }

    // The observation itself, checked after binding so a report of a bad
    // observation still shows what would have bound.
    for (std::size_t i = 0; i < observed.size(); ++i)
    {
        if (observed[i].empty())
        {
            assignment.refusal = TrackerAssignmentRefusal::ObservationInvalid;
            assignment.detail =
                "observed tracker " + std::to_string(i) + " has no identity";
            return assignment;
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (observed[j] == observed[i])
            {
                assignment.refusal =
                    TrackerAssignmentRefusal::ObservationInvalid;
                assignment.detail = "tracker " + Quote(observed[i])
                                    + " is observed twice";
                return assignment;
            }
        }
    }

    if (!assignment.unplaced.empty()
        && spec.unplaced != UnplacedTrackerPolicy::Ignore)
    {
        const bool hold = spec.unplaced == UnplacedTrackerPolicy::Hold;
        assignment.refusal = hold ? TrackerAssignmentRefusal::Held
                                  : TrackerAssignmentRefusal::UnplacedTracker;
        assignment.detail =
            "tracker " + Quote(observed[assignment.unplaced.front()])
            + " is on no region this statement names ("
            + std::string(UnplacedTrackerPolicyName(spec.unplaced)) + ")";
        return assignment;
    }

    if (assignment.bound.empty())
    {
        assignment.refusal = TrackerAssignmentRefusal::NothingPlaced;
        assignment.detail = "this statement places none of the "
                            + std::to_string(observed.size())
                            + " observed trackers";
        return assignment;
    }

    assignment.refusal = TrackerAssignmentRefusal::None;
    return assignment;
}

} // namespace motionTracking
