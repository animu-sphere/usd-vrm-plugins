// SPDX-License-Identifier: Apache-2.0
#include "motionSource/SourceProfileFile.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace motionSource
{
namespace
{

constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";

bool
IsSpace(char c) noexcept
{
    return c == ' ' || c == '\t';
}

char
LowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool
EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (LowerAscii(lhs[i]) != LowerAscii(rhs[i])) {
            return false;
        }
    }
    return true;
}

std::string_view
Trim(std::string_view text) noexcept
{
    std::size_t begin = 0;
    while (begin < text.size() && IsSpace(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && IsSpace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool
Fail(SourceProfileParseError* error, std::size_t line, std::string reason)
{
    if (error) {
        error->line = line;
        error->reason = std::move(reason);
    }
    return false;
}

std::string
Quoted(std::string_view text)
{
    return "'" + std::string(text) + "'";
}

// --- the small language --------------------------------------------------
//
// One physical line, with its indentation measured and its comment gone. Blank
// lines never reach here: a reader that carried them would have to skip them at
// every one of the four places below that asks what the next line is.
struct SourceLine
{
    std::string_view text;
    std::size_t number = 0;
    std::size_t indent = 0;
};

// A `#` inside a quoted value is part of the value, and one that follows a
// non-space character is too -- an id may carry one. Only a `#` that opens a
// word is a comment.
std::string_view
StripComment(std::string_view line) noexcept
{
    bool quoted = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quoted && c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && c == '#' && (i == 0 || IsSpace(line[i - 1]))) {
            return line.substr(0, i);
        }
    }
    return line;
}

bool
SplitLines(std::string_view text, std::vector<SourceLine>* out,
           SourceProfileParseError* error)
{
    std::size_t number = 0;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        ++number;
        const std::size_t newline = text.find('\n', begin);
        const std::size_t end =
            (newline == std::string_view::npos) ? text.size() : newline;
        std::string_view line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        std::size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') {
            ++indent;
        }
        if (indent < line.size() && line[indent] == '\t') {
            // Refused rather than counted as some number of spaces. A file
            // indented with tabs has a structure that depends on a setting no
            // reader of it can see, and this reader's whole shape is that a
            // profile's structure is the one thing nobody may have to guess at.
            return Fail(error, number, "the indentation carries a tab");
        }
        line = Trim(StripComment(line.substr(indent)));
        if (!line.empty()) {
            out->push_back(SourceLine{line, number, indent});
        }

        if (newline == std::string_view::npos) {
            break;
        }
        begin = newline + 1;
    }
    return true;
}

struct Node
{
    enum class Kind
    {
        Scalar,
        Mapping,
        Sequence,
    };

    Kind kind = Kind::Scalar;
    std::string scalar;
    // Ordered, and that is the point: a profile's joint map is reported in its
    // declaration order, so the order the lines were written in is data
    // (SourceProfile.h). A sorted container here would lose it silently.
    std::vector<std::pair<std::string, Node>> entries;
    std::vector<Node> items;
    // The line this value's *key* was written on, so an error about a mapping
    // points at the key rather than at the first of its children.
    std::size_t line = 0;

    const Node* Find(std::string_view key) const
    {
        for (const std::pair<std::string, Node>& entry : entries) {
            if (entry.first == key) {
                return &entry.second;
            }
        }
        return nullptr;
    }
};

std::string_view
KindName(Node::Kind kind) noexcept
{
    switch (kind) {
    case Node::Kind::Scalar:
        return "a value";
    case Node::Kind::Mapping:
        return "a mapping";
    case Node::Kind::Sequence:
        return "a sequence";
    }
    return "a value";
}

bool
IsSequenceItem(std::string_view text) noexcept
{
    return text == "-" || (text.size() > 1 && text[0] == '-' && text[1] == ' ');
}

class Reader
{
public:
    Reader(const std::vector<SourceLine>& lines, SourceProfileParseError* error)
        : lines_(lines)
        , error_(error)
    {
    }

    bool ReadDocument(Node* out)
    {
        if (lines_.empty()) {
            return Fail(error_, 0, "the file states nothing");
        }
        if (lines_.front().indent != 0) {
            return Fail(error_, lines_.front().number,
                        "the first line is indented; a profile is a mapping at "
                        "the left margin");
        }
        if (!ReadMapping(0, out)) {
            return false;
        }
        // Line 0: the document is a mapping, and a value that is not part of it
        // is about the file rather than about the line it happens to sit on.
        out->line = 0;
        return true;
    }

private:
    bool ReadMapping(std::size_t indent, Node* out)
    {
        out->kind = Node::Kind::Mapping;
        out->line = lines_[index_].number;
        while (index_ < lines_.size()) {
            const SourceLine line = lines_[index_];
            if (line.indent < indent) {
                break;
            }
            if (line.indent > indent) {
                return Fail(error_, line.number, "unexpected indentation");
            }
            if (IsSequenceItem(line.text)) {
                return Fail(error_, line.number,
                            "expected 'key: value'; this is a sequence item");
            }

            std::string key;
            std::string_view rest;
            if (!SplitKeyValue(line.text, line.number, &key, &rest)) {
                return false;
            }
            if (out->Find(key)) {
                return Fail(error_, line.number,
                            "key " + Quoted(key) + " is stated twice");
            }
            ++index_;

            Node value;
            if (rest.empty()) {
                if (index_ >= lines_.size() || lines_[index_].indent <= indent) {
                    return Fail(error_, line.number,
                                "key " + Quoted(key) + " states no value");
                }
                const std::size_t childIndent = lines_[index_].indent;
                const bool ok = IsSequenceItem(lines_[index_].text)
                                    ? ReadSequence(childIndent, &value)
                                    : ReadMapping(childIndent, &value);
                if (!ok) {
                    return false;
                }
                value.line = line.number;
            } else if (!ReadInline(rest, line.number, &value)) {
                return false;
            }
            out->entries.emplace_back(std::move(key), std::move(value));
        }
        return true;
    }

    bool ReadSequence(std::size_t indent, Node* out)
    {
        out->kind = Node::Kind::Sequence;
        out->line = lines_[index_].number;
        while (index_ < lines_.size()) {
            const SourceLine line = lines_[index_];
            if (line.indent < indent) {
                break;
            }
            if (line.indent > indent) {
                return Fail(error_, line.number, "unexpected indentation");
            }
            if (!IsSequenceItem(line.text)) {
                return Fail(error_, line.number, "expected '- value'");
            }
            const std::string_view rest = Trim(line.text.substr(1));
            if (rest.empty()) {
                return Fail(error_, line.number,
                            "the sequence item states no value");
            }
            Node item;
            item.line = line.number;
            if (!ReadScalar(rest, line.number, &item.scalar)) {
                return false;
            }
            out->items.push_back(std::move(item));
            ++index_;
        }
        return true;
    }

    bool ReadInline(std::string_view text, std::size_t line, Node* out)
    {
        out->line = line;
        if (text.front() == '{') {
            return ReadFlowMapping(text, line, out);
        }
        if (text.front() == '[') {
            return ReadFlowSequence(text, line, out);
        }
        out->kind = Node::Kind::Scalar;
        return ReadScalar(text, line, &out->scalar);
    }

    bool ReadFlowMapping(std::string_view text, std::size_t line, Node* out)
    {
        out->kind = Node::Kind::Mapping;
        std::vector<std::string_view> parts;
        if (!SplitFlow(text, '}', line, &parts)) {
            return false;
        }
        for (const std::string_view part : parts) {
            std::string key;
            std::string_view rest;
            if (!SplitKeyValue(part, line, &key, &rest)) {
                return false;
            }
            if (out->Find(key)) {
                return Fail(error_, line,
                            "key " + Quoted(key) + " is stated twice");
            }
            Node value;
            value.line = line;
            value.kind = Node::Kind::Scalar;
            if (!ReadScalar(rest, line, &value.scalar)) {
                return false;
            }
            out->entries.emplace_back(std::move(key), std::move(value));
        }
        return true;
    }

    bool ReadFlowSequence(std::string_view text, std::size_t line, Node* out)
    {
        out->kind = Node::Kind::Sequence;
        std::vector<std::string_view> parts;
        if (!SplitFlow(text, ']', line, &parts)) {
            return false;
        }
        for (const std::string_view part : parts) {
            Node item;
            item.line = line;
            if (!ReadScalar(part, line, &item.scalar)) {
                return false;
            }
            out->items.push_back(std::move(item));
        }
        return true;
    }

    // The inside of a `{...}` or `[...]`, split on the commas that are not
    // inside a quoted value. Neither form nests: a nested opener survives into a
    // part and `ReadScalar` refuses it there, which keeps the one rule -- a flow
    // value holds scalars -- in one place.
    bool SplitFlow(std::string_view text, char close, std::size_t line,
                   std::vector<std::string_view>* parts)
    {
        if (text.size() < 2 || text.back() != close) {
            return Fail(error_, line,
                        std::string("expected a closing '") + close + "'");
        }
        const std::string_view inner = Trim(text.substr(1, text.size() - 2));
        if (inner.empty()) {
            return true;
        }
        bool quoted = false;
        bool escaped = false;
        std::size_t start = 0;
        for (std::size_t i = 0; i < inner.size(); ++i) {
            const char c = inner[i];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (quoted && c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '"') {
                quoted = !quoted;
                continue;
            }
            if (!quoted && c == ',') {
                parts->push_back(Trim(inner.substr(start, i - start)));
                start = i + 1;
            }
        }
        if (quoted) {
            return Fail(error_, line, "a quoted value has no closing '\"'");
        }
        parts->push_back(Trim(inner.substr(start)));
        for (const std::string_view part : *parts) {
            if (part.empty()) {
                return Fail(error_, line, "an entry between commas is empty");
            }
        }
        return true;
    }

    // Splits `key: value`. The colon that ends a key is one followed by a space
    // or by nothing, which is what lets an unquoted name carry a colon of its
    // own -- a joint name is the writer's word and this reader does not get to
    // reserve characters in it.
    bool SplitKeyValue(std::string_view text, std::size_t line, std::string* key,
                       std::string_view* rest)
    {
        if (!text.empty() && text.front() == '"') {
            std::size_t end = 0;
            if (!ReadQuoted(text, line, key, &end)) {
                return false;
            }
            const std::string_view after = Trim(text.substr(end));
            if (after.empty() || after.front() != ':') {
                return Fail(error_, line, "expected ':' after the key");
            }
            *rest = Trim(after.substr(1));
            return true;
        }

        std::size_t colon = std::string_view::npos;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != ':') {
                continue;
            }
            if (i + 1 == text.size() || IsSpace(text[i + 1])) {
                colon = i;
                break;
            }
        }
        if (colon == std::string_view::npos) {
            return Fail(error_, line, "expected 'key: value'");
        }
        const std::string_view name = Trim(text.substr(0, colon));
        if (name.empty()) {
            return Fail(error_, line, "the line states no key");
        }
        if (name.find_first_of("{}[],\"#") != std::string_view::npos) {
            return Fail(error_, line,
                        "key " + Quoted(name)
                            + " carries a character a plain key may not; quote "
                              "it");
        }
        *key = std::string(name);
        *rest = Trim(text.substr(colon + 1));
        return true;
    }

    bool ReadQuoted(std::string_view text, std::size_t line, std::string* out,
                    std::size_t* end)
    {
        std::string value;
        for (std::size_t i = 1; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '\\') {
                if (i + 1 >= text.size()) {
                    break;
                }
                const char next = text[i + 1];
                if (next != '"' && next != '\\') {
                    return Fail(error_, line,
                                std::string("'\\") + next
                                    + "' is not an escape; a quoted value "
                                      "escapes only \\\" and \\\\");
                }
                value.push_back(next);
                ++i;
                continue;
            }
            if (c == '"') {
                *out = std::move(value);
                *end = i + 1;
                return true;
            }
            value.push_back(c);
        }
        return Fail(error_, line, "a quoted value has no closing '\"'");
    }

    bool ReadScalar(std::string_view text, std::size_t line, std::string* out)
    {
        if (text.empty()) {
            return Fail(error_, line, "expected a value");
        }
        if (text.front() == '"') {
            std::size_t end = 0;
            if (!ReadQuoted(text, line, out, &end)) {
                return false;
            }
            if (!Trim(text.substr(end)).empty()) {
                return Fail(error_, line,
                            "a quoted value is followed by more text");
            }
            return true;
        }
        if (text.find_first_of("{}[]") != std::string_view::npos) {
            return Fail(error_, line,
                        "value " + Quoted(text)
                            + " carries a flow character; quote it");
        }
        *out = std::string(text);
        return true;
    }

    const std::vector<SourceLine>& lines_;
    std::size_t index_ = 0;
    SourceProfileParseError* error_;
};

// --- the keys ------------------------------------------------------------

bool
RequireKind(const Node& node, Node::Kind kind, std::string_view what,
            SourceProfileParseError* error)
{
    if (node.kind == kind) {
        return true;
    }
    return Fail(error, node.line,
                std::string(what) + " must be " + std::string(KindName(kind))
                    + "; it is " + std::string(KindName(node.kind)));
}

const Node*
RequireKey(const Node& mapping, std::string_view key, std::string_view what,
           SourceProfileParseError* error)
{
    const Node* node = mapping.Find(key);
    if (!node) {
        Fail(error, mapping.line,
             std::string(what) + " states no " + Quoted(key));
        return nullptr;
    }
    return node;
}

// Unknown keys are refused rather than ignored, which is the whole reason this
// reader exists in this shape: a misspelled `requred:` that a permissive reader
// dropped would bind an arm the profile said was mandatory and report nothing.
bool
OnlyKeys(const Node& mapping, std::initializer_list<std::string_view> allowed,
         std::string_view what, SourceProfileParseError* error)
{
    for (const std::pair<std::string, Node>& entry : mapping.entries) {
        if (std::find(allowed.begin(), allowed.end(), entry.first)
            != allowed.end()) {
            continue;
        }
        std::string list;
        for (const std::string_view key : allowed) {
            if (!list.empty()) {
                list += ", ";
            }
            list += std::string(key);
        }
        return Fail(error, entry.second.line,
                    std::string(what) + " states no key "
                        + Quoted(entry.first) + "; it states " + list);
    }
    return true;
}

bool
ReadString(const Node& node, std::string_view what, std::string* out,
           SourceProfileParseError* error)
{
    if (!RequireKind(node, Node::Kind::Scalar, what, error)) {
        return false;
    }
    *out = node.scalar;
    return true;
}

bool
ReadBool(const Node& node, std::string_view what, bool* out,
         SourceProfileParseError* error)
{
    if (!RequireKind(node, Node::Kind::Scalar, what, error)) {
        return false;
    }
    if (EqualsIgnoreCase(node.scalar, "true")) {
        *out = true;
        return true;
    }
    if (EqualsIgnoreCase(node.scalar, "false")) {
        *out = false;
        return true;
    }
    return Fail(error, node.line,
                std::string(what) + ": " + Quoted(node.scalar)
                    + " is not true or false");
}

// One vocabulary term, looked up rather than interpreted. The refusal prints
// every word the vocabulary has, which is what the `...Count` constants beside
// each enum are for -- a list written out here instead would be the second
// vocabulary `SourceProfile.h` exists to prevent, one enumerator behind.
//
// Index 0 is `Unspecified` in every one of them, so a file that writes the word
// is refused here with the accepted list rather than three checks later with
// "states no handedness". Nothing may state it: it is the value a profile nobody
// finished has.
template <typename Enum, typename Find, typename Name>
bool
ReadTerm(const Node& node, std::string_view what, std::size_t count, Find find,
         Name name, Enum* out, SourceProfileParseError* error)
{
    if (!RequireKind(node, Node::Kind::Scalar, what, error)) {
        return false;
    }
    const std::optional<Enum> value = find(node.scalar);
    if (value && static_cast<std::size_t>(*value) != 0) {
        *out = *value;
        return true;
    }
    std::string list;
    for (std::size_t index = 1; index < count; ++index) {
        if (!list.empty()) {
            list += ", ";
        }
        list += std::string(name(static_cast<Enum>(index)));
    }
    return Fail(error, node.line,
                std::string(what) + ": " + Quoted(node.scalar)
                    + " is not one of " + list);
}

bool
BuildProfile(const Node& document, SourceProfile* profile,
             SourceProfileParseError* error)
{
    if (!RequireKind(document, Node::Kind::Mapping, "a profile", error)
        || !OnlyKeys(document,
                     {"schemaVersion", "id", "producer", "coordinates", "root",
                      "restPose", "unmappedJoints", "joints", "ignoredJoints"},
                     "a profile", error)) {
        return false;
    }

    const Node* version = RequireKey(document, "schemaVersion", "a profile",
                                     error);
    if (!version
        || !RequireKind(*version, Node::Kind::Scalar, "schemaVersion", error)) {
        return false;
    }
    // Compared as the text it was written as, not as a parsed number: `1.0` and
    // `01` are files somebody wrote by hand against a different idea of this
    // key, and accepting them would make the version the one field this reader
    // guesses at.
    const std::string expectedVersion =
        std::to_string(SourceProfileSchemaVersion);
    if (version->scalar != expectedVersion) {
        return Fail(error, version->line,
                    "schemaVersion " + Quoted(version->scalar) + " is not "
                        + expectedVersion + ", the version these keys are");
    }

    const Node* id = RequireKey(document, "id", "a profile", error);
    if (!id || !ReadString(*id, "id", &profile->id, error)) {
        return false;
    }
    const Node* producer = RequireKey(document, "producer", "a profile", error);
    if (!producer
        || !ReadString(*producer, "producer", &profile->producer, error)) {
        return false;
    }

    const Node* coordinates = RequireKey(document, "coordinates", "a profile",
                                         error);
    if (!coordinates
        || !RequireKind(*coordinates, Node::Kind::Mapping, "coordinates", error)
        || !OnlyKeys(*coordinates,
                     {"handedness", "upAxis", "forwardAxis", "translationUnit"},
                     "coordinates", error)) {
        return false;
    }
    const Node* handedness = RequireKey(*coordinates, "handedness",
                                        "coordinates", error);
    if (!handedness
        || !ReadTerm(*handedness, "coordinates.handedness",
                     SourceHandednessCount, FindSourceHandedness,
                     SourceHandednessName, &profile->handedness, error)) {
        return false;
    }
    const Node* upAxis = RequireKey(*coordinates, "upAxis", "coordinates",
                                    error);
    if (!upAxis
        || !ReadTerm(*upAxis, "coordinates.upAxis", SourceAxisCount,
                     FindSourceAxis, SourceAxisName, &profile->upAxis, error)) {
        return false;
    }
    const Node* forwardAxis = RequireKey(*coordinates, "forwardAxis",
                                         "coordinates", error);
    if (!forwardAxis
        || !ReadTerm(*forwardAxis, "coordinates.forwardAxis", SourceAxisCount,
                     FindSourceAxis, SourceAxisName, &profile->forwardAxis,
                     error)) {
        return false;
    }
    const Node* unit = RequireKey(*coordinates, "translationUnit",
                                  "coordinates", error);
    if (!unit
        || !ReadTerm(*unit, "coordinates.translationUnit", SourceLengthUnitCount,
                     FindSourceLengthUnit, SourceLengthUnitName,
                     &profile->translationUnit, error)) {
        return false;
    }

    const Node* root = RequireKey(document, "root", "a profile", error);
    if (!root || !RequireKind(*root, Node::Kind::Mapping, "root", error)
        || !OnlyKeys(*root, {"joint", "translation", "rotation"}, "root",
                     error)) {
        return false;
    }
    const Node* rootJoint = RequireKey(*root, "joint", "root", error);
    if (!rootJoint
        || !ReadString(*rootJoint, "root.joint", &profile->rootJoint, error)) {
        return false;
    }
    const Node* rootTranslation = RequireKey(*root, "translation", "root",
                                             error);
    if (!rootTranslation
        || !ReadTerm(*rootTranslation, "root.translation",
                     RootTranslationPolicyCount, FindRootTranslationPolicy,
                     RootTranslationPolicyName, &profile->rootTranslation,
                     error)) {
        return false;
    }
    const Node* rootRotation = RequireKey(*root, "rotation", "root", error);
    if (!rootRotation
        || !ReadTerm(*rootRotation, "root.rotation", RootRotationPolicyCount,
                     FindRootRotationPolicy, RootRotationPolicyName,
                     &profile->rootRotation, error)) {
        return false;
    }

    const Node* restPose = RequireKey(document, "restPose", "a profile", error);
    if (!restPose
        || !ReadTerm(*restPose, "restPose", RestPoseSourceCount,
                     FindRestPoseSource, RestPoseSourceName, &profile->restPose,
                     error)) {
        return false;
    }
    const Node* unmapped = RequireKey(document, "unmappedJoints", "a profile",
                                      error);
    if (!unmapped
        || !ReadTerm(*unmapped, "unmappedJoints", UnmappedJointPolicyCount,
                     FindUnmappedJointPolicy, UnmappedJointPolicyName,
                     &profile->unmappedJoints, error)) {
        return false;
    }

    const Node* joints = RequireKey(document, "joints", "a profile", error);
    if (!joints || !RequireKind(*joints, Node::Kind::Mapping, "joints", error)) {
        return false;
    }
    for (const std::pair<std::string, Node>& entry : joints->entries) {
        const std::string what = "joint " + Quoted(entry.first);
        if (!RequireKind(entry.second, Node::Kind::Mapping, what, error)
            || !OnlyKeys(entry.second, {"bone", "required"}, what, error)) {
            return false;
        }
        SourceJointMapping mapping;
        mapping.sourceName = entry.first;

        const Node* bone = RequireKey(entry.second, "bone", what, error);
        if (!bone
            || !RequireKind(*bone, Node::Kind::Scalar, what + ".bone", error)) {
            return false;
        }
        const std::optional<motion::HumanBone> named =
            motion::FindHumanBone(bone->scalar);
        if (!named || !motion::IsValidHumanBone(*named)) {
            // The one vocabulary whose words are not listed back: fifty-five
            // bone names in a refusal is a wall of text, and the humanoid
            // vocabulary is the one a profile's author already has in front of
            // them.
            return Fail(error, bone->line,
                        what + ".bone: " + Quoted(bone->scalar)
                            + " is not a canonical humanoid bone");
        }
        mapping.bone = *named;

        if (const Node* required = entry.second.Find("required")) {
            if (!ReadBool(*required, what + ".required", &mapping.required,
                          error)) {
                return false;
            }
        }
        profile->joints.push_back(std::move(mapping));
    }

    // The one optional key. A profile that deliberately ignores nothing states
    // nothing, rather than an empty sequence nobody would read as a claim.
    if (const Node* ignored = document.Find("ignoredJoints")) {
        if (!RequireKind(*ignored, Node::Kind::Sequence, "ignoredJoints",
                         error)) {
            return false;
        }
        for (const Node& item : ignored->items) {
            profile->ignoredJoints.push_back(item.scalar);
        }
    }

    // Line 0: every remaining invariant is about the profile rather than about
    // any line of it. "maps no hips" is true of the file, and pointing at a line
    // would send a reader to one with nothing wrong on it.
    std::string reason;
    if (!ValidateSourceProfile(*profile, &reason)) {
        return Fail(error, 0, std::move(reason));
    }
    return true;
}

} // namespace

bool
ParseSourceProfileText(std::string_view text, SourceProfile* profile,
                       SourceProfileParseError* error)
{
    if (error) {
        error->reason.clear();
        error->line = 0;
    }
    if (!profile) {
        return Fail(error, 0, "there is nowhere to parse into");
    }
    if (text.size() >= kByteOrderMark.size()
        && text.substr(0, kByteOrderMark.size()) == kByteOrderMark) {
        text.remove_prefix(kByteOrderMark.size());
    }

    std::vector<SourceLine> lines;
    if (!SplitLines(text, &lines, error)) {
        return false;
    }
    Node document;
    Reader reader(lines, error);
    if (!reader.ReadDocument(&document)) {
        return false;
    }

    // Built into a local and moved on success only, so a refused file leaves
    // the caller's profile as it was rather than half rewritten -- the parser
    // rule the reader below this layer states for the same reason.
    SourceProfile parsed;
    if (!BuildProfile(document, &parsed, error)) {
        return false;
    }
    *profile = std::move(parsed);
    return true;
}

bool
ParseSourceProfileFile(const std::filesystem::path& path,
                       SourceProfile* profile, SourceProfileParseError* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Fail(error, 0, "the file could not be opened");
    }
    std::string text((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    if (file.bad()) {
        return Fail(error, 0, "the file could not be read");
    }
    return ParseSourceProfileText(text, profile, error);
}

} // namespace motionSource
