// SPDX-License-Identifier: Apache-2.0
#include "Options.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>

namespace mocopiRecordTool
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
// A million datagrams, and the estimate behind that number is deliberately in
// the unit the bound is in rather than in payload bytes. This protocol's payload
// size is not documented, so "60 MB of packets" is a figure nobody here can
// honestly quote -- but the per-datagram *overhead* is knowable without knowing
// the protocol at all: each one is a `std::vector`, 24 bytes of header plus a
// separate heap block whose allocator rounds a small request up, so a million of
// them cost on the order of 100 MB of process before a single payload byte is
// counted. That is the half of the estimate the sibling tool could state
// exactly and this one cannot, and it is also the half that dominates.
//
// An operator recording longer says so.
constexpr std::size_t kDefaultMaxDatagrams = 1000000;

// Two hours. Not a limit on what a session may be, a limit on what a mistyped
// flag may cost: `--duration 3600000` should fail at the prompt rather than
// forty days later.
constexpr double kMaxDurationSeconds = 7200.0;

// The helpers below began as a second copy of the sibling tool's, and the trigger
// for making them one is the trigger `PacketCapture.h` states for the format and
// `UdpReceiver.h` restates for the socket: a **third** recorder. What is worth
// saying at this copy rather than there is how little of it is protocol-shaped --
// an endpoint splitter and a bounded number parser are the same code for any UDP
// tool, so if the shared library those headers describe is ever written, this
// file is the cheapest part of it to move and the least interesting.
//
// **And the copy has now cost something measurable, like the socket's did.** A
// review of this file found four defects in the parsing that the sibling has
// identically, because they were copied along with everything else: an endpoint
// whose port half is empty silently keeping the default port, a `--listen` error
// message advertising a bare port the splitter reads as an address, a count's
// range error naming a minimum a second check then refuses, and -- the one that
// loses a recording -- provenance written into a capture header without being
// checked for the whitespace that header cannot carry. All four are corrected
// here and remain in `vmcRecord` as of 2026-08-11. That is the cost of the
// repetition stated as a measurement rather than as a worry, which is the form
// `UdpReceiver.h` established for it.

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
// message is the range that is actually enforced. A count refused by a *second*
// check below the range check is a flag that answers "expects a whole number
// between 0 and 10000000" and then refuses 0.
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
// `mocopi-packet-capture` header values are single whitespace-delimited tokens:
// the reader takes one token and then requires the line to be fully consumed
// (PacketCapture.cpp), so `sender mocopi 1.2.3` fails to parse at the *value*
// rather than being read as three words. The writer will emit it regardless.
//
// That combination is why this is checked at the prompt and not at the write. A
// recorder that accepted `--sender "mocopi 1.2.3"`, ran for ten minutes against a
// device, reported success and left behind a file its own reader refuses has
// destroyed a session that cannot be re-recorded -- which is exactly the outcome
// the datagram-less refusal in main.cpp exists to prevent, arrived at from the
// other direction. A newline is worse than a space and the same check catches it:
// it would land in the file as another header or record line.
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
// this shape here, which is one more than the sibling has -- `--silence-timeout`
// is this tool's own -- so the check is a function rather than three copies of
// the same two comparisons.
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

// Splits "192.168.0.5:12351", "0.0.0.0", "[::1]:12351" or ":12351" into its two
// halves. A bracketed IPv6 literal is why this is not a search for the last
// colon in the string, and an unbracketed one carrying a port is refused rather
// than guessed at: "::1:12351" is a valid address in its own right.
//
// **A half that is present-but-empty is refused rather than ignored**, which is
// the correction the sibling still needs. Returning success with an empty port
// for "192.168.0.5:" left the caller's `if (!port.empty())` false, so the tool
// bound 12351 while the operator believed they had named a port and nothing was
// said either way -- a silent default is the one outcome an argument parser must
// not reach, because it is indistinguishable from being obeyed.
bool
SplitEndpoint(const std::string& text, std::string* address, std::string* port,
              std::string* error)
{
    address->clear();
    port->clear();
    // ADDR, ADDR:PORT or :PORT. A bare "12351" is an *address* to this splitter
    // and to `getaddrinfo` under AI_NUMERICHOST, so the message names the leading
    // colon rather than offering "a port" and leaving the reader to find out at
    // the bind that it meant something else.
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
    // An empty *address* is legal and documented -- ":12351" is how a port is
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
        "mocopi_record - record a native mocopi session, and say what arrived\n"
        "\n"
        "Writes the datagrams a source delivered, verbatim, in the\n"
        "mocopi-packet-capture format this adapter's corpus is written in.\n"
        "Nothing here decodes one: this protocol has no published\n"
        "specification, so the file is the evidence a decoder will be built\n"
        "from and this tool's job is to obtain it without having an opinion\n"
        "about it. What the report says is therefore what a socket can see -\n"
        "how much arrived, from whom, how fast, and in how many distinct\n"
        "shapes - and not what any of it means.\n"
        "\n"
        "Usage:\n"
        "  mocopi_record --output <session.mocopipackets> [options]\n"
        "  mocopi_record --dry-run [options]\n"
        "  mocopi_record --inspect <capture.mocopipackets>\n"
        "\n"
        "Listening:\n"
        "  --listen ADDR[:PORT]   Bind address, numeric (default 0.0.0.0).\n"
        "                         '0.0.0.0' every IPv4 interface, '127.0.0.1'\n"
        "                         this machine only - which no device can\n"
        "                         reach, because the vendor documents\n"
        "                         'localhost' as unsupported. ':PORT' gives a\n"
        "                         port alone; an IPv6 address must be\n"
        "                         bracketed, as '[::1]' or '[::1]:12351'.\n"
        "  --port N               Listen port (default 12351, the product's\n"
        "                         own). 0 lets the OS choose and is reported\n"
        "                         back.\n"
        "  --reuse-address        Allow binding a port another socket holds.\n"
        "                         Off by default: a second recorder that\n"
        "                         silently takes half the traffic is a failure\n"
        "                         with no symptom.\n"
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
        "                         This format's own header key: a capture that\n"
        "                         cannot say which device produced it cannot\n"
        "                         support this adapter's claim to keep device\n"
        "                         state a relay drops.\n"
        "  --source-id ID         Name for this capture, recorded as\n"
        "                         provenance.\n"
        "                         These three become capture header values, and\n"
        "                         the format carries one token per key - so a\n"
        "                         value with a space in it is refused here\n"
        "                         rather than written into a file the reader\n"
        "                         would then refuse. Join words with '-'.\n"
        "  --dry-run              Listen and report, write nothing.\n"
        "\n"
        "Noticing a device that is not there:\n"
        "  --silence-timeout S    Report VRM_MOCOPI_DEVICE_UNAVAILABLE after S\n"
        "                         seconds with nothing arriving, once per\n"
        "                         episode, and keep listening. Off by default,\n"
        "                         because how long a device may take to start\n"
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
        "  --export-trace PATH    Write what the adapter delivered as a\n"
        "                         motion-capture-trace, which the product's own\n"
        "                         motion_capture replays knowing nothing about\n"
        "                         mocopi. This is the one thing here that\n"
        "                         decodes, and it is why it runs against a file:\n"
        "                         a recording stays decoder-free, and a trace\n"
        "                         exported from committed bytes is the same\n"
        "                         trace on any machine.\n"
        "  --source-session N     Which of the capture's sessions to export,\n"
        "                         counting from 1. One trace is one session, so\n"
        "                         a capture the source restarted during is\n"
        "                         refused until this names a half - the two\n"
        "                         clocks overlap and splicing them would invent\n"
        "                         a continuity the device denies.\n"
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
    // indistinguishable afterwards from their own defaults -- `--port 12351`
    // and `--duration 0` both parse to what the struct already held.
    const char* sessionFlag = nullptr;
    const auto session = [&sessionFlag](const char* flag) {
        if (!sessionFlag) {
            sessionFlag = flag;
        }
    };

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
            // 256 MB. Every platform clamps this far lower, so the bound is
            // only here to refuse a typo before the socket does. 0 is a real
            // value here and means "leave the platform default".
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
            if (!TakeSeconds(arguments, &i, argument,
                             &options->durationSeconds, error)) {
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
            // Present-but-empty is refused rather than ignored, which is the
            // correction `SplitEndpoint` already carries for `--listen` and for
            // the same reason. An empty path leaves this field
            // indistinguishable from the flag never having been given, so the
            // tool would read the capture, write nothing, and exit 0 — a silent
            // default, which is the one outcome an argument parser must not
            // reach because it cannot be told apart from being obeyed.
            if (options->traceExportPath.empty()) {
                *error = "--export-trace names the trace to write and was "
                         "given an empty path";
                return false;
            }
        } else if (argument == "--source-session") {
            // The count's own minimum, so the range in the message is the range
            // enforced: a capture cannot hold a zeroth session, and the sibling
            // had to refuse 0 in a second check below its range check.
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

    if (options->sourceSession != 0 && options->traceExportPath.empty()) {
        *error = "--source-session says which session to export, so it needs "
                 "--export-trace";
        return false;
    }

    // The export must not be pointed at the capture it is reading, and what is
    // at stake is why it is refused in two places rather than one. A capture
    // recorded off a device is a session that cannot be re-recorded and, unlike
    // every other corpus here, has no upstream to fetch it back from; the trace
    // is derived from it and can be rebuilt from nothing else. Writing one over
    // the other destroys the irreplaceable half to produce the reproducible
    // half — and measured before it was fixed, it exited 0 and printed a report
    // describing 43,499 bytes of datagrams that no longer existed.
    //
    // **This check is not sufficient on its own, and that was verified rather
    // than assumed**: with only this one in place, `--export-trace
    // ./capture.mocopipackets` against `--inspect capture.mocopipackets` still
    // destroyed the file. Two spellings of one path are not one string.
    // `ExportTrace` therefore asks the filesystem, which is the check that
    // catches every spelling. This one stays because it is the one that can
    // refuse at the prompt, before a byte is read, naming both flags.
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
        // The two export flags are deliberately outside that class and are the
        // only ones that ever will be: they change what is written *out of* a
        // capture rather than how one is recorded, which is the same exception
        // the sibling tool makes. Until they landed this list was empty rather
        // than short, and the reason is worth keeping — this mode reads bytes,
        // so a flag only earns an exception by having a file to act on.
        // `--quiet` is not in this class at all: it is about the two output
        // streams, which both modes have.
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
        // message names the two commands rather than only refusing, because the
        // operator asking for this has a device strapped on and the recording
        // they want is still the right first step.
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

} // namespace mocopiRecordTool
