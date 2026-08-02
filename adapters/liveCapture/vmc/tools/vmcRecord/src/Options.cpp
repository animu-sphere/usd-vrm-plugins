// SPDX-License-Identifier: Apache-2.0
#include "Options.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>

namespace vmcRecordTool
{
namespace
{

// A session is held in memory until it is written, because the capture writer
// emits a whole capture at once -- that is what makes its output canonical and
// therefore comparable (PacketCapture.h). So the bound on how long a recording
// may run is a bound on memory, and it exists rather than being optional: a
// recorder left running overnight against a per-message sender would otherwise
// end as an out-of-memory kill with nothing written at all.
//
// A million datagrams is ten minutes of the densest sender shape the corpus
// holds (55 bones, a root and a clock, per message, at 30 Hz) and roughly 60 MB
// of payload. An operator recording longer says so.
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

// Splits "127.0.0.1:39539", "0.0.0.0", "[::1]:39539" or ":39539" into its two
// halves. A bracketed IPv6 literal is why this is not a search for the last
// colon in the string, and an unbracketed one carrying a port is refused rather
// than guessed at: "::1:39539" is a valid address in its own right.
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
        "vmc_record - record a live VMC session, and say what it was\n"
        "\n"
        "Writes the datagrams a sender delivered, verbatim, in the\n"
        "vmc-packet-capture format the adapter's corpus is written in - and\n"
        "reports what they decoded to, so an operator learns whether the\n"
        "session is worth committing before it is committed. Recording and\n"
        "decoding are separate: the datagram reaches the file before the\n"
        "decoder sees it, so nothing the decoder makes of it can change what\n"
        "was recorded.\n"
        "\n"
        "Usage:\n"
        "  vmc_record --output <session.vmcpackets> [options]\n"
        "  vmc_record --dry-run [options]\n"
        "  vmc_record --inspect <capture.vmcpackets>\n"
        "\n"
        "Listening:\n"
        "  --listen ADDR[:PORT]   Bind address, numeric (default 0.0.0.0).\n"
        "                         '0.0.0.0' every IPv4 interface, '127.0.0.1'\n"
        "                         this machine only, '[::]' every IPv6 one.\n"
        "  --port N               Listen port (default 39539, VMC's own).\n"
        "                         0 lets the OS choose and is reported back.\n"
        "  --reuse-address        Allow binding a port another socket holds.\n"
        "                         Off by default: a second recorder that\n"
        "                         silently takes half the traffic is a failure\n"
        "                         with no symptom.\n"
        "  --receive-buffer BYTES Kernel receive buffer to request. What was\n"
        "                         granted is reported, which is not always what\n"
        "                         was asked for.\n"
        "\n"
        "Recording:\n"
        "  --output PATH          Capture to write.\n"
        "  --sender NAME          Sender application, recorded as provenance.\n"
        "  --source-id ID         Name for this capture, recorded as\n"
        "                         provenance. Never taken from the sender's\n"
        "                         model title - see the report's 'model' line.\n"
        "  --dry-run              Listen and report, write nothing.\n"
        "\n"
        "Stopping (a session always has at least one):\n"
        "  --duration S           Stop after S seconds of session.\n"
        "  --idle-timeout S       Stop after S seconds with nothing arriving.\n"
        "  --max-datagrams N      Stop after N datagrams (default 1000000).\n"
        "  Ctrl-C stops at any point and still writes what was recorded.\n"
        "\n"
        "Reading:\n"
        "  --inspect PATH         Decode a recorded capture and report on it.\n"
        "                         Opens no socket, records nothing, and prints\n"
        "                         the same report a live session prints.\n"
        "  --staleness S          Seconds before an unreported bone is called\n"
        "                         stale (default 0.5; 0 disables).\n"
        "  --restart-backwards S  A sender clock going back further than this\n"
        "                         is a restart rather than a regression\n"
        "                         (default 1.0; 0 disables restart detection).\n"
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
    // indistinguishable afterwards from their own defaults -- `--port 39539`
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
        } else if (argument == "--source-id") {
            session("--source-id");
            if (!TakeValue(arguments, &i, argument, &options->sourceId,
                           error)) {
                return false;
            }
        } else if (argument == "--duration") {
            session("--duration");
            if (!TakeDouble(arguments, &i, argument, &options->durationSeconds,
                            error)) {
                return false;
            }
            if (options->durationSeconds < 0.0
                || options->durationSeconds > kMaxDurationSeconds) {
                *error = "--duration expects a non-negative number of seconds "
                         "no greater than "
                    + std::to_string(static_cast<long long>(
                          kMaxDurationSeconds));
                return false;
            }
        } else if (argument == "--idle-timeout") {
            session("--idle-timeout");
            if (!TakeDouble(arguments, &i, argument, &options->idleSeconds,
                            error)) {
                return false;
            }
            if (options->idleSeconds < 0.0
                || options->idleSeconds > kMaxDurationSeconds) {
                *error = "--idle-timeout expects a non-negative number of "
                         "seconds no greater than "
                    + std::to_string(static_cast<long long>(
                          kMaxDurationSeconds));
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
        } else if (argument == "--staleness") {
            double seconds = 0.0;
            if (!TakeDouble(arguments, &i, argument, &seconds, error)) {
                return false;
            }
            if (seconds < 0.0) {
                *error = "--staleness cannot be negative";
                return false;
            }
            options->frame.stalenessSeconds = seconds;
        } else if (argument == "--restart-backwards") {
            double seconds = 0.0;
            if (!TakeDouble(arguments, &i, argument, &seconds, error)) {
                return false;
            }
            if (seconds < 0.0) {
                *error = "--restart-backwards cannot be negative";
                return false;
            }
            options->frame.restartBackwardsSeconds = seconds;
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
        // --inspect opens no socket and writes no file, so every flag about
        // either is a mistake worth naming rather than a setting that silently
        // does nothing. `--staleness` and `--restart-backwards` are the two
        // that survive: they are how a capture is *read*, which is the whole of
        // what this mode does.
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

} // namespace vmcRecordTool
