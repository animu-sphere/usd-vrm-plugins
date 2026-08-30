// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVrchatOsc/FrameAssembler.h"

#include <algorithm>
#include <string>
#include <utility>

namespace vrmAdapterVrchatOsc
{

namespace
{

// Why a frame closed. Compared by pointer identity below, so these are the
// same three objects everywhere rather than three equal strings.
constexpr const char* kClosedByRepeat = "repeat";
constexpr const char* kClosedByGap = "gap";
constexpr const char* kClosedByRestart = "restart";
constexpr const char* kClosedByFlush = "flush";

// Six decimals in the classic locale. `FormatSeconds` is that formatter and its
// name is about its usual subject rather than about its arithmetic; a distance
// printed any other way would take its decimal point from the host's locale,
// which is the one thing every formatted value in this tree refuses to do.
std::string
Fixed(double value)
{
    return liveTransport::FormatSeconds(value);
}

} // namespace

TrackerFrameAssembler::TrackerFrameAssembler(const TrackerFrameConfig& config)
    : _config(config)
{
}

void
TrackerFrameAssembler::SetSource(std::string source)
{
    _source = std::move(source);
}

void
TrackerFrameAssembler::Reset()
{
    _frame = OpenFrame();
    _observed.clear();
    _lastSeen.clear();
    _reportedStale.clear();
    _lastPosition.clear();
    _lastArrival.reset();
    _peer.clear();
}

void
TrackerFrameAssembler::_Report(std::vector<Diagnostic>* diagnostics,
                               DiagnosticCode code, std::string_view subject,
                               std::optional<double> timestamp,
                               std::string detail)
{
    if (!diagnostics) {
        return;
    }
    Diagnostic diagnostic = MakeDiagnostic(code, std::move(detail));
    diagnostic.source = _source;
    diagnostic.subject.assign(subject);
    diagnostic.timestamp = timestamp;
    diagnostic.sequence = _packetSerial;
    diagnostics->push_back(std::move(diagnostic));
}

void
TrackerFrameAssembler::_Open(double receiveTime, std::string peer,
                             bool beginsNewSession)
{
    _frame = OpenFrame();
    _frame.open = true;
    _frame.receiveTime = receiveTime;
    _frame.peer = std::move(peer);
    _frame.beginsNewSession = beginsNewSession;
}

std::size_t
TrackerFrameAssembler::_SampleFor(const TrackerId& tracker, double receiveTime)
{
    for (std::size_t index = 0; index < _frame.samples.size(); ++index) {
        if (_frame.samples[index].tracker == tracker.segment) {
            return index;
        }
    }

    TrackerSample sample;
    // Copied, not viewed: `TrackerId::segment` points into the datagram, and a
    // frame outlives the buffer a receive loop reuses.
    sample.tracker.assign(tracker.segment);
    sample.index = tracker.index;
    sample.receiveTime = receiveTime;
    _frame.samples.push_back(std::move(sample));

    _frame.channelPacket.emplace_back();
    _frame.channelSet.emplace_back();
    _frame.channelPacket.back().fill(0);
    _frame.channelSet.back().fill(false);
    return _frame.samples.size() - 1;
}

bool
TrackerFrameAssembler::_Close(std::vector<TrackerFrame>* frames,
                              std::vector<Diagnostic>* diagnostics,
                              const char* reason)
{
    if (!_frame.open) {
        return false;
    }

    OpenFrame closing = std::move(_frame);
    _frame = OpenFrame();

    if (closing.samples.empty()) {
        // A boundary was met and nothing had reached the frame. It is not
        // emitted: an empty frame would say a tracker set was observed, and
        // none was.
        ++_stats.framesRefusedEmpty;
        return false;
    }

    if (reason == kClosedByRepeat) {
        ++_stats.framesClosedByRepeat;
    } else if (reason == kClosedByGap) {
        ++_stats.framesClosedByGap;
    } else if (reason == kClosedByRestart) {
        ++_stats.framesClosedByRestart;
    } else {
        ++_stats.framesClosedByFlush;
    }

    TrackerFrame frame;
    frame.samples = std::move(closing.samples);
    frame.receiveTime = closing.receiveTime;
    frame.peer = std::move(closing.peer);
    frame.beginsNewSession = closing.beginsNewSession;
    frame.duplicates = closing.duplicates;

    for (std::size_t index = 0; index < frame.samples.size(); ++index) {
        const TrackerSample& sample = frame.samples[index];
        if (sample.tracker == HeadTrackerSegment) {
            frame.headReference = index;
        }
        if (!sample.complete()) {
            ++frame.partial;
            // The one code only this layer can raise: a single message is
            // always half a tracker, so a decoder reporting this would warn
            // about once a datagram forever (TrackerMessage.h).
            _Report(diagnostics, DiagnosticCode::TrackerPartial,
                    std::string(TrackerAddressPrefix) + sample.tracker,
                    frame.receiveTime,
                    sample.hasPosition ? "a position arrived with no rotation"
                                       : "a rotation arrived with no position");
        }

        if (std::find(_observed.begin(), _observed.end(), sample.tracker)
            == _observed.end()) {
            _observed.push_back(sample.tracker);
        }
        _lastSeen[sample.tracker] = frame.receiveTime;
        // Back in the frame, so its next absence is reported as a crossing of
        // its own rather than being swallowed by the last one.
        _reportedStale[sample.tracker] = false;
    }

    // Absence, measured against what this session has actually seen and never
    // against the eight identities the surface defines.
    for (const std::string& tracker : _observed) {
        const bool present =
            std::any_of(frame.samples.begin(), frame.samples.end(),
                        [&tracker](const TrackerSample& sample) {
                            return sample.tracker == tracker;
                        });
        if (present) {
            continue;
        }
        frame.missing.push_back(tracker);

        const auto seen = _lastSeen.find(tracker);
        if (_config.stalenessSeconds <= 0.0 || seen == _lastSeen.end()) {
            continue;
        }
        const double silent = frame.receiveTime - seen->second;
        if (silent < _config.stalenessSeconds) {
            continue;
        }
        frame.stale.push_back(tracker);
        if (!_reportedStale[tracker]) {
            _reportedStale[tracker] = true;
            ++_stats.stalenessCrossings;
        }
    }

    // The recalibration check: simultaneity, not size. Only a tracker with a
    // position in both frames can be compared, and one of those is not a
    // simultaneity at all -- see the header.
    if (_config.calibrationJumpMeters > 0.0) {
        std::size_t comparable = 0;
        std::size_t jumped = 0;
        for (const TrackerSample& sample : frame.samples) {
            if (!sample.hasPosition) {
                continue;
            }
            const auto previous = _lastPosition.find(sample.tracker);
            if (previous == _lastPosition.end()) {
                continue;
            }
            ++comparable;
            if ((sample.position - previous->second).GetLength()
                > _config.calibrationJumpMeters) {
                ++jumped;
            }
        }
        if (comparable >= 2 && jumped == comparable) {
            frame.followsDiscontinuity = true;
            ++_stats.calibrationDiscontinuities;
            _Report(diagnostics, DiagnosticCode::CalibrationRequired,
                    std::string(TrackerAddressPrefix), frame.receiveTime,
                    "all " + std::to_string(comparable)
                        + " comparable tracker(s) moved more than "
                        + Fixed(_config.calibrationJumpMeters)
                        + " m at once: the tracking space may have been "
                          "recalibrated");
        }
    }

    for (const TrackerSample& sample : frame.samples) {
        if (sample.hasPosition) {
            _lastPosition[sample.tracker] = sample.position;
        }
    }

    ++_stats.framesEmitted;
    _stats.samplesEmitted += frame.samples.size();
    if (!frame.missing.empty()) {
        ++_stats.framesIncomplete;
    }
    if (frame.partial != 0) {
        ++_stats.framesPartial;
    }

    if (frames) {
        frames->push_back(std::move(frame));
    }
    // Counted either way. A caller that wants only a session's diagnostics must
    // get the same reading of it as one that wants its frames.
    return true;
}

std::size_t
TrackerFrameAssembler::Push(const TrackerPacket& packet, double receiveTime,
                            std::string_view peer,
                            std::vector<TrackerFrame>* frames,
                            std::vector<Diagnostic>* diagnostics)
{
    ++_packetSerial;
    const std::size_t before = frames ? frames->size() : 0;

    // The silence first, because it happened first. A stream that went quiet
    // and came back on a new port is both a timeout and a restart, and the
    // other order would read as a session that restarted and then fell silent.
    if (_lastArrival && _config.sourceTimeoutSeconds > 0.0
        && receiveTime - *_lastArrival > _config.sourceTimeoutSeconds) {
        ++_stats.sourceTimeouts;
        _Report(diagnostics, DiagnosticCode::SourceTimeout, _peer, receiveTime,
                "nothing arrived for " + Fixed(receiveTime - *_lastArrival)
                    + " s");
    }

    // A restart is an identity change and nothing else. A silence is a source
    // that paused, however long it lasts -- see the header on why guessing the
    // stronger claim is the one thing this layer must not do.
    bool beginsNewSession = false;
    if (!peer.empty() && !_peer.empty() && peer != _peer) {
        _Close(frames, diagnostics, kClosedByRestart);
        const std::string previous = _peer;
        _observed.clear();
        _lastSeen.clear();
        _reportedStale.clear();
        _lastPosition.clear();
        ++_stats.sessionRestarts;
        _Report(diagnostics, DiagnosticCode::SourceRestarted, peer, receiveTime,
                "the source's endpoint changed from " + previous
                    + "; the trackers this session had observed are forgotten");
        beginsNewSession = true;
    }
    if (!peer.empty()) {
        _peer.assign(peer);
    }
    _lastArrival = receiveTime;

    // The gap rule, against the *first* message of the open frame: a frame on
    // this wire is a burst 0.053 ms wide, so what the window bounds is the
    // whole burst rather than the spacing inside it.
    if (_frame.open && _config.frameWindowSeconds > 0.0
        && receiveTime - _frame.receiveTime > _config.frameWindowSeconds) {
        _Close(frames, diagnostics, kClosedByGap);
    }

    for (const TrackerMessage& message : packet.messages) {
        const std::size_t channel = static_cast<std::size_t>(message.channel);

        // One lookup, and both boundary rules read it: this tracker's channel
        // is already in the open frame, from this datagram or from an earlier
        // one.
        bool duplicate = false;
        if (_frame.open) {
            for (std::size_t index = 0; index < _frame.samples.size();
                 ++index) {
                if (_frame.samples[index].tracker != message.tracker.segment
                    || !_frame.channelSet[index][channel]) {
                    continue;
                }
                if (_frame.channelPacket[index][channel] == _packetSerial) {
                    // The same channel twice inside one datagram. A datagram is
                    // one indivisible delivery, so this is a sender repeating
                    // itself rather than two frames in one send; the first
                    // value stands.
                    duplicate = true;
                } else {
                    // The cycle came round again.
                    _Close(frames, diagnostics, kClosedByRepeat);
                }
                break;
            }
        }
        if (duplicate) {
            ++_frame.duplicates;
            ++_stats.messagesDuplicated;
            continue;
        }

        // The value is converted **before** the sample exists, so a refused
        // message leaves no half-built tracker behind -- a sample with neither
        // channel set would be reported as partial, which would say a tracker
        // reported half of itself when it reported none of itself.
        //
        // The conversion is the boundary and it refuses one thing: a component
        // that is not finite. On the wire path the decoder has already refused
        // that with the same code, so what reaches here is a caller-built
        // packet -- and a NaN reaching a solve makes a tracker vanish from
        // every comparison that had it rather than fail (TrackingSpace.h).
        Diagnostic refusal;
        pxr::GfVec3f position;
        pxr::GfQuatf rotation;
        const bool mapped =
            message.channel == TrackerChannel::Position
                ? MapTrackerPosition(message, &position, &refusal)
                : MapTrackerRotation(message, &rotation, &refusal);
        if (!mapped) {
            ++_stats.messagesRefused;
            if (diagnostics) {
                refusal.source = _source;
                refusal.timestamp = receiveTime;
                refusal.sequence = _packetSerial;
                diagnostics->push_back(std::move(refusal));
            }
            continue;
        }

        if (!_frame.open) {
            _Open(receiveTime, std::string(peer), beginsNewSession);
            beginsNewSession = false;
        }

        const std::size_t index = _SampleFor(message.tracker, receiveTime);
        if (message.channel == TrackerChannel::Position) {
            _frame.samples[index].position = position;
            _frame.samples[index].hasPosition = true;
            ++_stats.positionsAccepted;
        } else {
            _frame.samples[index].rotation = rotation;
            _frame.samples[index].hasRotation = true;
            ++_stats.rotationsAccepted;
        }

        _frame.channelSet[index][channel] = true;
        _frame.channelPacket[index][channel] = _packetSerial;
    }

    return frames ? frames->size() - before : 0;
}

std::size_t
TrackerFrameAssembler::Flush(std::vector<TrackerFrame>* frames,
                             std::vector<Diagnostic>* diagnostics)
{
    return _Close(frames, diagnostics, kClosedByFlush) ? 1 : 0;
}

} // namespace vrmAdapterVrchatOsc
