// SPDX-License-Identifier: Apache-2.0
#include "Options.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>

namespace vrchatOscRecordTool
{
namespace
{

// A session is held in memory until it is written, because the capture writer
// emits a whole capture at once -- that is what makes its output canonical and
// therefore comparable (PacketCapture.h). So the bound on how long a recording
// may run is a bound on memory, and it exists rather than being optional: a
// recorder left running overnight would otherwise end as an out-of-memory kill
// with nothing written at all.
//
// A million datagrams is a generous ten minutes of anything this wire is likely
// to carry. The shape is not measured yet -- that is VRC-1's job -- but the
// arithmetic that bounds it does not need the shape: a datagram is a
// `std::vector`, so a million of them cost their payload plus 24 bytes of header
// each plus whatever the allocator rounds a small request up to, which is on the
// order of 150 MB resident for short packets.
//
// Stated in memory rather than in seconds on purpose. An operator recording
// longer says so.
constexpr std::size_t kDefaultMaxDatagrams = 1000000;

// Two hours. Not a limit on what a session may be, a limit on what a mistyped
// flag may cost: `--duration 3600000` should fail at the prompt rather than
// forty days later.
constexpr double kMaxDurationSeconds = 7200.0;

bool
TakeValue(const std::vector<std::string>& arguments, std::size_t* index,
          const std::string& flag, std::string* value, std::string* error)
{
    if (*index + 1 >= arguments.size()) {
        *error = flag + " requires a value";
        return false;
    }
    ++(*index);
    *value = arguments[*index];
    return true;
}

bool
TakeDouble(const std::vector<std::string>& arguments, std::size_t* index,
           const std::string& flag, double* value, std::string* error)
{
    std::string text;
    if (!TakeValue(arguments, index, flag, &text, error)) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        // `stod` accepts "nan" and "inf", and every range check below is a
        // comparison -- which NaN passes by failing to be on either side of it.
        if (!std::isfinite(parsed)) {
            throw std::invalid_argument("not a finite number");
        }
        *value = parsed;
    } catch (const std::exception&) {
        *error = flag + " expects a number, got '" + text + "'";
        return false;
    }
    return true;
}

// `minimum` is a parameter rather than an assumed zero so that the range in the
// message is the range that is actually enforced.
bool
TakeCount(const std::vector<std::string>& arguments, std::size_t* index,
          const std::string& flag, double minimum, double limit,
          std::size_t* value, std::string* error)
{
    double parsed = 0.0;
    if (!TakeDouble(arguments, index, flag, &parsed, error)) {
        return false;
    }
    if (parsed < minimum || parsed > limit || parsed != std::floor(parsed)) {
        *error = flag + " expects a whole number between "
            + std::to_string(static_cast<long long>(minimum)) + " and "
            + std::to_string(static_cast<long long>(limit));
        return false;
    }
    *value = static_cast<std::size_t>(parsed);
    return true;
}

// A value destined for the capture's header, refused here if the format cannot
// carry it.
//
// Header values are single whitespace-delimited tokens: the reader takes one
// token and then requires the line to be fully consumed, so `sender vrchat 1.2`
// fails to parse at the *value* rather than being read as three words. The
// writer will emit it regardless.
//
// That combination is why this is checked at the prompt and not at the write. A
// recorder that accepted `--sender "mocopi app 2.7"`, ran for ten minutes
// against a live sender, reported success and left behind a file its own reader
// refuses has destroyed a session that cannot be re-recorded.
bool
TakeHeaderValue(const std::vector<std::string>& arguments, std::size_t* index,
                const std::string& flag, std::string* value,
                std::string* error)
{
    std::string text;
    if (!TakeValue(arguments, index, flag, &text, error)) {
        return false;
    }
    const std::size_t space = text.find_first_of(" \t\r\n\v\f");
    if (space != std::string::npos) {
        *error = flag + " is written into the capture's header, which carries "
                        "one whitespace-delimited token per key, so '"
            + text
            + "' would produce a file this adapter's own reader refuses; join "
              "the words with '-' or '_'";
        return false;
    }
    *value = text;
    return true;
}

// Non-negative seconds, with the shared upper bound. Three flags take exactly
// this shape, so the check is a function rather than three copies of the same
// two comparisons.
bool
TakeSeconds(const std::vector<std::string>& arguments, std::size_t* index,
            const std::string& flag, double* value, std::string* error)
{
    if (!TakeDouble(arguments, index, flag, value, error)) {
        return false;
    }
    if (*value < 0.0 || *value > kMaxDurationSeconds) {
        *error = flag + " expects a non-negative number of seconds no greater "
                        "than "
            + std::to_string(static_cast<long long>(kMaxDurationSeconds));
        return false;
    }
    return true;
}

// Splits "192.168.0.5:9000", "0.0.0.0", "[::1]:9000" or ":9000" into its two
// halves. A bracketed IPv6 literal is why this is not a search for the last
// colon in the string, and an unbracketed one carrying a port is refused rather
// than guessed at: "::1:9000" is a valid address in its own right.
//
// A half that is present-but-empty is refused rather than ignored. An empty port
// would leave the caller unable to tell "the operator named nothing" from "the
// operator named this", so the tool would bind its default while the operator
// believed they had chosen -- a silent default is the one outcome an argument
// parser must not reach, because it is indistinguishable from being obeyed.
bool
SplitEndpoint(const std::string& text, std::string* address, std::string* port,
              std::string* error)
{
    address->clear();
    port->clear();
    const char* const shape = " expects ADDR, ADDR:PORT, or :PORT";
    if (text.empty()) {
        *error = std::string("--listen") + shape;
        return false;
    }
    if (text.front() == '[') {
        const std::size_t close = text.find(']');
        if (close == std::string::npos) {
            *error = "--listen: '" + text + "' opens a bracketed address and "
                     "never closes it";
            return false;
        }
        *address = text.substr(1, close - 1);
        if (close + 1 < text.size()) {
            if (text[close + 1] != ':') {
                *error = "--listen: '" + text + "' has trailing characters "
                         "after the address";
                return false;
            }
            *port = text.substr(close + 2);
        }
        if (address->empty()) {
            *error = "--listen: '" + text + "' brackets an empty address";
            return false;
        }
        if (text.size() > close + 1 && port->empty()) {
            *error = "--listen: '" + text + "' ends with a ':' and names no port";
            return false;
        }
        return true;
    }
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) {
        *address = text;
        return true;
    }
    if (text.find(':', colon + 1) != std::string::npos) {
        *error = "--listen: '" + text + "' looks like an IPv6 address with a "
                 "port; bracket it as [address]:port";
        return false;
    }
    *address = text.substr(0, colon);
    *port = text.substr(colon + 1);
    // An empty *address* is legal and documented -- ":9000" is how a port is
    // given alone -- so only the port half is checked here. That also covers ":"
    // on its own, which names neither.
    if (port->empty()) {
        *error = "--listen: '" + text + "' ends with a ':' and names no port";
        return false;
    }
    return true;
}

bool
ParsePort(const std::string& text, std::uint16_t* port, std::string* error)
{
    try {
        std::size_t consumed = 0;
        const long parsed = std::stol(text, &consumed);
        if (consumed != text.size() || parsed < 0 || parsed > 65535) {
            throw std::invalid_argument("out of range");
        }
        *port = static_cast<std::uint16_t>(parsed);
    } catch (const std::exception&) {
        *error = "expected a port in [0, 65535], got '" + text + "'";
        return false;
    }
    return true;
}

} // namespace

const char*
GetUsage()
{
    return
        "vrchat_osc_record - record a VRChat OSC session, and say what arrived\n"
        "\n"
        "Writes the datagrams a sender delivered, verbatim, in the\n"
        "vrchat-osc-packet-capture format this adapter's corpus is written in.\n"
        "Nothing here decodes one. The OSC surface is published, which is\n"
        "exactly why this tool has no opinion about it: a specification says\n"
        "what a receiver must accept, and what a sender sends is a\n"
        "measurement. The file is the evidence a decoder will be built from.\n"
        "What the report says is therefore what a socket can see - how much\n"
        "arrived, from whom, how fast, and in how many distinct shapes - and\n"
        "not what any of it means.\n"
        "\n"
        "Usage:\n"
        "  vrchat_osc_record --output <session.vrchatoscpackets> [options]\n"
        "  vrchat_osc_record --dry-run [options]\n"
        "  vrchat_osc_record --inspect <capture.vrchatoscpackets>\n"
        "\n"
        "Listening:\n"
        "  --listen ADDR[:PORT]   Bind address, numeric (default 0.0.0.0).\n"
        "                         '0.0.0.0' every IPv4 interface, '127.0.0.1'\n"
        "                         this machine only - which is narrow rather\n"
        "                         than useless here, because a sender on this\n"
        "                         machine is a normal arrangement for this\n"
        "                         protocol. ':PORT' gives a port alone; an\n"
        "                         IPv6 address must be bracketed, as '[::1]'\n"
        "                         or '[::1]:9000'.\n"
        "  --port N               Listen port (default 9000, the port VRChat\n"
        "                         itself listens on and therefore the one a\n"
        "                         sender aims at). 0 lets the OS choose and is\n"
        "                         reported back.\n"
        "  --reuse-address        Allow binding a port another socket holds.\n"
        "                         Off by default: a second recorder that\n"
        "                         silently takes half the traffic is a failure\n"
        "                         with no symptom - and on this port the other\n"
        "                         socket is quite likely to be VRChat.\n"
        "  --receive-buffer BYTES Kernel receive buffer to request. What was\n"
        "                         granted is reported, which is not always what\n"
        "                         was asked for.\n"
        "\n"
        "Recording:\n"
        "  --output PATH          Capture to write. A session that received\n"
        "                         nothing writes no file and exits 1: the\n"
        "                         format has no datagram-less form, so the\n"
        "                         alternative is a file this adapter's own\n"
        "                         reader refuses.\n"
        "  --sender NAME          Application that sent the packets, recorded\n"
        "                         as provenance.\n"
        "  --device NAME          Hardware behind it, recorded as provenance.\n"
        "                         Worth more here than on a native wire: this\n"
        "                         one is relayed, so --sender names the\n"
        "                         application and not the thing that was\n"
        "                         measured.\n"
        "  --source-id ID         Name for this capture, recorded as\n"
        "                         provenance.\n"
        "                         These three become capture header values, and\n"
        "                         the format carries one token per key - so a\n"
        "                         value with a space in it is refused here\n"
        "                         rather than written into a file the reader\n"
        "                         would then refuse. Join words with '-'.\n"
        "  --dry-run              Listen and report, write nothing.\n"
        "\n"
        "Noticing a sender that is not there:\n"
        "  --silence-timeout S    Report VRM_VRCHAT_OSC_SOURCE_TIMEOUT after S\n"
        "                         seconds with nothing arriving, once per\n"
        "                         episode, and keep listening. Off by default,\n"
        "                         because how long a sender may take to start\n"
        "                         is a property of the session and not of the\n"
        "                         socket. This does not stop the recording -\n"
        "                         --idle-timeout is the flag that does.\n"
        "\n"
        "Stopping (a session always has at least one):\n"
        "  --duration S           Stop after S seconds of session.\n"
        "  --idle-timeout S       Stop after S seconds with nothing arriving.\n"
        "  --max-datagrams N      Stop after N datagrams (1..10000000, default\n"
        "                         1000000). The capture is held in memory until\n"
        "                         it is written, so this bound is on memory.\n"
        "  Ctrl-C stops at any point and still writes what was recorded.\n"
        "  A session the socket cut short writes what it had and exits 1, so a\n"
        "  script can tell a complete recording from a truncated one.\n"
        "\n"
        "Reading:\n"
        "  --inspect PATH         Read a recorded capture and report on it.\n"
        "                         Opens no socket, records nothing, and prints\n"
        "                         the same report a live session prints minus\n"
        "                         the lines that describe a socket.\n"
        "\n"
        "Exporting (with --inspect, and only there):\n"
        "  --export-trace PATH    Write what this adapter delivered as a\n"
        "                         motion-capture-trace, which the product's own\n"
        "                         motion_capture replays knowing nothing about\n"
        "                         VRChat OSC. This is the one thing here that\n"
        "                         decodes and solves, and it is why it runs\n"
        "                         against a file: a recording stays\n"
        "                         decoder-free, and a trace exported from\n"
        "                         committed bytes is the same trace on any\n"
        "                         machine.\n"
        "  --assign SPEC          Which tracker is on which body region, as\n"
        "                         '1=hips 2=leftFoot 3=rightFoot head=head'.\n"
        "                         Pairs separate on whitespace or commas; '#'\n"
        "                         runs to end of line. Required with\n"
        "                         --export-trace and deliberately undefaulted:\n"
        "                         a tracker index is not a body role, so a\n"
        "                         default would be a calibration this tool\n"
        "                         invented. Regions: head chest hips\n"
        "                         leftElbow leftHand leftKnee leftFoot\n"
        "                         rightElbow rightHand rightKnee rightFoot.\n"
        "                         Spelled exactly; a near miss is refused\n"
        "                         rather than guessed at.\n"
        "  --unplaced POLICY      What to do with an observed tracker the\n"
        "                         assignment does not place: refuse (default),\n"
        "                         ignore, or hold. 'refuse' stops - it will\n"
        "                         still be true next frame; 'hold' waits for a\n"
        "                         rig that is still coming up.\n"
        "  --no-root-motion       Do not author the observed hips position as\n"
        "                         root motion. The hips *rotation* is authored\n"
        "                         either way: a body that turned turned\n"
        "                         whatever the translation is worth.\n"
        "  --source-session N     Which of the capture's sessions to export,\n"
        "                         counting from 1. One trace is one session, so\n"
        "                         a capture the sender restarted during is\n"
        "                         refused until this names a half - the two\n"
        "                         clocks overlap and splicing them would invent\n"
        "                         a continuity the wire denies.\n"
        "\n"
        "  --quiet                Suppress the progress line and the warnings\n"
        "                         on stderr. The report is what this tool\n"
        "                         produces and always goes to stdout.\n"
        "  -h, --help             Show this message.\n";
}

bool
ParseOptions(const std::vector<std::string>& arguments, Options* options,
             bool* showHelp, std::string* error)
{
    *showHelp = false;
    // The first flag seen that only means something to a live session. Tracked
    // rather than inferred from the parsed values, because several of these are
    // indistinguishable afterwards from their own defaults -- `--port 9000` and
    // `--duration 0` both parse to what the struct already held.
    const char* sessionFlag = nullptr;
    const auto session = [&sessionFlag](const char* flag) {
        if (!sessionFlag) {
            sessionFlag = flag;
        }
    };
    // The same problem on the export side, and it needs the same treatment for
    // the same reason: `--unplaced refuse` and an unwritten `--no-root-motion`
    // both parse to what the struct already held, so an export flag given
    // without an export cannot be found by reading the parsed values back.
    bool assignmentGiven = false;
    bool unplacedGiven = false;
    bool rootMotionGiven = false;

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (argument == "-h" || argument == "--help") {
            *showHelp = true;
            return true;
        } else if (argument == "--listen") {
            session("--listen");
            std::string text;
            if (!TakeValue(arguments, &i, argument, &text, error)) {
                return false;
            }
            std::string address;
            std::string port;
            if (!SplitEndpoint(text, &address, &port, error)) {
                return false;
            }
            if (!address.empty()) {
                options->receiver.listenAddress = address;
            }
            if (!port.empty()) {
                std::string reason;
                if (!ParsePort(port, &options->receiver.listenPort, &reason)) {
                    *error = "--listen: " + reason;
                    return false;
                }
            }
        } else if (argument == "--port") {
            session("--port");
            std::string text;
            if (!TakeValue(arguments, &i, argument, &text, error)) {
                return false;
            }
            std::string reason;
            if (!ParsePort(text, &options->receiver.listenPort, &reason)) {
                *error = "--port: " + reason;
                return false;
            }
        } else if (argument == "--reuse-address") {
            session("--reuse-address");
            options->receiver.reuseAddress = true;
        } else if (argument == "--receive-buffer") {
            session("--receive-buffer");
            // 256 MB. Every platform clamps this far lower, so the bound is only
            // here to refuse a typo before the socket does. 0 is a real value
            // here and means "leave the platform default".
            if (!TakeCount(arguments, &i, argument, 0.0, 268435456.0,
                           &options->receiver.receiveBufferBytes, error)) {
                return false;
            }
        } else if (argument == "--silence-timeout") {
            session("--silence-timeout");
            if (!TakeSeconds(arguments, &i, argument,
                             &options->receiver.silenceTimeoutSeconds, error)) {
                return false;
            }
        } else if (argument == "--output") {
            session("--output");
            if (!TakeValue(arguments, &i, argument, &options->outputPath,
                           error)) {
                return false;
            }
        } else if (argument == "--inspect") {
            if (!TakeValue(arguments, &i, argument, &options->inspectPath,
                           error)) {
                return false;
            }
        } else if (argument == "--sender") {
            session("--sender");
            if (!TakeHeaderValue(arguments, &i, argument, &options->sender,
                                 error)) {
                return false;
            }
        } else if (argument == "--device") {
            session("--device");
            if (!TakeHeaderValue(arguments, &i, argument, &options->device,
                                 error)) {
                return false;
            }
        } else if (argument == "--source-id") {
            session("--source-id");
            if (!TakeHeaderValue(arguments, &i, argument, &options->sourceId,
                                 error)) {
                return false;
            }
        } else if (argument == "--duration") {
            session("--duration");
            if (!TakeSeconds(arguments, &i, argument, &options->durationSeconds,
                             error)) {
                return false;
            }
        } else if (argument == "--idle-timeout") {
            session("--idle-timeout");
            if (!TakeSeconds(arguments, &i, argument, &options->idleSeconds,
                             error)) {
                return false;
            }
        } else if (argument == "--max-datagrams") {
            session("--max-datagrams");
            // A minimum of 1, stated in the range rather than enforced under it:
            // a session with no bound at all is not offered, because the capture
            // is held in memory until it is written.
            if (!TakeCount(arguments, &i, argument, 1.0,
                           static_cast<double>(kDefaultMaxDatagrams) * 10.0,
                           &options->maxDatagrams, error)) {
                return false;
            }
        } else if (argument == "--export-trace") {
            if (!TakeValue(arguments, &i, argument, &options->traceExportPath,
                           error)) {
                return false;
            }
            // Present-but-empty is refused rather than ignored, the correction
            // `SplitEndpoint` already carries and for its reason: an empty path
            // leaves this field indistinguishable from the flag never having
            // been given, so the tool would read the capture, write nothing and
            // exit 0 -- a silent default, which is the one outcome an argument
            // parser must not reach because it cannot be told apart from being
            // obeyed.
            if (options->traceExportPath.empty()) {
                *error = "--export-trace names the trace to write and was "
                         "given an empty path";
                return false;
            }
        } else if (argument == "--assign") {
            std::string text;
            if (!TakeValue(arguments, &i, argument, &text, error)) {
                return false;
            }
            // Parsed here rather than carried as text, so an operator with a
            // device strapped on and a capture in hand learns about a
            // misspelled region before the capture is read rather than after.
            // `ParseTrackerAssignmentSpec` refuses a spec that parses and
            // cannot be used in the same call, which is why one message covers
            // both.
            std::string reason;
            // The parser carries `unplaced` over rather than resetting it
            // (TrackerAssignment.h), which is what makes the two flags order-
            // independent: `--unplaced hold --assign ...` and `--assign ...
            // --unplaced hold` are the same command.
            if (!motionTracking::ParseTrackerAssignmentSpec(
                    text, &options->assignment, &reason)) {
                *error = "--assign: " + reason;
                return false;
            }
            assignmentGiven = true;
        } else if (argument == "--unplaced") {
            std::string text;
            if (!TakeValue(arguments, &i, argument, &text, error)) {
                return false;
            }
            const std::optional<motionTracking::UnplacedTrackerPolicy> policy =
                motionTracking::ParseUnplacedTrackerPolicy(text);
            if (!policy) {
                *error = "--unplaced expects refuse, ignore or hold, got '"
                    + text + "'";
                return false;
            }
            // The policy is the caller's flag and not part of the spec's
            // syntax, deliberately: an `unplaced=...` directive inside --assign
            // would make `unplaced` a tracker identity nobody could use
            // (TrackerAssignment.h).
            options->assignment.unplaced = *policy;
            unplacedGiven = true;
        } else if (argument == "--no-root-motion") {
            options->solve.authorRootMotion = false;
            rootMotionGiven = true;
        } else if (argument == "--source-session") {
            // The count's own minimum, so the range in the message is the range
            // enforced: a capture cannot hold a zeroth session.
            if (!TakeCount(arguments, &i, argument, 1.0, 1000000.0,
                           &options->sourceSession, error)) {
                return false;
            }
        } else if (argument == "--dry-run") {
            session("--dry-run");
            options->dryRun = true;
        } else if (argument == "--quiet") {
            options->quiet = true;
        } else {
            *error = "unknown argument '" + argument + "'";
            return false;
        }
    }

    if (options->maxDatagrams == 0) {
        options->maxDatagrams = kDefaultMaxDatagrams;
    }

    // The three flags that only mean something to an export, named one at a
    // time rather than as a class: an operator who typed `--assign` and forgot
    // `--export-trace` has asked for a solve nothing would write down, and a
    // message saying which flag is stranded is the one that says what to add.
    const char* strandedFlag = nullptr;
    if (assignmentGiven) {
        strandedFlag = "--assign";
    } else if (unplacedGiven) {
        strandedFlag = "--unplaced";
    } else if (rootMotionGiven) {
        strandedFlag = "--no-root-motion";
    } else if (options->sourceSession != 0) {
        strandedFlag = "--source-session";
    }
    if (options->traceExportPath.empty() && strandedFlag != nullptr) {
        *error = std::string(strandedFlag)
            + " configures the trace an export writes, so it needs "
              "--export-trace";
        return false;
    }

    // The assignment is required and cannot be defaulted, which is the whole of
    // §5.1 said at a prompt: a tracker index is not a body role, so there is no
    // reading of `/tracking/trackers/1` this tool is entitled to pick. A
    // default would be a calibration a decoder invented, which is the thing
    // this path exists to make impossible.
    if (!options->traceExportPath.empty() && !assignmentGiven) {
        *error = "--export-trace needs --assign: a tracker index is not a body "
                 "role, so which device is on which region is your statement "
                 "about the rig and not something this tool may guess. Try "
                 "--assign '1=hips 2=leftFoot 3=rightFoot head=head' and read "
                 "the report's placed/unsolved lines back";
        return false;
    }

    // The export must not be pointed at the capture it is reading. This is the
    // check that can refuse at the prompt, before a byte is read, naming both
    // flags; it is **not sufficient on its own**, because two spellings of one
    // path are not one string, and `ExportTrace` asks the filesystem for the
    // rest. The sibling tool measured what the gap costs: it exited 0 and
    // printed a report describing datagrams that no longer existed.
    if (!options->traceExportPath.empty()
        && options->traceExportPath == options->inspectPath) {
        *error = "--export-trace names the same path as --inspect, and writing "
                 "the trace there would destroy the capture it was derived "
                 "from";
        return false;
    }

    if (!options->inspectPath.empty()) {
        // --inspect opens no socket and records nothing, so every flag about
        // either is a mistake worth naming rather than a setting that silently
        // does nothing.
        //
        // The four export flags are outside that class and are the only ones
        // that ever will be: they change what is written *out of* a capture
        // rather than how one is recorded, which is the same exception both
        // sibling tools make. This list was empty rather than short until
        // VRC-6, and the reason is worth keeping -- a flag earns an exception
        // here by having a file to act on, and until an export existed none
        // did. `--quiet` is not in this class at all: it is about the two
        // output streams, which both modes have.
        if (sessionFlag) {
            *error = std::string("--inspect reads a recorded capture and opens "
                                 "no socket, so ")
                + sessionFlag + " has nothing to act on";
            return false;
        }
        return true;
    }

    if (!options->traceExportPath.empty()) {
        // The line this tool is built on, enforced at the prompt rather than
        // explained afterwards: a recording runs no decoder, so there is
        // nothing in a live session for this flag to read (TraceExport.h). The
        // message names both commands rather than only refusing, because the
        // operator asking for this has a sender running and the recording they
        // want is still the right first step.
        *error = "--export-trace derives a trace from a recorded capture, so it "
                 "goes with --inspect: record the session first, then export "
                 "from the file. Nothing decodes during a recording here, and "
                 "that is what makes the capture the evidence";
        return false;
    }

    if (options->outputPath.empty() && !options->dryRun) {
        *error = "--output is required (or use --dry-run to listen and report "
                 "without writing, or --inspect to read a capture)";
        return false;
    }
    if (!options->outputPath.empty() && options->dryRun) {
        *error = "--dry-run writes nothing, so --output has nothing to act on";
        return false;
    }
    return true;
}

} // namespace vrchatOscRecordTool
