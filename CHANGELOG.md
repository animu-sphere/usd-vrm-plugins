# Changelog

All notable changes to `usd-vrm-plugins` are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The release version
is the single value in the repo-root [`VERSION`](VERSION) file; the git tag
(`vX.Y.Z`), this changelog, and the OpenStrata bundle manifest mirror it.

The **schema contract version** is tracked separately from the package version
(it changes only when the typed `Vrm*API` interpretation contract changes — see
[`plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](plugins/vrmSchema/docs/SCHEMA_CONTRACT.md)).
Current schema contract version: **1**.

## [Unreleased]

### Changed

- **The roadmap is rebased on what v0.6.0 actually shipped.** It still said
  "Next: v0.6.0 — the OpenExec foundation" after v0.6.0 shipped VMC input, so the
  sequence was rebuilt rather than renumbered by one: v0.7.0 is the mocopi native
  adapter and real VMC sender validation, and the OpenExec foundation is v0.8.0.
  The ordering is a claim about evidence, not preference — OpenExec parity is
  worth exactly as much as its input, so the release that records real device and
  sender sessions comes first and OpenExec then re-evaluates a pipeline that has
  already met real hardware. Two decisions land with it: the mocopi native
  adapter is a **committed deliverable** rather than something gated on measuring
  what the VMC relay drops (the native-vs-relay comparison stays, as the phase's
  distinguishing check rather than as a go/no-go), and recorded real-session
  evidence is kept separately from the generated corpora — redistributable
  captures committed, everything else as a measured manifest with no bytes.
  `docs/roadmap/openexec-v0.6.0-v0.7.0.md` is renamed
  `docs/roadmap/openexec-foundation.md`, because a filename carrying a version
  number is drift waiting to happen, and
  [`docs/roadmap/README.md`](docs/roadmap/README.md) now holds the one status
  table that decides which release a track lands in.

- **A capture product has two surfaces, and one adapter must not carry both.**
  v0.7.0 grows a second axis: reading recorded motion files. It is deliberately
  *not* a mode of the live adapter — a BVH file argues about a hierarchy, channel
  order, a frame time and a rest pose, where a socket argues about packets,
  timestamps, restarts and tracking loss — so the two are separate code meeting
  at `motionCore` and nowhere earlier
  ([motion policy §8.3](docs/design/MOTION_ARCHITECTURE_POLICY.md)).
  `vrmAdapterMocopi` is now stated as live UDP only, and the VMC sender
  interoperability matrix drops from a release condition to best-effort: the
  cross-source comparison that replaces it as the gate needs only the device,
  since one physical session can be captured live *and* exported to a file, which
  makes the two paths comparable on motion that is genuinely the same.

### Added

- **The recorded half is a generic BVH pipeline, not a capture product's
  importer** — [`docs/roadmap/recorded-motion-sources.md`](docs/roadmap/recorded-motion-sources.md),
  with the identities and edges in
  [`docs/architecture/WORKSPACE.md`](docs/architecture/WORKSPACE.md) §1, §2, §5.
  A BVH file outlives the application that wrote it, and joint names, units, axes
  and root conventions are facts about the *writer* rather than the format — so
  `motionBvh` reads syntax and decides nothing semantic, `motionSource` is the
  format-neutral model a second reader can be added behind, and what one
  producer's export *means* is a declarative profile
  (`<producer>-<format>-<preset>-v<N>`). A producer is not a profile: one
  application's export presets can disagree, and two applications can agree.
  There is **no default profile** — a caller names one or the conversion is
  refused, because guessing from joint names is right often enough to be trusted
  and wrong silently, and it produces motion that is subtly misassembled rather
  than absent. The second producer's profile lands while the first is still being
  written, since a pipeline validated against one writer cannot tell its own
  assumptions from the format's. This adds the one place a product name may
  appear outside `adapters/`, with a stated test for whether the line has been
  crossed: ship every profile file and the libraries are byte-identical.
- **`scripts/check_docs.py` checks the roadmap against the release records.**
  Nothing was wrong with any single document during the drift above; the pair had
  gone out of step, and only a reader holding both noticed. Five assertions now
  hold them together: `VERSION` has a release record and a changelog section, a
  `Next:` / `Then:` milestone is not an already-released version, a `Shipped:`
  one is, the roadmap status table names every version `current.md` plans, a
  component's status in the root README is a version that shipped, and no
  document points at a retired roadmap filename.

- **[`docs/guides/VIEWING_MOTION.md`](docs/guides/VIEWING_MOTION.md) walks a
  `.vrm` and a `.vrma` to a moving character in `usdview`.** The path existed and
  was only ever written down in pieces: which bundles a session needs, why `view`
  is the one command that does not compose them, how to compose a whole motion
  pack into one stage, and — the part that motivated it — how to tell a bound
  animation from an ignored one, since UsdSkel answers a failed binding with the
  rest pose rather than an error.

### Fixed

- **A baked clip bound nothing, and the avatar stood still.** `motion_retarget`
  authored `skel:animationSource` through the non-applied `UsdSkelBindingAPI`
  constructor, so the relationship landed with the correct target and the schema
  was never applied. UsdSkel honours that binding only on a prim carrying
  `SkelBindingAPI`, so it resolved the rest pose instead — a full-length, wholly
  valid array of joint transforms, which is why every value-level check passed
  and no diagnostic fired anywhere. The design triplet could not see it either:
  `docs/design/fixtures/motion/avatar.usda` applies the schema on its own
  skeleton, and an imported `.vrm` skeleton does not, so the fixture that proved
  the tool correct was the one shape where the bug is invisible. The tool now
  applies the schema, and the regression case bakes onto a copy of the fixture
  with the schema stripped — without the fix it resolves the rest pose
  (pelvis `(0, 1, 0)` where the clip says `(0, 1, 0.5)`) and fails.

## [0.6.0] — 2026-08-03

### Added

- **`vmc_record`, the VMC adapter's CLI and the one part of it that meets a real
  sender** —
  [`adapters/liveCapture/vmc/tools/vmcRecord/`](adapters/liveCapture/vmc/tools/vmcRecord/)
  records a live session to a `vmc-packet-capture` file and reports what it
  decoded to. Every layer beneath it is verifiable from committed bytes, which
  is the adapter's build order and also its limit: the corpus is *generated*, so
  it reproduces the protocol's shapes and not what any real application emits.
  Every item still open in Milestone B is that same shape — two sender
  applications validated, a capture device through a relay, a recorded corpus —
  and none of them closes by writing more code; they close by an operator
  pointing a sender at a port. **The datagram reaches the file before the
  decoder sees it**, which is the only rule here: a recorder whose decoder could
  refuse a datagram would record what the adapter already understands, and the
  sessions worth recording are exactly the ones it might not. The decode still
  runs in the same loop rather than afterwards, because an operator with a
  sender open needs to know *now* whether the session is worth keeping — what it
  produces is a report, and a report is not a filter. The report reads the
  layers' tallies together in the order the questions are asked when a session
  is not working (is anything arriving, does it decode, does it become motion,
  what went wrong), because `UdpReceiverStats` cannot see a bone and
  `VmcFrameStats` cannot see a datagram that never decoded. **Two of its lines
  are not statistics**: `hips offset` and `root` are the evidence the frame
  assembler's two open questions need, reported as how far each value moved and
  never as what it means — this tool is in no better position to decide what a
  sender means by a field than the layer that declined to. A third, `model`,
  reports that the sender's model title is in the recorded bytes and never
  *uses* it, since naming a capture after the avatar it happened to see would
  put someone's title in a fixture's header as well as its payload.
  `--inspect` prints the same block from a recorded capture with no socket at
  all, so the CLI is testable in CI over the committed corpus — all seven
  captures to the same 168 datagrams and 22 frames the corpus tests below it are
  written against — and so a fixture recorded months ago can be re-read.
  `vmc_record_loopback` then raises `vrmAdapterVmc_loopbackCorpus`'s claim to
  the artifact an operator keeps: the datagrams that come off a real socket are
  byte-identical to the ones that went in, and the recorded file reports the
  same motion as the file it was replayed from. Every session has a stop
  condition — `--duration`, `--idle-timeout`, Ctrl-C, and a `--max-datagrams`
  bound that is on by default because the capture is held in memory until it is
  written — and **reports which one ended it**, since a recording that stopped
  because a flag said so and one that stopped because the socket failed are
  different sessions and the file cannot tell them apart afterwards. It links
  `vrmAdapterVmc` and nothing else: WORKSPACE.md §2 permits an adapter tool to
  drive `vrmRetarget` and author a stage, this one needs neither, and a second
  path from a VMC session to an avatar would be the fork the adapter plan §2
  forbids. Both tests were picked up by the `kind: workspace` CI cells with no
  CI edit, taking the root suite from 41 names to 43.
- **The VMC adapter has a socket, and the runtime still has one thread** —
  [`UdpReceiver.h`](adapters/liveCapture/vmc/include/vrmAdapterVmc/UdpReceiver.h)
  is the last layer of the VMC path and the first one a live session touches.
  It owns a socket, a bind address, a receive clock and a size limit, and owns
  no decoding at all: `Receive` hands back the bytes exactly as they arrived,
  including the ones the layers above will refuse, because a receiver that
  filtered its own input would make the corpus a record of what the receiver let
  through rather than of what a sender sent. **The thread question the plan left
  for it is answered by moving the boundary rather than by locking anything.**
  Motion policy §11.4 put a network thread on one side of a "thread-safe
  timestamped pose buffer" that does not exist — `motionRuntime` contains no
  mutex or atomic — so the hand-off happens on **raw datagrams, before the
  decoder**: `DatagramQueue` is the only synchronised object in the whole path,
  and the five decode layers and all of `motionRuntime` stay single-threaded
  exactly as their tests are written. Locking `LiveCaptureSource` instead would
  have made one class safe against itself and left every `GetIntake()` caller
  racing on the same buffer, which is a worse fault for looking like a fixed one.
  A consumer that already has a tick needs no second thread at all — `Receive`
  with a zero timeout is a true poll — so the queue is for the narrow case of a
  consumer that cannot drain often enough, and exists mainly so that the first
  caller who meets it reaches for a queue rather than for a mutex around the
  runtime; overflow drops the **oldest** datagram, because holding a stale frame
  and refusing a fresh one adds latency a live session never gets back. Four
  smaller decisions carry the rest. **Every wait has a timeout**, for
  cancellation rather than latency: a thread parked in `recvfrom` can only be
  woken by closing the socket underneath it, which races the descriptor's reuse
  on every platform here. **Nothing arrives truncated** — the buffer is
  `MaxDatagramBytes`, so truncation is impossible rather than configurable, which
  is a decision about blame, since a truncated datagram is indistinguishable at
  the OSC layer from a malformed one and a smaller buffer would let the receiver
  manufacture `VRM_VMC_PACKET_MALFORMED` against a sender that did nothing wrong.
  **The clock is monotonic**, which the recorded capture format requires rather
  than prefers: it forbids backwards receive times because arrival order is the
  whole point of it, and a wall clock steps for reasons that have nothing to do
  with the session. And **the frozen diagnostic set needed no ninth code** —
  `VRM_VMC_SOCKET_BIND_FAILED` is the only socket failure a session cannot
  continue past, so a lost datagram, a transient error and an empty poll are
  counts in `UdpReceiverStats` rather than diagnostics that would report
  "recoverable" on every line. The kernel receive buffer is a *request* and what
  was granted is read back through `GetReceiveBufferBytes()` — every platform may
  clamp it and Linux doubles it, and asking for four megabytes, silently getting
  the default, and then losing datagrams between two slow ticks is the hardest
  failure in this class to see from the outside.
  `vrmAdapterVmc_loopbackCorpus` replays all seven
  captures **through a real socket** — 168 datagrams sent to a bound port and
  read back off it — and makes the claim the layer exists for: the 22 poses that
  come out are `operator==` identical to the ones the same bytes produce read
  from the file, with the arrival clock the only thing the wire is allowed to
  have changed. One buffer is reused for the whole replay, so the bridge's
  lifetime claim is checked by the poses matching rather than by an assertion
  about bytes. The two socket tests are their own CTest names so a runner that
  forbids one excludes two names and loses no coverage of the decode path, and
  they bind loopback on an OS-assigned port — never 39539, which would fight a
  developer's own sender for it.
- **VMC's names and VMC's axes, turned into a humanoid** —
  [`SkeletonMap.h`](adapters/liveCapture/vmc/include/vrmAdapterVmc/SkeletonMap.h)
  is the one conversion the VMC adapter exists to perform and the first layer in
  it that knows a `motion::HumanBone` exists. It converts and it does not
  decide: frame boundaries and missing bones stay with the assembler, and
  resolving a target joint stays with `vrmRetarget`. Two decisions carry the
  risk. **The vocabulary is Unity's `HumanBodyBones`, not VRM 1.0's** — a sender
  is a Unity application and writes PascalCase where VRM 1.0 writes lowerCamel —
  and for the thumb the two disagree about more than case, because VRM 1.0
  renamed the chain one joint down (`LeftThumbProximal` → `leftThumbMetacarpal`,
  `LeftThumbIntermediate` → `leftThumbProximal`). A map that lowercased the first
  letter would land every thumb rotation one joint out while every other bone in
  the hand arrived correctly, so the table is written out rather than derived.
  **The basis change is VRM 1.0's reflection through X**, not VRM 0.x's through
  Z: `(x, y, z)` → `(-x, y, z)` and `(x, y, z, w)` → `(w, (x, -y, -z))`, where
  the two sign flips on the imaginary part are one from the axis and one from
  the reversed sense of rotation — which is why a rotation about +X survives
  unchanged and one about +Z comes out about −Z. Quaternions are normalised on
  the way through, because senders emit un-normalised ones and a retarget
  composing them would skew a joint rather than rotate it; a zero-length or
  non-finite one is refused as `VRM_VMC_PACKET_MALFORMED` instead, since it names
  no orientation and the identity that would have to be invented to carry on is
  exactly what a reader could not tell from a real sample. An unrecognised name
  is `VRM_VMC_UNSUPPORTED_MESSAGE` — info, recoverable, that bone dropped and the
  frame kept — for the same reason an unimplemented address is. Two questions are
  deliberately left open rather than guessed: a VMC bone rotation is the
  *sender's local* rotation and equals a rotation away from rest only when the
  sender's rest is identity (`vrmRetarget`'s `SourceRestPose` is where that is
  answered, not here), and a bone's position has nowhere canonical to go, so it
  is converted and handed on unread until the frame assembler decides whether
  the hips offset composes with `/VMC/Ext/Root/Pos`.
  `vrmAdapterVmc_skeletonMapCorpus` maps every transform in all seven captures —
  493 bones and 24 roots, none unsupported and none refused, 232 reflected off
  the X axis — and pins the sign flip against recorded bytes rather than against
  a hand-written quaternion: the arm-raise capture's five rotations about Unity's
  −Z come out about the canonical +Z at 0°, 15°, 30°, 45° and 60°, which is the
  same left arm going up on both sides of a conversion that moved it from −X
  to +X.
- **Two comparisons for one motion value, because three callers wanted
  different answers** — [`Compare.h`](libs/motionCore/include/motionCore/Compare.h)
  gives `motionCore` exact `operator==` on `MotionSourceMetadata`, `RootMotion`,
  `ContactState`, `HumanoidPose` and `HumanoidAnimation`, plus a tolerant
  `NearlyEqual` beside it. `ExecTypeRegistry::RegisterType` requires the exact
  one before a pose can cross an OpenExec computation boundary at all; the
  offline/OpenExec parity check and the adapter corpus tests compare two float
  paths that will never agree bit for bit. So `==` answers *is this the same
  recorded value* and `NearlyEqual` answers *is this the same motion*, and they
  diverge in exactly three places, each a decision: **a quaternion and its
  negation** are the same orientation and different components, so `NearlyEqual`
  measures the angle between two orientations and `==` does not; **provenance**
  is part of the value and not of the motion, which is why a parity check needs
  no switch to turn `MotionSourceMetadata` off; and **the tolerance is stated
  once**, derived from the recorded-trace format's six decimals — a value that
  survived a round trip is already 5e-7 away from the one recorded, so a test
  picking its own epsilon would be asserting a contract nobody reviewed. Two
  rules hold for both: a field the pose does not claim is never compared (an
  absent bone's rotation slot holds whatever the producer left there, so only
  the presence bits are read), and a NaN equals nothing including itself, which
  is a property of the sample rather than something a comparison should hide.
  `NearlyEqual` also names the first field that differed and by how much, in a
  fixed order, so a failing corpus test says `leftUpperArm rotation differs by
  0.0031 rad` rather than only that two poses disagree. Landed before the VMC
  bone mapping that is its first caller, which is the order
  [docs/README.md](docs/README.md) asks for; the semantics are in
  [MOTION_CONTRACT.md](docs/design/MOTION_CONTRACT.md#comparison-semantics-v060).
- **The VMC message layer, and no humanoid in it** —
  [`VmcMessage.h`](adapters/liveCapture/vmc/include/vrmAdapterVmc/VmcMessage.h)
  turns a decoded OSC message into one of seven VMC messages: availability
  (`/VMC/Ext/OK`), the sender's clock (`/VMC/Ext/T`), the model
  (`/VMC/Ext/VRM`), the root and bone transforms, and blend-shape values with
  their apply. It stays in VMC's own terms — a bone name is plain text and a
  quaternion keeps the sender's `(x, y, z, w)` order rather than the
  `pxr::GfQuatf` one a layer down uses — because handedness, up axis, units,
  normalisation and the map to a `motion::HumanBone` belong to the skeleton map,
  and a conversion here would make the corpus agree with exactly one downstream
  reading of it. Four rules are decisions rather than details, and the first
  inverts the OSC layer's: **a message is refused, never a packet**, since the
  framing is already established and one malformed `/VMC/Ext/Bone/Pos` should
  cost that bone rather than the twenty-one that arrived with it. An address
  this adapter does not implement is **not** a defect — `VRM_VMC_UNSUPPORTED_MESSAGE`
  is info, `DecodeVmcPacket` still returns true, and every real sender emits a
  headset transform, a camera or a MIDI note. A **known** address whose arguments
  disagree with the protocol *is* malformed, and the refusal quotes both tag
  strings: OSC puts an `f` and a `d` in the same field, so a decoder reading
  values without checking tags would accept `,sddddddd` as a bone pose and pin
  nothing about the wire format. And arguments past the known form are **counted,
  never interpreted** — longer forms are in the wild (a third string on
  `/VMC/Ext/VRM`, further status integers on `/VMC/Ext/OK`, more floats after
  `/VMC/Ext/Root/Pos`'s quaternion), refusing those blames a sender for being
  newer, and decoding them would invent a meaning for bytes the corpus records
  the *shape* of and no meaning for. `vrmAdapterVmc_vmcCorpus` runs both layers
  over all seven captures to counts derived from the generator's structure (568
  decoded, twelve ignored, eight refused, nine arguments counted and not read),
  plus three claims counts cannot make: the neutral capture's rotations are all
  identity with its root at the origin and its sender clock starts at 12.5 s
  where the receive clock starts at 0; the sender-restart capture's backwards
  clock decodes without complaint — `VRM_VMC_TIMESTAMP_REGRESSION` needs the
  assembler's memory of the previous frame, and this layer has none; and the
  malformed-forms capture's bad bone costs that bone, its datagram still
  yielding the twenty-two messages that arrived with it.
- **The OSC layer, and nothing about VMC in it** —
  [`OscPacket.h`](adapters/liveCapture/vmc/include/vrmAdapterVmc/OscPacket.h)
  decodes a datagram into addresses, type tags, arguments, and bundles flattened
  into wire order. It does not know that `/VMC/Ext/Bone/Pos` means anything, and
  that separation is what makes both layers testable: OSC has its own
  malformed-input cases, and a decoder that mixed the two could only be tested
  end to end. Three rules are decisions rather than details — a datagram decodes
  entirely or not at all, so a bundle with one bad element yields no messages
  and the assembler is never handed half a frame; every OSC 1.0 and 1.1 type tag
  is sized, including the fourteen VMC never sends, because a decoder that knew
  only `i`/`f`/`s` would refuse a valid message the moment a sender attached a
  `d`; and the only code this layer raises is `VRM_VMC_PACKET_MALFORMED`, since
  it cannot tell an unimplemented address from any other one (`/foo/bar` and
  `/VMC/Ext/Midi/Note` both decode cleanly — `VRM_VMC_UNSUPPORTED_MESSAGE`
  belongs one layer up). Diagnostics carry the address as subject and a
  datagram-relative byte offset, so a refusal inside a bundle can be found in a
  committed capture rather than bisected. `vrmAdapterVmc_oscCorpus` decodes the
  whole recorded corpus to message counts derived from the generator's
  structure: 122 / 117 / 93 / 173 messages, and eight of the ten malformed
  datagrams refused with the other two — valid OSC this adapter does not
  implement — decoded.
- **A VMC session can be recorded and replayed before anything decodes one** —
  `vmc-packet-capture` v1
  ([`PacketCapture.h`](adapters/liveCapture/vmc/include/vrmAdapterVmc/PacketCapture.h)),
  the format that makes the adapter's transport-last order possible. It records
  the datagrams a session delivered, verbatim, with the instant each arrived:
  line-oriented text with hex bytes and an ASCII gutter, so a fixture diffs in a
  pull request and an OSC address pattern is legible without a decoder. It is
  deliberately *not* a `motion-capture-trace` — a trace records what an adapter
  produced, and only a capture can represent a truncated datagram, a duplicate
  delivery, or a restart mid-frame, which is to say only a capture can test a
  decoder. The reader is strict in the four ways a fixture goes wrong silently:
  a record whose hex lines under- or overrun its declared length, a gutter that
  disagrees with its bytes (a reviewer reads the gutter, not the hex), an
  unknown header key, and a length above the largest UDP payload.
- **The VMC packet corpus** — seven generated captures in
  [`adapters/liveCapture/vmc/tests/corpus/`](adapters/liveCapture/vmc/tests/corpus/),
  pinning the bundled and the unbundled sender shape, well-formed traffic the
  body path must ignore rather than refuse, ten packet-level refusals, seven
  message-level ones plus a bad bone inside an otherwise whole frame, the longer
  forms of known messages, and the arrival-order phenomena (a byte-identical
  duplicate, a backwards sender clock, a frame cut off after six bones, a
  restart). The two `malformed-*` captures are a pair rather than a duplicate:
  `-packets` dies in the OSC layer before an address means anything, and only
  `-forms` still has a frame for a bad message to be *inside* of. Generated rather than recorded
  off a commercial sender for the reason the VRM corpus is licence-gated: a
  fixture carrying someone's avatar is one CI cannot redistribute. Two tests
  hold it — `vrmAdapterVmc_corpus` re-emits every capture through the C++ writer
  and compares bytes, `vrmAdapterVmc_packetGen` re-runs the generator and
  compares against that, and a hand-edited fixture that is still canonical fails
  the second and not the first. Three properties are deliberate: the receive and
  sender clocks share no origin, receive times never go backwards while sender
  times do, and bones arrive in Unity's `HumanBodyBones` order, in which
  `UpperChest` sorts last of all.
- **The first input adapter exists as a boundary** — `adapters/liveCapture/vmc`,
  the `vrmAdapterVmc` scaffold. A plain static library declaring the only two
  edges `WORKSPACE.md` §2 permits it (`motionCore`, `motionRuntime`), built by
  the root workspace tree, with the eight `VRM_VMC_*` diagnostic codes frozen
  before any decoder exists so the set describes the protocol rather than
  whichever failure was hit first. No OSC, no VMC semantics, and no socket yet —
  transport arrives last on purpose, so every test stays fixture-driven.
  `tests/check_boundaries.py` is what makes the boundary a fact: it fails on a
  plugin manifest anywhere in the tree, a stage/registration/exec API in
  `include/` or `src/`, a mention of a sibling adapter or a plugin bundle, and —
  through an allowlist rather than a denylist — a `target_link_libraries` naming
  anything but the two permitted libraries, or a binary import outside the
  OpenUSD value-type layer. The import check reads the **test executable**: a
  static archive records no imports, so a check pointed at the library could
  never fail. Pointed at the executable it reports `usd_sdf` / `usd_usd` /
  `usd_usdSkel` when run against `motion_retarget`.
- **`ost` report 34** —
  [docs/reports/ost/34-2026-07-29-v0.21.0-adapter-library-discovery-gap.md](docs/reports/ost/34-2026-07-29-v0.21.0-adapter-library-discovery-gap.md).
  Scaffolding the adapter measured two gaps: `ost plugin test --workspace`
  discovers plain libraries only in the project root's immediate subdirectories
  and under `libs/`, so an adapter's descriptor is skipped *silently* while the
  gate still reports "valid"; and there is no per-library verb at all, so an
  adapter cannot be packaged outside the aggregate. Coverage was unaffected —
  the `kind: workspace` cells picked the adapter's tests up on all three OS with
  no CI edit.
- **The motion layer has CI** — the v0.4.0 carry-over v0.5.0 could not close.
  `ost 0.21.0` grew a `kind: workspace` cell, so `openstrata.ci.yaml` now
  declares four of them: `workspace-graph-pr` runs the WORKSPACE.md §2
  dependency-graph gate on every PR in milliseconds without building anything,
  and `workspace-pr-{windows,macos-arm64,linux}` build the root CMake tree and
  run its whole 22-test CTest suite — `motionCore`, `motionRuntime` (live
  capture and the capture corpus), `vrmRetarget`, `vrmContainer`, both CLI
  tools, and `usdvrm_baseline`, the whole-workspace behavior gate that no lane
  had ever run.
- **The 26.08 OpenExec migration audit** —
  [docs/reports/openusd/26.08-openexec-migration.md](docs/reports/openusd/26.08-openexec-migration.md),
  the remaining half of the v0.6.0 P0-1 pin work and the input `execMotion` and
  `execVrm` need before either can be designed. It reads the published runtime's
  own headers, plugInfo files and 26.08 sources across registration, callbacks,
  value types, connection dataflow, requests, cache/invalidation, `ExecIr` and
  the Hydra path, and lands five findings that change v0.6.0's scope — chiefly
  that `VtArray` is not an execution value type (so pose data crosses a
  computation boundary as a registered aggregate) and that `usdExecImaging`'s
  adapter registry is hard-coded to two prim types in 26.08, which makes the
  planned `UsdSkel` display slice unreachable without upstream work.
- **`workspace_openusd_contract`** — a CTest that drives
  `cmake/UsdVrmOpenUsd.cmake` against fixture OpenUSD installs (too old, too
  new, an exec library with no imported target, an exec component with no
  headers) and asserts both that it refuses and why. Every runtime this repo
  builds against satisfies the contract, so on a normal build the pin and the
  probe are code that never fires; this is what fires them.
- **The CLI tools ship in the release.** `tools/motionRetarget` and
  `tools/motionCapture` carry an `openstrata.tool.yaml`, so
  `ost plugin package --workspace --product` packages them as tool members: the
  aggregate product now has six members, and `ost plugin product install` places
  each under `tools/<id>/bin` with its directory joined to the activation path.
  Both tools stage into their member root's `bin/`, mirroring how each bundle
  stages its shared library into `lib/`.

### Changed

- **OpenUSD is pinned to 26.08 exactly, and a build against anything else is
  refused rather than merely unsupported.** The `>=25.05,<27.0` tolerated range
  is retired from all four bundle manifests (`openusd: "==26.08"`), and the new
  `cmake/UsdVrmOpenUsd.cmake` enforces the same pin at configure time. It is
  included by every entry point that resolves OpenUSD — the root project, each
  bundle, each library under `libs/`, each tool under `tools/` — because a
  bundle built standalone by `ost plugin build` never composes the root, and a
  plain-CMake consumer never sees `ost` at all. A range could only ever defer
  the failure to load time; OpenUSD guarantees no ABI stability across
  releases.
  - Not via `find_package(pxr 26.08 EXACT ...)`, which cannot work: OpenUSD
    installs no `pxrConfigVersion.cmake`, so a version argument makes
    `find_package` fail with "no config version file" against every OpenUSD,
    including the right one. The module tests `PXR_VERSION`, which
    `pxrConfig.cmake` does publish.
- **A 26.08 without OpenExec is refused too.** The same module probes `exec`,
  `execGeom`, `execIr`, `execUsd`, `vdf`, and `usdExecImaging` — each by both
  its imported CMake target and one header, since `ost` stages the link and
  development halves separately. `build_usd.py` exposes no flag for OpenExec, so
  a runtime built that way always carries it; the CMake build does have
  `PXR_BUILD_EXEC` (default `ON`), so the probe also catches a plain-CMake
  install built without it, as well as a slimmed or hand-stripped one. OpenExec
  becomes a first-class execution basis in v0.6.0.
- **`buildInfo.json` is at schema 2.** It gains `openexecAvailable` and
  `openexecComponents`, and `openusdVersion` is now the release name (`26.08`)
  rather than `pxrConfig.cmake`'s `PXR_MAJOR.MINOR.PATCH` — which reads
  `0.26.8`, because OpenUSD's major version is 0 and nobody calls the release
  that. `pxrVersion` keeps the packed integer for machine comparison.
- **`ost` is pinned at 0.21.0** in the CI contract and the release workflow.
- **The Linux X11 requirement is in-contract.** Every Linux cell declares
  `host_packages: {apt: [libx11-dev, libxt-dev]}`, so regenerating the
  workflows re-renders the step instead of deleting a hand-added one. The
  standing hand-edit exception in `ost-source-ci.yml` is retired; hand-authored
  `release.yml` still carries its own copy.
- **`release.yml` builds the workspace root before the bundles**, which is what
  produces the tool executables — and must come first, because the root build
  rewrites each bundle's staged library without recording bundle-managed
  provenance. It now asserts 4 bundle + 2 tool packages by member kind.

- **The input-adapter direction is in the contracts, ahead of any adapter
  code.** `WORKSPACE.md` §1 reserves `vrmAdapterMocopi`, `vrmAdapterVmc` and
  `vrmAdapterArdy`; §2 adds the adapter-library vs adapter-tool split (a
  library converts and stops; its CLI may retarget and author a stage),
  `execMotion`/`execVrm` ⇸ `adapters/*`, and the rule that adapters are
  siblings rather than a stack; §5 keeps them out of the aggregate product. The
  motion policy gains VMC as a first-class generic input — it previously
  described one direct adapter only — and the plan lives in
  [docs/roadmap/adapters-mocopi-vmc-ardy.md](docs/roadmap/adapters-mocopi-vmc-ardy.md).
  The three identities were then corrected from *bundle* to *plain library plus
  CLI tool*, still ahead of any adapter directory: a plugin manifest names an
  OpenUSD plugin kind and a `plugInfo.json`, and an adapter — barred from
  `vrmSchema`, from the file-format bundles, and from OpenExec — has nothing to
  register. The artifact name and the aggregate exclusion are unchanged, and an
  adapter's dependencies are now expressible as `requires.libraries` — the one
  form the workspace graph reads — though `ost` 0.21.0 does not yet discover a
  library descriptor nested under `adapters/<group>/<name>/`, which §2 records
  as a measured gap rather than a claim.
- **`scripts/check_docs.py` guards the OpenUSD pin.** Two independent
  mechanisms assert it — `ost` reads the manifests, a plain CMake build reads
  the contract module — and nothing made them agree, so a half-bumped pair
  would have failed only on whichever build path the next person took.

### Fixed

- **The VMC packet corpus was a mirror image of a person.**
  `tools/generate_packets.py` placed `LeftUpperLeg` at +0.09 X and
  `RightUpperLeg` at −0.09, but the table is in the *sender's* axes and Unity is
  left-handed with the character facing +Z — so a character's left side is −X,
  and every left/right pair in the rest skeleton was on the wrong side. Nothing
  was inconsistent and no decoder test could see it: the bytes are valid, the
  counts are unchanged, and a packet decoder never asks which side a bone is on.
  What it cost was the one claim the offsets exist to make, that a decoded frame
  is "recognisable as a humanoid" — it was recognisable as a mirrored one, and
  the first thing to render the corpus would have found it. The signs are
  flipped and all seven captures are regenerated. The arm-raise capture's angles
  are negated with them, because the arm is now at −X where a positive rotation
  about +Z *lowers* it: that capture is the only one whose name asserts a
  direction, so it is the only one where the sign is load-bearing. Both
  conventions — Unity's here, glTF's "−X is right" downstream of the skeleton
  map — are now written down where the numbers are, since the two differ by
  exactly the reflection the adapter applies and a corpus authored in the wrong
  one decodes perfectly.
- **The Phase 0 baseline gate no longer fails on a platform it has no baseline
  for.** `tools/baseline_freeze.py --check` now skips a `symbols/<platform>.txt`
  that is not committed — a platform with no frozen symbol list has nothing to
  regress against, which is what the tool has always documented — while any
  difference against a baseline that *does* exist still fails. Only
  `windows-x86_64.txt` is frozen; the new workspace cells were the first thing
  ever to run this gate on Linux and macOS. Freezing those two is roadmap work.

### Removed

- **`.github/workflows/motion-ci.yml`** — 210 hand-written lines, ~120 of them a
  copy of generated logic, which never worked (a bare `cmake` could not
  configure against a pulled runtime) and shipped disabled. The workspace cells
  replace it. Diagnosis and adoption:
  [ost report 33](docs/reports/ost/33-2026-07-28-v0.21.0-workspace-ci-adoption.md).

## [0.5.0] — 2026-07-26

### Added

- **Motion Phase D — the live-capture surface in `motionRuntime`.**
  `IMotionSource` with an explicit `PoseSampleStatus` (`Sampled` / `Held` /
  `Extrapolated` / `Unavailable`) and a reported lag, `ClipSource` over a
  finished `HumanoidAnimation`, and the vendor-neutral `LiveCaptureSource`:
  timestamped intake into the `PoseBuffer` Workspace Phase 6b built for it,
  confidence gating, a missing-bone policy (hold the last observation vs leave
  it unbound), root-motion intake (`Passthrough` / `Ignore` / `DeriveVelocity`),
  clock alignment, and `LiveCaptureStats` recording what was refused and why.
  A frame carrying no confidence array is never gated; a bone the rig never
  solves is reported as never observed rather than as a per-frame diff.
- **A recorded-trace format and its replay driver.** `motion-capture-trace`
  version 1 (`CaptureTrace.h`) is line-oriented text at fixed six-decimal
  precision, so a trace round-trips byte-identically and a fixture can be
  compared rather than merely parsed. The parser refuses the three ways a
  fixture goes wrong silently — an unknown bone name, text left over after a
  line's operands, and a rotation that is not unit length — because each of
  those otherwise reads as good data. `ReplaySender` pushes recorded frames as a
  caller-driven clock advances and `CaptureRecorder` accumulates the evaluated
  result back into a clip, carrying the per-tick status counts with it.
  **Nothing in this path opens a transport or reads a wall clock** — which is
  what makes a replay a faithful test rather than a mock.
- **A motion corpus**
  ([`libs/motionRuntime/tests/corpus`](libs/motionRuntime/tests/corpus/README.md)):
  six traces pinning a clean session, a limb dropout, confidence collapse at the
  extremities, irregular arrival, a rig that solves no legs, and a reported root
  velocity. They are generated by closed-form maths in `tools/generate_traces.py`
  **because** a corpus recorded from a commercial capture SDK would inherit the
  VRM corpus's redistribution gate and CI could not run it. They reproduce the
  shapes a live source produces, not any device's noise characteristics. The
  corpus `manifest.json` is derived from the traces and re-verified by
  `motionRuntime_traceGen`, so its recorded frame counts, durations, observed
  bones and digests cannot quietly stop describing the fixtures.
- **`motion_capture`** (`tools/motionCapture`) — replays a trace into an
  avatar-independent semantic humanoid clip in exactly the form
  `usdVrmaFileFormat` produces, so `motion_retarget` bakes a live session with
  no changes. `--delivery-lag` makes the consumer hold and extrapolate as it
  would when a real transport falls behind; `--report` prints the intake and
  evaluation summary, which is also written into the clip's `capture:*`
  customData. `--normalize` rewrites a hand-written trace canonically.
- **`motionCore` now owns the humanoid hierarchy** — `HumanBoneParent`,
  `NearestPresentAncestor`, and `HumanBoneJointPath`. The `.vrma` reader carried
  a private copy; two tables that can disagree would produce two semantic
  skeletons that look alike and do not compose.
- **A CI lane for the motion layer** (`.github/workflows/motion-ci.yml`) —
  written, but **disabled** (`workflow_dispatch` only), so the v0.4.0
  carry-over is *not* closed. `ost ci generate` emits one job per *bundle*
  cell, so `motionCore`, `motionRuntime`, `vrmRetarget`, `vrmContainer` and
  both CLI tools still have no lane. The bootstrap, the runtime pull and the
  WORKSPACE.md §2 dependency-graph gate work on all three OS; configuring
  against the runtime does not, because `pxrConfig.cmake` resolves Python
  development components to the paths the runtime was *built* against. The
  header records the diagnosis and the untried next step.

### Changed

- **Every lane is pinned to OpenUSD 26.08**, the exact-pin half of what was
  scoped for v0.6.0, landed early. The `motion-ci.yml` runtime digests mirror
  `openstrata.ci.yaml` and must be re-pinned with it.
- **The release lane needed the same X11 host package the PR lanes got.** 26.08's
  MaterialX 1.39.5 makes X11 dev headers a hard requirement of
  `find_package(pxr)` on Linux; the fix was added to the generated PR lane and to
  `motion-ci.yml`, but `release.yml` is hand-authored, so regenerating never
  touched it and no PR exercises it. The first `v0.5.0` tag build failed there,
  Linux-only, at configure. Every Linux job that configures against a 26.08
  runtime now carries the step, and `openstrata.ci.yaml` records that it lives in
  three workflows rather than one.
- **The Phase 0 baseline is refrozen against OpenUSD 26.08.** It is registered
  only from the plain-CMake root build, which no lane ran, so the committed
  symbol baseline was still frozen against 26.05 after the runtime bump. The
  refreeze changed all 220 exported symbols by `pxrInternal_v0_26_5` →
  `pxrInternal_v0_26_8` **and nothing else** — every other baseline artifact
  (flattened stages, structural digests, schema contract, discovery,
  diagnostics) is byte-identical across the two OpenUSD versions.
- Every workspace manifest moves to `0.5.0` in lockstep with `VERSION`, and each
  `requires` range moves from `>=0.4,<0.5` to `>=0.5,<0.6`.

### Fixed

- **Every C++ unit suite had been passing without checking anything.** All five
  (`motionCore`, `motionRuntime` ×2, `vrmContainer`, `vrmRetarget`) verify with
  `assert()`, and `NDEBUG` compiles `assert()` away — so in a Release build,
  which is the root `CMakeLists.txt` default *and* what every lane asks for
  explicitly, each binary ran to completion and returned 0 having verified
  nothing. Test targets now undefine `NDEBUG`; every assertion in the five
  suites passes with them live. Two things to be clear about: this changes what
  a green C++ suite has meant since v0.1.0, and it changed nothing about the
  Python, golden-comparison and baseline gates, which never used `assert()` and
  are what actually held the line.

### Known limitations

- **The motion layer still has no CI coverage.** The lane exists and is
  disabled rather than red; see above.
- **No product-specific capture adapter ships**, and nothing here has been
  validated against a real capture rig. Protocol decode and coordinate
  conversion belong under `adapters/`, the only place product names are
  permitted (motion policy §8.1); the corpus is synthetic by necessity.
- **A trace records capture order, not delivery order.** When a frame arrived is
  a property of the transport, so out-of-order and stale classification is
  driven from the replay schedule and unit tests rather than from a fixture.
- **The scheduled lane's `plugin_artifact` is still a 26.05 build**, pairing a
  26.05-built plugin with a 26.08 runtime. Carried from v0.4.0, untouched here.
- **The CLIs still ship in no release artifact.** `motion_retarget` and
  `motion_capture` are executables, not bundles; this needs an `ost` packaging
  answer (report 32, ask 4).

## [0.4.0] — 2026-07-26

### Added

- **Workspace Phase 6b — `motionRuntime`:** a plain static CMake library over
  `motionCore` providing `PoseBuffer` (a bounded, strictly ordered pose history
  with bracketed sampling and capped position-only extrapolation),
  `SlerpShortest` / `LerpPose` / `LerpRootMotion`, `Resample` /
  `SampleAnimation`, `PoseFilter` (frame-rate-independent exponential
  smoothing), and two- and N-pose `BlendPoses`. Every operation preserves
  `validRotations` and the `RootMotion` presence flags: a bone present in only
  one input is held, never faded toward identity.
- **Workspace Phase 6b — `vrmRetarget`:** the offline retarget core —
  `TargetSkeleton`, `HumanoidMap`, `SourceRestPose` / `RestPoseCorrection`,
  `RootMotionPolicy`, and `PoseRetargeter`. It takes plain values and never
  opens a stage, and it has no OpenExec dependency, so `execVrm`'s future
  `HumanoidRetarget` node can wrap it rather than reimplement it.
- **Motion Phase C — `motion_retarget`:** a CLI that reads a target rig and a
  semantic clip off stages, retargets, authors the resulting
  `UsdSkelAnimation`, and binds `skel:animationSource` on an override of the
  referenced skeleton. Supports `--humanoid-map`, `--root-motion
  hips|root|ignore`, `--root-joint`, `--translation-scale`,
  `--preserve-target-height`, `--resample`, `--skeleton`, and
  `--clip-skeleton`. It reads `vrm:humanBones:*` as plain attributes, so the
  motion layer needs no link against the `vrmSchema` bundle.
- **The Motion Phase A design triplet is now executable.** An end-to-end test
  bakes `canonical_walk.usda` onto `avatar.usda` and compares the result with
  `expected_retargeted.usda` at the value level through USD composition on both
  sides, rather than by byte-comparing a layer. It then resolves the baked clip
  through a `UsdSkelSkeletonQuery`, so a stage that binds correctly but animates
  nothing is a failure rather than a pass.
- **Retarget semantics are written into
  [`MOTION_CONTRACT.md`](docs/design/MOTION_CONTRACT.md):** explicit binding
  (never name heuristics), the rest-pose correction's world-delta invariant, and
  root motion as a rest-relative delta rather than an absolute height.

### Changed

- **OpenStrata CI updated to `ost 0.20.0`**, including the release lane's
  aggregate-product reproducibility gate that blocked the v0.3.0 release under
  `ost 0.19.0`.
- Every workspace manifest moves to `0.4.0` in lockstep with `VERSION`, and each
  `requires` range moves from `>=0.3,<0.4` to `>=0.4,<0.5`.
  `ost plugin test --workspace` validates plain libraries as well as bundles, so
  a half-bumped workspace now fails the graph gate outright
  (`WORKSPACE_LIBRARY_DEPENDENCY_VERSION_MISMATCH`).

### Fixed

- `tools/baseline_freeze.py` located its bundle by `kind: usd-fileformat`, which
  stopped identifying exactly one bundle when `usdVrmaFileFormat` shipped in
  v0.3.0; it now keys on `provides: usd-fileformat:vrm`.
- **`motion_retarget` authors a constant identity `scales` array.** UsdSkel
  resolves translations, rotations and scales as a unit and `scales` has no
  schema fallback, so the baked clip previously bound to the avatar and then
  resolved no joint transforms at all. Scale is still never animated.
- **`usdVrmaFileFormat` authors the same identity `scales` array**
  ([#64](https://github.com/animu-sphere/usd-vrm-plugins/issues/64)) — the other
  half of the defect above. An imported `.vrma` bound its `BodyAnimation`
  cleanly, satisfied every authored-value check, and then resolved every joint
  to the skeleton's **rest pose**: opened in usdview, the clip did not move.
  Measured on the seven VRM Animation MotionPack clips, which now resolve an
  animated skeleton at every sampled time. `test_usdvrma_plugin.py` gained a
  `UsdSkelSkeletonQuery` check that compares resolved transforms against the
  animation, since an existence check passes either way.
- `tests/baseline/discovery.json` is re-frozen to include `usdVrmaFileFormat`'s
  registration, closing the `usdvrm_baseline` failure carried since v0.3.0. The
  gate's CTest wiring now stages every workspace bundle, not just this bundle's
  dependency closure — the missing session was what made the check abort.
- **The rest-pose correction accumulates each parent chain.** It read the
  parent's own *local* rest rotation as the world-delta invariant's `Sp`/`Tp`,
  which agrees only where that parent is itself a root — every bone below the
  second level of a rig with a non-identity rest pose was mis-corrected. Clips
  from `usdVrmaFileFormat`, whose rest pose is all identity, were unaffected.
- **`motion_retarget` refuses an `--output` that names an input.** The output
  layer is cleared before it is authored, and `SdfLayer::FindOrOpen` goes
  through the layer registry, so a bake writing over its own avatar or clip
  destroyed that file.
- `motion::Resample` fell back to a single collapsed pose for a clip that
  carries samples but leaves `startTime`/`endTime` at their defaults; it now
  derives the interval from the samples.
- `HumanoidMap::SetJointIndex` returned `true` after rejecting an out-of-range
  joint index, so a refused binding was indistinguishable from a successful one.

### Known limitations

- **No CI lane compiles or tests the motion layer.** `ost ci generate` emits one
  job per *bundle* cell, so `motionRuntime`, `vrmRetarget`, and
  `motion_retarget` are covered only by the plain-CMake root build. Recorded as
  an ask in
  [report 28](docs/reports/ost/28-2026-07-26-v0.20.0-motion-layer-ci-gap.md).
- **`motion_retarget` is not in the published artifacts.** It is an executable,
  not a bundle, and the aggregate product has no member shape for one; build it
  from source. Same report, P1 ask.
- Scale is authored but never **animated**, in either the importer or the bake:
  both write a constant identity array so the clip evaluates.
- Live capture, generation, expression, look-at, OpenExec evaluation, blending
  beyond the primitive, IK, and foot locking remain Motion Phases D–H.

## [0.3.0] — 2026-07-23

### Added

- **Motion Phase A contract:** `motion::`'s vendor-neutral vocabulary, semantic
  joint paths, coordinate/time conventions, root-motion policy, provenance, and
  the hand-authored Phase C retarget hand-off triplet are frozen in
  [`docs/design/MOTION_CONTRACT.md`](docs/design/MOTION_CONTRACT.md).
- **Workspace Phase 6a — `motionCore`:** a plain static CMake package exporting
  `HumanBone`, `HumanoidPose`, `HumanoidAnimation`, `RootMotion`, source
  metadata, contact samples, and declarative motion constraints. Its public
  contract permits only OpenUSD `Gf` values — never a stage, plugin, network,
  SDK, or product name.
- **Workspace Phase 7 / Motion Phase B — `usdVrmaFileFormat`:** a `.vrma`
  `SdfFileFormat` for VRMC VRM Animation 1.0 GLB clips. It imports the first
  animation's humanoid rotations and hips translation into an
  avatar-independent semantic `UsdSkelSkeleton` / `UsdSkelAnimation`, with
  30-fps time mapping and `vrma` provenance metadata.
- **VRMA verification:** a deterministic, license-free GLB fixture, L0–L5
  plugin pyramid and flattened golden, plus Python checks for semantic joints,
  rest transforms, rotations, root motion, time range, and animation binding.
- **Hermetic glTF parser source:** cgltf v1.15 is vendored under
  [`third_party/cgltf`](third_party/cgltf) with its MIT license; CMake no longer
  downloads a dependency while configuring a bundle.

### Changed

- The release workspace now contains four plugin bundles. Every target publishes
  `usdVrmaFileFormat-0.3.0-<target>.tar.zst` alongside the three VRM bundles;
  its package is independently validated through L5 before staging a release.

### Known limitations

- VRMA expressions, look-at, multiple clips, retargeting, live capture,
  generation, constraint solving, and OpenExec evaluation are deferred to
  Motion Phases C–H.
- `CUBICSPLINE` input currently uses value vertices as a linear approximation
  and reports a warning; scale channels and non-hips translations are ignored.

## [0.2.0] — 2026-07-18

First release of the multi-bundle workspace. **Artifact-breaking:** consumers of
the v0.1.0 `usdVrm-0.1.0-*` asset names must move to
`usdVrmFileFormat-0.2.0-<target>`, and the typed schemas now ship as their own
`vrmSchema-0.2.0-<target>` asset that must be installed alongside it — see
[the release notes](https://github.com/animu-sphere/usd-vrm-plugins/blob/v0.2.0/docs/releases/v0.2.0.md)
for the install order.

### Added
- **Workspace Phase 2 — `vrmContainer` extraction**: a standalone plain shared
  CMake library (`libs/vrmContainer`) now validates GLB 2 headers/chunks and
  byte ranges, exposes immutable non-owning views, and owns stable embedded
  resource hashing. It exports an installed `find_package(vrmContainer)`
  package, carries OpenUSD/plugin boundary checks, and is consumed by both the
  importer reader and the co-located resolver without changing Phase 0 output.
- **Negative test corpus** (`plugins/usdVrmFileFormat/tests/corpus/generated/malformed/`):
  nine deliberately-broken, license-clean `.vrm` fixtures (authored by
  `tools/generate_negative.py`) that pin the importer's diagnostic contract —
  each provokes exactly one code, driven by `negative-manifest.json` +
  `tests/test_usdvrm_negative.py` (registered as the `usdvrm_negative` CTest and
  in the bundle's `tests.smoke` / `tests.negative` lists).

### Changed
- **Release artifacts now ship the whole workspace.** The release lane packages
  and publishes all three bundles — `vrmSchema`, `usdVrmFileFormat`, and
  `usdVrmPackageResolver` — via `ost plugin package --workspace`, instead of the
  importer bundle alone. This is required, not cosmetic: since the Workspace
  Phase 1 schema split, an `usdVrmFileFormat` package on its own registers the
  `.vrm` file format but **cannot open a stage**, because OpenStrata stages a
  dependency bundle's link-time half (`libvrmSchema` + its CMake package) without
  its USD registration half (`plugInfo.json`, `generatedSchema.usda`), so the
  typed `Vrm*API` schemas are never discovered. Installing all three bundles on
  the plugin path is therefore the supported configuration. Upstream asks are
  filed in [ost report 23](https://github.com/animu-sphere/usd-vrm-plugins/blob/v0.2.0/docs/reports/ost/23-2026-07-18-v0.18.0-workspace-packaging-v0.19.0-asks.md).
- **Workspace Phase 4 — `usdVrm` → `usdVrmFileFormat` rename**: the file-format
  bundle now carries the identity the workspace contract assigns it
  (`docs/architecture/WORKSPACE.md` §1). The bundle directory
  (`plugins/usdVrm` → `plugins/usdVrmFileFormat`), the manifest `plugin.name`,
  and the USD resource directory (`plugin/resources/usdVrm` →
  `plugin/resources/usdVrmFileFormat`) all move together. `usdVrm` is retired as
  a bundle id and now names only the aggregate product.
  **Release-artifact rename:** the per-bundle artifacts and manifest sidecar
  ship as `usdVrmFileFormat-<version>-<target>.tar.zst` (§5) — anyone consuming
  the v0.1.0 `usdVrm-*` asset names must update. No functional change: the USD
  registry name (`UsdVrmFileFormat`), the registered `.vrm` file format, the
  library name (`libUsdVrmFileFormat`), authored stage output, and the
  diagnostic codes are all untouched, and the Phase 0 baseline verifies
  byte-identical (53/53).
- **OpenStrata 0.15 workspace composition**: CI and source builds now consume
  the `requires.bundles` closure directly, removing the repo-owned vrmSchema
  bootstrap and restoring the importer PR lanes from L1 to the full L5 gate.
  `vrmContainer` is staged through `requires.runtime_libs`; portable
  `requires.libraries` remains reserved/fail-closed in ost 0.15.
- **Workspace Phase 1 — `vrmSchema` bundle split**: the six typed `Vrm*API`
  schemas (sources, generated C++, `generatedSchema.usda`, USD registration,
  `tools/generate_schema.py`, `docs/SCHEMA_CONTRACT.md`) moved from the
  `usdVrm` bundle into the new standalone `plugins/vrmSchema` bundle
  (`kind: usd-schema`, `schema.contract: 1`, CMake package export +
  installed-package consumer smoke). `usdVrm` now consumes it via
  `find_package(vrmSchema)` / `requires.bundles` per
  `docs/architecture/WORKSPACE.md` §2. Authored stages, the schema contract,
  the registered-type set, and the exported C++ surface are unchanged
  (Phase 0 baseline verified byte-identical).
- **Importer diagnostics**: malformed inputs the importer previously sanitized or
  rejected silently now emit stable codes. New codes: `VRM003` (container
  unreadable — FATAL, stage fails to open), `VRM111` (skin `JOINTS_0` index out of
  range; clamped to root), `VRM141` (duplicate humanoid bone; first mapping kept),
  `VRM151` (expression morph-target index out of range; bind skipped), `VRM190`
  (spring collider-group index out of range; dropped). See
  `plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md`.

## [0.1.0] — 2026-07-12

First tagged release of the `usdVrm` OpenUSD file-format plugin: a `.vrm`
(VRM 0.x / 1.0) importer that normalizes into a canonical model and authors a
typed USD stage, plus the reliability tooling around it.

### Added
- **Importer** (`SdfFileFormat` for `vrm`): GLB read via cgltf, VRM 0.x/1.0
  detection, all version differences absorbed into `VrmCanonicalDocument` before
  any USD is authored.
- **Geometry & materials**: `UsdGeomMesh` (points/normals/UV/indices), non-skinned
  node transforms preserved; `UsdPreviewSurface` materials with the full texture
  set (base color, metallic-roughness, normal, emissive, occlusion; wrap modes,
  `KHR_texture_transform`).
- **Skeleton & skinning**: one `UsdSkelSkeleton` unified across all glTF skins,
  topologically ordered, bind from inverse bind matrices; `UsdSkelBindingAPI`.
- **Skeletal animation**: glTF joint TRS clips → `UsdSkelAnimation`.
- **Front-direction bake**: VRM 0.x −Z front normalized to a canonical +Z rest
  pose; provenance in `customData` (`vrm:sourceFrontAxis`, `vrm:frontAxisNormalized`).
- **Typed schemas** (compiled, co-located in `usdVrm`): `VrmHumanoidAPI`,
  `VrmExpressionAPI`, `VrmLookAtAPI`, `VrmSpringBoneAPI`, `VrmColliderAPI`,
  `VrmConstraintAPI`. VRM constraints / LookAt / SpringBone are authored as
  **data only** — evaluation and simulation are the future `execVrm` runtime layer.
- **MToon**: preserved as `vrm:mtoon:raw` + `vrm:shaderModel` tag, with a
  `UsdPreviewSurface` fallback (approximation, not a shading reproduction).
- **Lossless preservation**: `vrm:meta` / `specVersion` / `vrm:rawExtension` in
  `customData`.
- **Reliability tooling**: standalone stage validator (`tools/validate_vrm.py`,
  non-zero exit on ERROR/FATAL), coded diagnostic taxonomy (`VRMxxx`), compatibility
  report (`tools/vrm_report.py`), portable texture packaging (`ArPackageResolver`
  + `tools/package_vrm.py`).
- **Schema contract v1** documented and versioned; stages/report carry
  `schemaContractVersion = 1`.
- **Redistributable corpus** seed: Seed-san (VirtualCast) and VRM1 Constraint
  Twist (pixiv), both VRM 1.0 with `allowRedistribution: true`.
- **Corpus foundation** — `tests/corpus/` organized into `spec-samples/`
  (vendored, license-clear), `vroid/` (fetched, git-ignored), `conformance/`, and
  `generated/`; a machine-readable `tests/corpus/manifest.json` (provenance,
  SHA-256, roles, feature tags, expected diagnostics + max severity) drives
  `test_usdvrm_corpus.py` and asserts the diagnostic-code contract.
  `scripts/verify_corpus.py` (SHA-256) and `scripts/fetch_corpus.py` (pinned,
  license-gated fetch for the VRoid + Alicia candidates). No third-party binaries
  are committed.
- **Release contract**: repo-root `VERSION` single source of truth (consumed by
  CMake), this `CHANGELOG.md`, [`docs/reference/CAPABILITY_MATRIX.md`](docs/reference/CAPABILITY_MATRIX.md),
  and [`docs/reference/SUPPORTED_CONFIGURATIONS.md`](docs/reference/SUPPORTED_CONFIGURATIONS.md).
- **Clean-install / plugin-discovery smoke** — `scripts/clean_install_smoke.py`
  packages the bundle, extracts the artifact into a fresh directory outside the
  repo, and runs `plugins/usdVrm/tests/clean_install_smoke.py` inside that
  extracted bundle's session: `.vrm` discovery served from the package, a
  textured fixture + a corpus avatar open and validate, and an embedded texture
  resolves straight from the `.vrm` container — proving no build-tree dependency.
- **Release workflow** ([`.github/workflows/release.yml`](.github/workflows/release.yml)) —
  a tag `vX.Y.Z` (must match `VERSION`, with this changelog's section finalized)
  builds Windows / macOS arm64 / Linux bundles against digest-pinned cy2026
  runtimes, gates on **digest-reproducible packaging**, packaged-artifact
  verification (`ost plugin test --from-package`) and the clean-install smoke on
  all three OS, then assembles a **draft** GitHub release: per-target lean +
  debug-symbol bundles, a source archive, `SHA256SUMS`, and notes rendered from
  this changelog via [`docs/contributing/RELEASE_NOTES_TEMPLATE.md`](docs/contributing/RELEASE_NOTES_TEMPLATE.md)
  (`scripts/make_release_notes.py`). `workflow_dispatch` = dry run.
- **Build-metadata stamp** — CMake configures `buildInfo.json` next to
  `plugInfo.json` (git commit, build OS, compiler, build type, OpenUSD version,
  plugin version, schema contract version), shipped inside every packaged bundle;
  `tools/vrm_report.py` surfaces it as the report's `build` section. The stamp
  carries no timestamp so packaging stays digest-reproducible.
- **Install guide** ([`docs/guides/INSTALL.md`](docs/guides/INSTALL.md)) — release-artifact,
  OpenStrata, and from-source installation with verification and troubleshooting.
- **CI & packaging**: generated 3-OS source lanes (`ost ci generate github`)
  bootstrap-pinned to **ost 0.13.0**; packages are **lean by default** (debug
  symbols split into a sibling `*-debug.tar.zst`) and digest-reproducible for an
  unchanged build.

### Known limitations
- MToon is a `UsdPreviewSurface` fallback plus preserved raw data — **not** a
  shading reproduction (full realization is roadmap P5).
- **Morph-weight (blend-shape) animation is not authored** — only joint TRS
  animation clips are.
- No runtime **evaluation/simulation** (LookAt, constraints, spring bones are
  data only; the `execVrm` runtime layer is roadmap P4).
- Compressed / unsupported embedded texture formats (e.g. KTX2) are skipped with
  a coded warning (`VRM101`/`VRM102`).
- No VRM **exporter** (round-trip is research only, roadmap P6).

### Non-goals for v0.1.0
Explicitly out of scope for this release (tracked in the
[roadmap](docs/roadmap/) non-goals and design policy §15/§19):
- Full/pixel-perfect MToon shading reproduction across renderers.
- A VRM exporter.
- SpringBone / physics runtime simulation.
- Mocopi or other live input streaming.
- Coverage of every glTF extension.
- ABI stability guarantees across all OpenUSD versions (see
  [`docs/reference/SUPPORTED_CONFIGURATIONS.md`](docs/reference/SUPPORTED_CONFIGURATIONS.md)).

[Unreleased]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/animu-sphere/usd-vrm-plugins/releases/tag/v0.1.0
