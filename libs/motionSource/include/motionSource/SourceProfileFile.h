// SPDX-License-Identifier: Apache-2.0
//
// A profile as a file: the keys, and the small language they are written in.
//
// `SourceProfile.h` froze the **values** a profile states — every convention by
// name, with its spellings in the library so a reader and a report print the
// same word. What was left open was the other half: which keys carry them, in
// what shape, and what happens to a file that gets one wrong
// (roadmap/recorded-motion-sources.md §3). That is this header, and it is the
// reason a profile can be data at all: package data nothing can read is not a
// profile, it is a document about one.
//
// Five decisions, each of which a more permissive reader would have cost.
//
// **A stated subset, not a language.** The grammar below is written out and
// everything outside it is refused with a line. The alternative — a general
// document parser, vendored or written — buys nothing this data needs and costs
// the thing it most needs: an unknown key has to be an error. A profile is the
// one place a near-miss is invisible, because a file that parses and quietly
// drops the line that would have bound an arm produces motion that is
// *misassembled* rather than absent (roadmap §3.1). So an unknown key, a
// duplicate key, an unknown vocabulary word, a tab in the indentation, and a
// value of the wrong shape are all refusals, and none of them is a warning.
//
// **A reader hands back a profile that is already valid.** Parsing ends with
// `ValidateSourceProfile`, so no half-built profile leaves this layer. A loader
// that returned an unvalidated value would put the same check in every caller
// and make "there is no default profile" a claim each of them re-proved.
//
// **The joint map's order is the file's.** The map is read in the order the
// lines appear and kept that way, because a match reports in the profile's
// declaration order — a report whose row order depended on the rig would be a
// worse golden test than one whose row order is the file's.
//
// **A refusal names a line, and line 0 means the document.** A key's spelling,
// a word no vocabulary has, a value that is not a map — each is about a line and
// says which. Whether a profile maps the canonical root is about no line at all,
// and reporting one for it would send a reader to a line with nothing wrong on
// it.
//
// **The file's `schemaVersion` is not a profile's `v<N>`.** This one moves when
// the *keys* below change; a profile's own id moves when a *producer's* output
// contract breaks. Neither implies the other, and one number doing both jobs
// would make every producer's break look like a tooling upgrade.
//
// The grammar, in full:
//
//   * UTF-8 text, LF or CRLF, a leading byte-order mark skipped. Blank lines are
//     ignored, and `#` begins a comment where it starts a line or follows a
//     space.
//   * Indentation is spaces. A tab in it is refused rather than guessed at,
//     which is the one place this reader is stricter than it looks: a file
//     indented with tabs is a file whose structure nobody can see.
//   * A block mapping is `key: value` lines at one indentation. A key with no
//     value on its line takes the block below it: a mapping indented under it,
//     or a sequence of `- item` lines either indented under it **or at its own
//     indentation**, which is how the format is ordinarily written and so is
//     accepted rather than refused for looking unusual.
//   * A value on the line is a scalar, a flow mapping `{ a: 1, b: 2 }`, or a
//     flow sequence `[a, b]`. Neither flow form nests, and a sequence holds
//     scalars.
//   * A scalar is the rest of the line, trimmed, or a `"…"` string with `\"` and
//     `\\` escapes. Quoting is what a value needs when it carries a `#` that
//     opens a word, or any of `{ } [ ]`, or — inside a flow form — a comma.
//   * Not supported, and refused rather than half-read: anchors and aliases,
//     tags, multiple documents, block scalars (`|`, `>`), and a flow form
//     inside another. A profile is a table of names, and every one of those is
//     an indirection a reader of the file would have to resolve in their head
//     to know which joint is mapped to what.
//
// What is *not* claimed is that this reads every file YAML would: the list above
// is deliberately absent. What is claimed, and checked, is that it never
// disagrees with YAML about a file it does accept —
// `scripts/check_motion_profiles.py` reads each shipped profile a second time
// with a real YAML implementation where one is available and compares the two
// readings, because a silent difference of interpretation is the one failure a
// hand-written reader can produce that a refusal cannot.
//
// and the keys, in the shape a profile file writes them:
//
//     schemaVersion: 1
//     id: <profile id>
//     producer: <provenance label>
//     coordinates:
//       handedness: right
//       upAxis: +Y
//       forwardAxis: +Z
//       translationUnit: centimeters
//     root:
//       joint: <source name>
//       translation: absolute-position
//       rotation: body-orientation
//     restPose: rest-offsets
//     unmappedJoints: report
//     joints:
//       <source name>: { bone: hips, required: true }
//     ignoredJoints: [<source name>]
//
// `ignoredJoints` is the one optional key; every other one above is required,
// and `required:` inside a joint entry defaults to false. Each word on a
// right-hand side is a vocabulary term `SourceProfile.h` defines and this reader
// only looks up — a spelling accepted here that no report can print would be the
// second vocabulary that header exists to prevent.
#pragma once

#include "motionSource/SourceProfile.h"
#include "motionSource/api.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace motionSource
{

// The `schemaVersion` a file must state. See the header note on why this number
// and a profile id's `v<N>` are different numbers.
inline constexpr int SourceProfileSchemaVersion = 1;

struct SourceProfileParseError
{
    // Plain text, for the reason the refusals in `SourceProfile.h` are typed and
    // this one is not: a malformed file is not an event in any reader's
    // diagnostic set. A caller mapping this onto one has exactly one candidate
    // and does not need help choosing it.
    std::string reason;
    // 1-based, and 0 when the refusal is about the document rather than a line.
    std::size_t line = 0;
};

// Parses `text`. On failure `profile` is untouched and `error`, when given,
// carries the reason and the line.
//
// On success `profile` satisfies `ValidateSourceProfile` — see the header note.
MOTIONSOURCE_API bool ParseSourceProfileText(std::string_view text,
                                             SourceProfile* profile,
                                             SourceProfileParseError* error
                                             = nullptr);

// Reads the file and parses it. A file that cannot be opened or read is a
// refusal at line 0 naming the path — there is no line, and the document this
// error is about is one nobody managed to read. The path is in the text because
// this error carries no field for it, unlike the reader below this layer whose
// diagnostic does: a caller loading a directory of profiles otherwise gets a
// refusal that does not say which file.
MOTIONSOURCE_API bool ParseSourceProfileFile(const std::filesystem::path& path,
                                             SourceProfile* profile,
                                             SourceProfileParseError* error
                                             = nullptr);

} // namespace motionSource
