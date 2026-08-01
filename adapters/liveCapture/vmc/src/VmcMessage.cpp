// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVmc/VmcMessage.h"

#include <string>
#include <utility>

namespace vrmAdapterVmc
{

namespace
{

// One entry per kind, in enum order, so the address, the required tags and the
// optional tags cannot drift apart the way three separate switch statements
// would. Diagnostics.cpp uses the same shape for the same reason.
//
// `optional` is what the decoder reads *if the sender sent it in that exact
// type*; anything past `required + optional`, and anything inside `optional`
// whose tag disagrees, is counted into `unreadArguments` instead. The two lists
// are deliberately short: a form belongs here when a committed capture pins it,
// and a sender's longer variant is a count until one does.
struct Form
{
    std::string_view address;
    std::string_view required;
    std::string_view optional;
};

constexpr std::array<Form, VmcMessageKindCount> kForms{{
    {"/VMC/Ext/OK", "i", "ii"},
    {"/VMC/Ext/T", "f", ""},
    {"/VMC/Ext/VRM", "ss", ""},
    {"/VMC/Ext/Root/Pos", "sfffffff", ""},
    {"/VMC/Ext/Bone/Pos", "sfffffff", ""},
    {"/VMC/Ext/Blend/Val", "sf", ""},
    {"/VMC/Ext/Blend/Apply", "", ""},
}};

const Form* Entry(VmcMessageKind kind) noexcept
{
    const auto index = static_cast<std::size_t>(kind);
    return index < kForms.size() ? &kForms[index] : nullptr;
}

bool Refuse(Diagnostic* diagnostic, DiagnosticCode code,
            std::string_view address, std::string detail)
{
    if (diagnostic) {
        *diagnostic = MakeDiagnostic(code, std::move(detail));
        diagnostic->subject.assign(address);
    }
    return false;
}

// The tag string as it appears on the wire, so a diagnostic quotes what a
// reader will find in the capture rather than a de-comma'd version of it.
std::string Quoted(std::string_view tags)
{
    std::string text = "\",";
    text.append(tags);
    text += '"';
    return text;
}

float Real32(const OscArgument& argument)
{
    return static_cast<float>(argument.real);
}

} // namespace

std::string_view
VmcMessageKindAddress(VmcMessageKind kind) noexcept
{
    const Form* form = Entry(kind);
    return form ? form->address : std::string_view();
}

std::string_view
VmcMessageKindTypeTags(VmcMessageKind kind) noexcept
{
    const Form* form = Entry(kind);
    return form ? form->required : std::string_view();
}

std::optional<VmcMessageKind>
FindVmcMessageKind(std::string_view address) noexcept
{
    for (std::size_t index = 0; index < kForms.size(); ++index) {
        if (kForms[index].address == address) {
            return static_cast<VmcMessageKind>(index);
        }
    }
    return std::nullopt;
}

bool
DecodeVmcMessage(const OscMessage& message, VmcMessage* out,
                 Diagnostic* diagnostic)
{
    if (!out) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed,
                      message.address, "no output message was provided");
    }
    // The OSC layer emits one argument per type tag, including the zero-width
    // ones. A message where the two disagree did not come from it, and reading
    // arguments by tag index would run off the end -- so it is refused here
    // rather than trusted because of where it usually comes from.
    if (message.arguments.size() != message.typeTags.size()) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed,
                      message.address,
                      "the type tags describe "
                          + std::to_string(message.typeTags.size())
                          + " argument(s) and "
                          + std::to_string(message.arguments.size())
                          + " were given");
    }

    const std::optional<VmcMessageKind> kind =
        FindVmcMessageKind(message.address);
    if (!kind) {
        // Info, not a defect: this is what a sender's headset, controller,
        // camera and MIDI traffic looks like from here, and every one of them
        // is well-formed OSC that this adapter has simply not been asked to
        // consume.
        return Refuse(diagnostic, DiagnosticCode::UnsupportedMessage,
                      message.address,
                      "no VMC message is decoded from this address");
    }

    const Form& form = kForms[static_cast<std::size_t>(*kind)];
    if (message.typeTags.substr(0, form.required.size()) != form.required) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed,
                      message.address,
                      "expects " + Quoted(form.required) + " and carries "
                          + Quoted(message.typeTags));
    }

    // Optional arguments are read while they match. The first that does not
    // ends the known form -- a sender at a version this decoder does not know
    // is reporting something, not doing something wrong, and guessing at the
    // remainder is how a decoder starts inventing values.
    std::size_t read = form.required.size();
    while (read < message.typeTags.size()) {
        const std::size_t extra = read - form.required.size();
        if (extra >= form.optional.size()
            || message.typeTags[read] != form.optional[extra]) {
            break;
        }
        ++read;
    }

    const std::vector<OscArgument>& arguments = message.arguments;
    VmcMessage decoded;
    decoded.kind = *kind;
    decoded.unreadArguments = arguments.size() - read;

    switch (*kind) {
    case VmcMessageKind::Availability:
        decoded.availability.loaded =
            static_cast<std::int32_t>(arguments[0].integer);
        if (read > 1) {
            decoded.availability.calibrationState =
                static_cast<std::int32_t>(arguments[1].integer);
        }
        if (read > 2) {
            decoded.availability.calibrationMode =
                static_cast<std::int32_t>(arguments[2].integer);
        }
        break;
    case VmcMessageKind::Time:
        decoded.seconds = arguments[0].real;
        break;
    case VmcMessageKind::Model:
        decoded.name = arguments[0].text;
        decoded.title = arguments[1].text;
        break;
    case VmcMessageKind::RootTransform:
    case VmcMessageKind::BoneTransform:
        decoded.name = arguments[0].text;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            decoded.transform.position[axis] = Real32(arguments[1 + axis]);
        }
        // x, y, z, w -- the order the wire uses, kept.
        for (std::size_t component = 0; component < 4; ++component) {
            decoded.transform.rotation[component] =
                Real32(arguments[4 + component]);
        }
        break;
    case VmcMessageKind::BlendValue:
        decoded.name = arguments[0].text;
        decoded.value = Real32(arguments[1]);
        break;
    case VmcMessageKind::BlendApply:
        break;
    case VmcMessageKind::Count:
        break;
    }

    *out = decoded;
    return true;
}

bool
DecodeVmcPacket(const OscPacket& packet, VmcPacket* out,
                std::vector<Diagnostic>* diagnostics)
{
    if (!out) {
        if (diagnostics) {
            diagnostics->push_back(
                MakeDiagnostic(DiagnosticCode::PacketMalformed,
                               "no output packet was provided"));
        }
        return false;
    }

    // Assigned once at the end, like the OSC layer -- but for the opposite
    // reason. There a refused datagram must leave the caller's packet alone;
    // here a refused *message* must not, because the messages either side of it
    // decoded and the assembler needs them. The local exists so `out` is
    // replaced exactly once rather than grown as the loop runs.
    VmcPacket result;
    result.messages.reserve(packet.messages.size());

    bool ok = true;
    for (std::size_t index = 0; index < packet.messages.size(); ++index) {
        VmcMessage decoded;
        Diagnostic diagnostic;
        if (DecodeVmcMessage(packet.messages[index], &decoded, &diagnostic)) {
            decoded.oscIndex = index;
            result.messages.push_back(decoded);
            continue;
        }

        if (diagnostic.code == DiagnosticCode::UnsupportedMessage) {
            ++result.unsupported;
        } else {
            ++result.malformed;
            ok = false;
        }
        if (diagnostics) {
            // Where in the datagram, in the same shape the OSC layer's "(at
            // byte N)" uses -- a bundle carries twenty-four messages and the
            // address alone does not say which one.
            diagnostic.detail += " (message " + std::to_string(index)
                + " of the datagram)";
            diagnostics->push_back(std::move(diagnostic));
        }
    }

    *out = std::move(result);
    return ok;
}

} // namespace vrmAdapterVmc
