// SPDX-License-Identifier: Apache-2.0

#include "motionBvh/BvhParser.h"

#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace motionBvh
{

namespace
{

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

constexpr char
ToLowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool
EqualsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (ToLowerAscii(lhs[index]) != ToLowerAscii(rhs[index])) {
            return false;
        }
    }
    return true;
}

constexpr bool
IsSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v'
           || c == '\f';
}

std::string
Count(std::size_t value)
{
    return std::to_string(value);
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

// `std::from_chars` for doubles is not available on every lane this repository
// builds on -- libc++ shipped the floating-point overloads years after libstdc++
// and MSVC did -- so the one code path everywhere is `strtod`, which is
// correctly rounded on all three.
//
// Its one hazard is the locale: `strtod` reads the *current* `LC_NUMERIC`
// decimal point, which belongs to the host application rather than to this
// library. Under a comma-decimal locale it would stop at the '.' in "1.5" and
// return 1, silently, for every value in the file. A BVH file's separator is
// always '.', so translate the token to whatever the process is using instead
// of assuming either.
bool
ParseDoubleToken(std::string_view token, double* out) noexcept
{
    // Long enough for any decimal or hex float a writer can emit; a longer
    // token is malformed rather than precise.
    char buffer[96];
    if (token.empty() || token.size() >= sizeof(buffer)) {
        return false;
    }
    std::memcpy(buffer, token.data(), token.size());
    buffer[token.size()] = '\0';

    const char point = *std::localeconv()->decimal_point;
    if (point != '.') {
        for (std::size_t index = 0; index < token.size(); ++index) {
            if (buffer[index] == '.') {
                buffer[index] = point;
            }
        }
    }

    char* end = nullptr;
    const double value = std::strtod(buffer, &end);
    if (end != buffer + token.size()) {
        return false;
    }
    // `errno` is deliberately not consulted. Overflow yields +/-HUGE_VAL, which
    // the caller already refuses as non-finite and reports with the offending
    // token; underflow yields a denormal or zero, which is a real value and not
    // a refusal. Reading ERANGE would add a second way to say the first thing
    // and a way to disagree with it.
    *out = value;
    return true;
}

bool
ParseSizeToken(std::string_view token, std::size_t* out) noexcept
{
    if (token.empty()) {
        return false;
    }
    unsigned long long value = 0;
    const char* first = token.data();
    const char* last = token.data() + token.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc() || result.ptr != last) {
        return false;
    }
    *out = static_cast<std::size_t>(value);
    return true;
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

struct Token
{
    std::string_view text;
    std::size_t line = 1;
};

class Scanner
{
public:
    explicit Scanner(std::string_view text) : text_(text) {}

    bool Next(Token* token)
    {
        SkipSpace();
        if (position_ >= text_.size()) {
            return false;
        }
        const std::size_t start = position_;
        const std::size_t startLine = line_;
        while (position_ < text_.size() && !IsSpace(text_[position_])) {
            ++position_;
        }
        token->text = text_.substr(start, position_ - start);
        token->line = startLine;
        return true;
    }

    bool Peek(Token* token)
    {
        const std::size_t position = position_;
        const std::size_t line = line_;
        const bool found = Next(token);
        position_ = position;
        line_ = line;
        return found;
    }

    // The offset just past the last token returned, so the motion section can
    // switch from tokens to lines without rescanning the hierarchy.
    std::size_t Position() const noexcept { return position_; }
    std::size_t Line() const noexcept { return line_; }

private:
    void SkipSpace()
    {
        while (position_ < text_.size() && IsSpace(text_[position_])) {
            if (text_[position_] == '\n') {
                ++line_;
            }
            ++position_;
        }
    }

    std::string_view text_;
    std::size_t position_ = 0;
    std::size_t line_ = 1;
};

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

class Parser
{
public:
    Parser(std::string_view text, const BvhParseOptions& options,
           Diagnostic* diagnostic)
        : scanner_(text)
        , text_(text)
        , options_(options)
        , diagnostic_(diagnostic)
    {
    }

    bool Parse(BvhDocument* document);

private:
    bool Fail(DiagnosticCode code, std::size_t line, std::string subject,
              std::string detail)
    {
        if (diagnostic_) {
            *diagnostic_ = MakeDiagnostic(code, std::move(detail));
            diagnostic_->source = options_.source;
            diagnostic_->line = line;
            diagnostic_->subject = std::move(subject);
        }
        return false;
    }

    bool FailAtEnd(std::string detail)
    {
        return Fail(DiagnosticCode::ParseFailed, scanner_.Line(), {},
                    std::move(detail));
    }

    bool ExpectKeyword(std::string_view keyword);
    bool ExpectBrace(char brace);
    bool ExpectLabel(std::string_view word);
    bool ReadOffset(BvhVec3* offset);
    bool ReadChannels(BvhJoint* joint);
    bool ReadJointBody(std::size_t jointIndex, std::size_t depth);
    bool ReadEndSite(BvhJoint* joint);
    bool ReadMotion();

    Scanner scanner_;
    std::string_view text_;
    const BvhParseOptions& options_;
    Diagnostic* diagnostic_ = nullptr;
    BvhDocument work_;
};

bool
Parser::ExpectKeyword(std::string_view keyword)
{
    Token token;
    if (!scanner_.Next(&token)) {
        return FailAtEnd("expected " + std::string(keyword)
                         + ", reached the end of the file");
    }
    if (!EqualsIgnoreCaseAscii(token.text, keyword)) {
        return Fail(DiagnosticCode::ParseFailed, token.line,
                    std::string(token.text),
                    "expected " + std::string(keyword));
    }
    return true;
}

bool
Parser::ExpectBrace(char brace)
{
    Token token;
    if (!scanner_.Next(&token)) {
        return FailAtEnd(std::string("expected '") + brace
                         + "', reached the end of the file");
    }
    if (token.text.size() != 1 || token.text[0] != brace) {
        return Fail(DiagnosticCode::ParseFailed, token.line,
                    std::string(token.text),
                    std::string("expected '") + brace + "'");
    }
    return true;
}

// `Frames:` and `Frame Time:` reach this one word at a time, because writers
// disagree about whether the colon is attached, separate, or absent -- and a
// colon is punctuation, not meaning.
bool
Parser::ExpectLabel(std::string_view word)
{
    Token token;
    if (!scanner_.Next(&token)) {
        return FailAtEnd("expected " + std::string(word)
                         + ", reached the end of the file");
    }
    std::string_view text = token.text;
    bool colonAttached = false;
    if (!text.empty() && text.back() == ':') {
        text.remove_suffix(1);
        colonAttached = true;
    }
    if (!EqualsIgnoreCaseAscii(text, word)) {
        return Fail(DiagnosticCode::ParseFailed, token.line,
                    std::string(token.text), "expected " + std::string(word));
    }
    if (!colonAttached) {
        Token next;
        if (scanner_.Peek(&next) && next.text == ":") {
            scanner_.Next(&next);
        }
    }
    return true;
}

bool
Parser::ReadOffset(BvhVec3* offset)
{
    float* components[3] = {&offset->x, &offset->y, &offset->z};
    for (std::size_t index = 0; index < 3; ++index) {
        Token token;
        if (!scanner_.Next(&token)) {
            return FailAtEnd("OFFSET needs three numbers, reached the end of "
                             "the file after " + Count(index));
        }
        double value = 0.0;
        if (!ParseDoubleToken(token.text, &value)) {
            return Fail(DiagnosticCode::ParseFailed, token.line,
                        std::string(token.text),
                        "OFFSET component " + Count(index + 1)
                            + " is not a number");
        }
        if (!std::isfinite(value)) {
            return Fail(DiagnosticCode::NonFiniteValue, token.line,
                        std::string(token.text),
                        "OFFSET component " + Count(index + 1)
                            + " is not finite");
        }
        *components[index] = static_cast<float>(value);
    }
    return true;
}

bool
Parser::ReadChannels(BvhJoint* joint)
{
    Token countToken;
    if (!scanner_.Next(&countToken)) {
        return FailAtEnd("CHANNELS needs a count, reached the end of the file");
    }
    std::size_t declared = 0;
    if (!ParseSizeToken(countToken.text, &declared)) {
        return Fail(DiagnosticCode::ParseFailed, countToken.line,
                    std::string(countToken.text),
                    "CHANNELS count is not a non-negative integer");
    }

    // `declared` is not trusted for allocation: it is a number in a file, and
    // the loop below stops at the first token that is not a channel name.
    for (std::size_t index = 0; index < declared; ++index) {
        Token token;
        if (!scanner_.Next(&token)) {
            return FailAtEnd("CHANNELS declared " + Count(declared)
                             + ", reached the end of the file after "
                             + Count(index));
        }
        // A structural keyword here means the declared count was wrong, and
        // saying *that* is worth the extra branch: reporting `JOINT` as an
        // unsupported channel name sends the reader looking for a channel.
        if (EqualsIgnoreCaseAscii(token.text, "JOINT")
            || EqualsIgnoreCaseAscii(token.text, "End")
            || EqualsIgnoreCaseAscii(token.text, "OFFSET")
            || EqualsIgnoreCaseAscii(token.text, "CHANNELS")
            || EqualsIgnoreCaseAscii(token.text, "MOTION")
            || token.text == "}" || token.text == "{") {
            return Fail(DiagnosticCode::ParseFailed, token.line,
                        std::string(token.text),
                        "CHANNELS declared " + Count(declared)
                            + " channels for joint '" + joint->name
                            + "'; channel " + Count(index + 1)
                            + " is a structural keyword");
        }
        const std::optional<BvhChannel> channel = FindBvhChannel(token.text);
        if (!channel) {
            return Fail(DiagnosticCode::UnsupportedChannel, token.line,
                        std::string(token.text),
                        "joint '" + joint->name + "' declares a channel this "
                        "format model cannot represent");
        }
        joint->channels.push_back(*channel);
    }
    return true;
}

bool
Parser::ReadEndSite(BvhJoint* joint)
{
    // "End" is already consumed.
    if (!ExpectKeyword("Site")) {
        return false;
    }
    if (joint->endSiteOffset) {
        return FailAtEnd("joint '" + joint->name
                         + "' carries more than one End Site");
    }
    if (!ExpectBrace('{')) {
        return false;
    }
    if (!ExpectKeyword("OFFSET")) {
        return false;
    }
    BvhVec3 offset;
    if (!ReadOffset(&offset)) {
        return false;
    }
    if (!ExpectBrace('}')) {
        return false;
    }
    joint->endSiteOffset = offset;
    return true;
}

bool
Parser::ReadJointBody(std::size_t jointIndex, std::size_t depth)
{
    if (depth > options_.limits.maxHierarchyDepth) {
        return FailAtEnd("hierarchy is deeper than the "
                         + Count(options_.limits.maxHierarchyDepth)
                         + "-level limit");
    }
    if (!ExpectBrace('{')) {
        return false;
    }

    bool haveOffset = false;
    bool haveChannels = false;
    for (;;) {
        Token token;
        if (!scanner_.Next(&token)) {
            return FailAtEnd("joint '" + work_.joints[jointIndex].name
                             + "' is not closed");
        }
        if (token.text == "}") {
            break;
        }
        if (EqualsIgnoreCaseAscii(token.text, "OFFSET")) {
            if (haveOffset) {
                return Fail(DiagnosticCode::ParseFailed, token.line,
                            work_.joints[jointIndex].name,
                            "joint carries more than one OFFSET");
            }
            if (!ReadOffset(&work_.joints[jointIndex].offset)) {
                return false;
            }
            haveOffset = true;
            continue;
        }
        if (EqualsIgnoreCaseAscii(token.text, "CHANNELS")) {
            if (haveChannels) {
                return Fail(DiagnosticCode::ParseFailed, token.line,
                            work_.joints[jointIndex].name,
                            "joint carries more than one CHANNELS");
            }
            if (!ReadChannels(&work_.joints[jointIndex])) {
                return false;
            }
            haveChannels = true;
            continue;
        }
        if (EqualsIgnoreCaseAscii(token.text, "End")) {
            if (!ReadEndSite(&work_.joints[jointIndex])) {
                return false;
            }
            continue;
        }
        if (EqualsIgnoreCaseAscii(token.text, "JOINT")) {
            Token name;
            if (!scanner_.Next(&name)) {
                return FailAtEnd("JOINT needs a name, reached the end of the "
                                 "file");
            }
            if (name.text == "{" || name.text == "}") {
                return Fail(DiagnosticCode::ParseFailed, name.line,
                            std::string(name.text), "JOINT needs a name");
            }
            if (work_.joints.size() >= options_.limits.maxJoints) {
                return Fail(DiagnosticCode::ParseFailed, name.line,
                            std::string(name.text),
                            "hierarchy carries more than the "
                                + Count(options_.limits.maxJoints)
                                + "-joint limit");
            }
            BvhJoint child;
            child.name = std::string(name.text);
            child.parent = static_cast<int>(jointIndex);
            const std::size_t childIndex = work_.joints.size();
            work_.joints.push_back(std::move(child));
            if (!ReadJointBody(childIndex, depth + 1)) {
                return false;
            }
            continue;
        }
        return Fail(DiagnosticCode::ParseFailed, token.line,
                    std::string(token.text),
                    "expected OFFSET, CHANNELS, JOINT, End Site or '}' in "
                    "joint '" + work_.joints[jointIndex].name + "'");
    }

    if (!haveOffset) {
        return FailAtEnd("joint '" + work_.joints[jointIndex].name
                         + "' declares no OFFSET");
    }
    // A joint with no CHANNELS is legal and means the file animates nothing
    // about it. Refusing that would refuse a static prop a producer exported
    // beside the rig -- which a profile is entitled to ignore, one layer up.
    return true;
}

bool
Parser::ReadMotion()
{
    if (!ExpectKeyword("MOTION")) {
        return false;
    }
    if (!ExpectLabel("Frames")) {
        return false;
    }

    Token framesToken;
    if (!scanner_.Next(&framesToken)) {
        return FailAtEnd("Frames: needs a count, reached the end of the file");
    }
    std::size_t declaredFrames = 0;
    if (!ParseSizeToken(framesToken.text, &declaredFrames)) {
        return Fail(DiagnosticCode::ParseFailed, framesToken.line,
                    std::string(framesToken.text),
                    "Frames: is not a non-negative integer");
    }
    if (declaredFrames > options_.limits.maxFrames) {
        return Fail(DiagnosticCode::ParseFailed, framesToken.line,
                    std::string(framesToken.text),
                    "Frames: declares more than the "
                        + Count(options_.limits.maxFrames) + "-frame limit");
    }

    if (!ExpectLabel("Frame") || !ExpectLabel("Time")) {
        return false;
    }
    Token timeToken;
    if (!scanner_.Next(&timeToken)) {
        return FailAtEnd("Frame Time: needs a number, reached the end of the "
                         "file");
    }
    double frameTime = 0.0;
    if (!ParseDoubleToken(timeToken.text, &frameTime)) {
        return Fail(DiagnosticCode::InvalidFrameTime, timeToken.line,
                    std::string(timeToken.text),
                    "Frame Time: is not a number");
    }
    if (!std::isfinite(frameTime) || frameTime < 0.0) {
        return Fail(DiagnosticCode::InvalidFrameTime, timeToken.line,
                    std::string(timeToken.text),
                    "Frame Time: must be finite and not negative");
    }
    // Zero is a statement below two frames and a contradiction above them: a
    // single pose has no interval to describe, and two frames one zero-second
    // interval apart cannot both be placed in time. Refusing the first would
    // refuse the single-pose files real exporters write.
    if (declaredFrames > 1 && frameTime <= 0.0) {
        return Fail(DiagnosticCode::InvalidFrameTime, timeToken.line,
                    std::string(timeToken.text),
                    "Frame Time: is zero across " + Count(declaredFrames)
                        + " frames");
    }

    work_.frameCount = declaredFrames;
    work_.frameTime = frameTime;

    // From here the section is read as lines, not tokens: a row is a frame, and
    // only a row boundary can tell a short frame from a missing one. Scanning
    // starts just past the frame-time token, so a writer that put the first row
    // on that same line is read correctly rather than by a rule about lines.
    std::size_t position = scanner_.Position();
    std::size_t line = scanner_.Line();
    std::size_t framesRead = 0;
    while (position < text_.size()) {
        const std::size_t start = position;
        while (position < text_.size() && text_[position] != '\n') {
            ++position;
        }
        std::string_view row = text_.substr(start, position - start);
        const std::size_t rowLine = line;
        if (position < text_.size()) {
            ++position;
            ++line;
        }

        // Writers pad the motion section; padding is not data.
        bool blank = true;
        for (const char c : row) {
            if (!IsSpace(c)) {
                blank = false;
                break;
            }
        }
        if (blank) {
            continue;
        }

        if (framesRead >= declaredFrames) {
            return Fail(DiagnosticCode::ParseFailed, rowLine, {},
                        "Frames: declared " + Count(declaredFrames)
                            + ", the file carries more");
        }

        std::size_t valuesInRow = 0;
        std::size_t offset = 0;
        while (offset < row.size()) {
            while (offset < row.size() && IsSpace(row[offset])) {
                ++offset;
            }
            if (offset >= row.size()) {
                break;
            }
            const std::size_t tokenStart = offset;
            while (offset < row.size() && !IsSpace(row[offset])) {
                ++offset;
            }
            const std::string_view token =
                row.substr(tokenStart, offset - tokenStart);

            if (valuesInRow >= work_.channelCount) {
                return Fail(DiagnosticCode::FrameWidthMismatch, rowLine,
                            "frame " + Count(framesRead),
                            "expected " + Count(work_.channelCount)
                                + " values, the row carries more");
            }
            double value = 0.0;
            if (!ParseDoubleToken(token, &value)) {
                return Fail(DiagnosticCode::ParseFailed, rowLine,
                            std::string(token),
                            "frame " + Count(framesRead) + " value "
                                + Count(valuesInRow) + " is not a number");
            }
            if (!std::isfinite(value)) {
                return Fail(DiagnosticCode::NonFiniteValue, rowLine,
                            std::string(token),
                            "frame " + Count(framesRead) + " value "
                                + Count(valuesInRow) + " is not finite");
            }
            work_.values.push_back(static_cast<float>(value));
            ++valuesInRow;
        }

        if (valuesInRow != work_.channelCount) {
            return Fail(DiagnosticCode::FrameWidthMismatch, rowLine,
                        "frame " + Count(framesRead),
                        "expected " + Count(work_.channelCount)
                            + " values, read " + Count(valuesInRow));
        }
        ++framesRead;
    }

    if (framesRead != declaredFrames) {
        return Fail(DiagnosticCode::ParseFailed, line, {},
                    "Frames: declared " + Count(declaredFrames) + ", read "
                        + Count(framesRead));
    }
    return true;
}

bool
Parser::Parse(BvhDocument* document)
{
    if (!ExpectKeyword("HIERARCHY")) {
        return false;
    }
    if (!ExpectKeyword("ROOT")) {
        return false;
    }
    Token rootName;
    if (!scanner_.Next(&rootName)) {
        return FailAtEnd("ROOT needs a name, reached the end of the file");
    }
    if (rootName.text == "{" || rootName.text == "}") {
        return Fail(DiagnosticCode::ParseFailed, rootName.line,
                    std::string(rootName.text), "ROOT needs a name");
    }

    BvhJoint root;
    root.name = std::string(rootName.text);
    root.parent = -1;
    work_.joints.push_back(std::move(root));
    if (!ReadJointBody(0, 1)) {
        return false;
    }

    // Channel columns follow declaration order across the whole hierarchy,
    // which is the only thing that maps a motion row's numbers back to joints.
    std::size_t offset = 0;
    for (BvhJoint& joint : work_.joints) {
        joint.channelOffset = offset;
        offset += joint.channels.size();
    }
    work_.channelCount = offset;

    if (!ReadMotion()) {
        return false;
    }

    // Everything above builds the document; this states what it must be, once,
    // in the place a hand-assembled document is also checked.
    Diagnostic validation;
    if (!ValidateBvhDocument(work_, &validation)) {
        if (diagnostic_) {
            validation.source = options_.source;
            *diagnostic_ = validation;
        }
        return false;
    }

    *document = std::move(work_);
    return true;
}

} // namespace

bool
ParseBvhText(std::string_view text, BvhDocument* document,
             Diagnostic* diagnostic, const BvhParseOptions& options)
{
    if (!document) {
        return false;
    }
    if (text.size() >= kUtf8Bom.size() && text.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
        text.remove_prefix(kUtf8Bom.size());
    }
    Parser parser(text, options, diagnostic);
    return parser.Parse(document);
}

bool
ParseBvhFile(const std::filesystem::path& path, BvhDocument* document,
             Diagnostic* diagnostic, const BvhParseOptions& options)
{
    BvhParseOptions resolved = options;
    if (resolved.source.empty()) {
        resolved.source = path.string();
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (diagnostic) {
            *diagnostic = MakeDiagnostic(DiagnosticCode::ParseFailed,
                                         "the file could not be opened");
            diagnostic->source = resolved.source;
        }
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    if (file.bad()) {
        if (diagnostic) {
            *diagnostic = MakeDiagnostic(DiagnosticCode::ParseFailed,
                                         "the file could not be read");
            diagnostic->source = resolved.source;
        }
        return false;
    }
    return ParseBvhText(text, document, diagnostic, resolved);
}

} // namespace motionBvh
