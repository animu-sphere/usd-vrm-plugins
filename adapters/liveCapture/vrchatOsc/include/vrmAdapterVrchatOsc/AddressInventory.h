// SPDX-License-Identifier: Apache-2.0
//
// What a recorded session actually contains, counted from bytes.
//
// This is VRC-1's measurement and it is deliberately not VRC-2's decoder. It
// answers *which addresses did this sender send, with which type tags, how many
// times, and how fast* — and it stops there. It has no list of addresses it
// expects, no notion that `/tracking/trackers/1/position` is a tracker or that
// `1` is an index, and no opinion about which of them a later decoder will
// implement. A row here is a fact about a capture; giving one a meaning is a
// separate layer and a separate milestone
// (roadmap/osc-and-vrchat-trackers.md §5, §9).
//
// ## Why an inventory could not be written until now
//
// VRC-0 shipped a recorder whose report refuses to group by address, and said
// why: this protocol's *receiving* end is published, so a report grouped from
// the documentation could have been written on day one, and every number in it
// would have been conditional on an assumption nobody had tested. The
// difference now is not that the assumption was confirmed. It is that nothing
// below is assumed at all: OSC's grammar says where an address ends, so a
// decoder reads the addresses a sender sent rather than looks for the ones a
// document predicted. An address nobody expected appears here as a row.
//
// ## The other half of what this file is for
//
// It is the **second consumer** of `libs/osc`, and that is not a side effect —
// it is the evidence the extraction waited for. A decoder with one caller is a
// decoder shaped like that caller, and the only proof that a surface is neutral
// is a caller that never says `VMC` (osc-and-vrchat-trackers.md §3.1). This
// file, and the map from an OSC refusal onto this adapter's own
// `VRM_VRCHAT_OSC_PACKET_MALFORMED`, are what that came to.
//
// It costs this adapter one edge and no more: `osc` links nothing at all — not
// even a socket — so the property VRC-0 measured survives it. This adapter's
// test binaries still import no OpenUSD on any platform, and the boundary check
// still asserts it against the built binary rather than against a comment.
#pragma once

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/PacketCapture.h"
#include "vrmAdapterVrchatOsc/api.h"

#include <cstddef>
#include <string>
#include <vector>

namespace vrmAdapterVrchatOsc
{

// One address, with one type tag string.
//
// The pair is the key rather than the address alone, and that is the row this
// milestone exists to produce. A sender that emits `/a/b` as `,fff` in most
// frames and as `,f` in some is a sender whose shape a decoder has to handle,
// and a table keyed on the address would average the two into one row and hide
// exactly that. Two rows with the same address and different tags is a finding.
struct AddressRow
{
    std::string address;
    // Without the leading comma, so `typeTags.size()` is the argument count.
    // Empty for a message that carries no arguments, which is well-formed.
    std::string typeTags;

    std::size_t messages = 0;
    // Datagrams carrying at least one such message. Lower than `messages` when
    // a sender repeats an address within one bundle, which is itself worth
    // seeing.
    std::size_t datagrams = 0;

    // Receiver-clock seconds of the first and last datagram carrying it, so a
    // row that stopped part-way through a session is visible as one.
    double firstTime = 0.0;
    double lastTime = 0.0;
};

// One capture, inventoried.
//
// The counts are of *datagrams* and are not derived from the rows: a capture
// whose rows sum to fewer messages than `messages` would be an arithmetic bug
// in this file, and a reader can see it rather than take it on trust.
struct AddressInventory
{
    // Sorted by address, then by type tags. Deterministic, because this is a
    // report an operator pastes into a milestone record and diffs against the
    // next session's.
    std::vector<AddressRow> rows;

    std::size_t datagrams = 0;
    std::size_t decoded = 0;
    // Datagrams that are not decodable OSC. On this wire that is a real
    // possibility rather than a corrupt-file case: the port is a well-known one
    // and anything on the network may send to it.
    std::size_t refused = 0;
    std::size_t bundled = 0;
    std::size_t messages = 0;

    // One per refused datagram, in capture order, each carrying
    // `VRM_VRCHAT_OSC_PACKET_MALFORMED` and the shared decoder's own subject
    // and detail. Not capped: a capture is a bounded file, and an operator
    // reading why a session was half-refused needs all of them.
    std::vector<Diagnostic> diagnostics;
};

// Inventories a capture. Never fails: an undecodable datagram is a row in
// `diagnostics` and a session that carried nothing decodable is an inventory
// with no rows, which is a finding rather than an error.
VRMADAPTERVRCHATOSC_API AddressInventory InventoryAddresses(
    const PacketCapture& capture);

} // namespace vrmAdapterVrchatOsc
