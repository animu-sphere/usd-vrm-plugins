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

// The four helpers below are a second copy of the sibling tool's, and the
// trigger for making them one is the trigger `PacketCapture.h` states for the
// format and `UdpReceiver.h` restates for the socket: a **third** recorder. What
// is worth saying at this copy rather than there is how little of it is
// protocol-shaped -- an endpoint splitter and a bounded number parser are the
// same code for any UDP tool, so if the shared library those headers describe is
// ever written, this file is the cheapest part of it to move and the least
// interesting.

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

bool
TakeCount(const std::vector<std::string>& arguments, std::size_t* index,
          const std::string& flag, double limit, std::size_t* value,
          std::string* error)
{
    double parsed = 0.0;
    if (!TakeDouble(arguments, index, flag, &parsed, error)) {
        return false;
    }
    if (parsed < 0.0 || parsed > limit || parsed != std::floor(parsed)) {
        *error = flag + " expects a whole number between 0 and "
            + std::to_string(static_cast<long long>(limit));
        return false;
    }
    *value = static_cast<std::size_t>(parsed);
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
bool
SplitEndpoint(const std::string& text, std::string* address, std::string* port,
              std::string* error)
{
    address->clear();
    port->clear();
    if (text.empty()) {
        *error = "--listen expects an address, a port, or address:port";
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
        "                         'localhost' as unsupported.\n"
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
        "  --max-datagrams N      Stop after N datagrams (default 1000000).\n"
        "                         The capture is held in memory until it is\n"
        "                         written, so this bound is on memory.\n"
        "  Ctrl-C stops at any point and still writes what was recorded.\n"
        "\n"
        "Reading:\n"
        "  --inspect PATH         Read a recorded capture and report on it.\n"
        "                         Opens no socket, records nothing, and prints\n"
        "                         the same report a live session prints minus\n"
        "                         the lines that describe a socket.\n"
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
            // only here to refuse a typo before the socket does.
            if (!TakeCount(arguments, &i, argument, 268435456.0,
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
            if (!TakeValue(arguments, &i, argument, &options->sender, error)) {
                return false;
            }
        } else if (argument == "--device") {
            session("--device");
            if (!TakeValue(arguments, &i, argument, &options->device, error)) {
                return false;
            }
        } else if (argument == "--source-id") {
            session("--source-id");
            if (!TakeValue(arguments, &i, argument, &options->sourceId,
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
            if (!TakeCount(arguments, &i, argument,
                           static_cast<double>(kDefaultMaxDatagrams) * 10.0,
                           &options->maxDatagrams, error)) {
                return false;
            }
            if (options->maxDatagrams == 0) {
                *error = "--max-datagrams expects at least 1; a session with no "
                         "bound at all is not offered, because the capture is "
                         "held in memory until it is written";
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

    if (!options->inspectPath.empty()) {
        // --inspect opens no socket and records nothing, so every flag about
        // either is a mistake worth naming rather than a setting that silently
        // does nothing.
        //
        // *Every* one of them, here. The sibling tool lets three flags through
        // because they change how a capture is read or what is written out of
        // it; this tool reads bytes and writes nothing, so there is no such
        // flag to let through and the exception list is empty rather than
        // short. `--quiet` is not in this class at all: it is about the two
        // output streams, which both modes have.
        if (sessionFlag) {
            *error = std::string("--inspect reads a recorded capture and opens "
                                 "no socket, so ")
                + sessionFlag + " has nothing to act on";
            return false;
        }
        return true;
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
