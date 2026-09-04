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

### Added

- **Two expressions can no longer both own the eyelid: VRM 1.0's expression
  overrides, read and obeyed** (closes #170). Expressions accumulate on the
  targets they bind, and two that bind *different* targets still fight when
  those targets displace the same vertices: measured on `AliciaSolid.vrm`, a
  `blink` held inside a full-weight `happy` displaces 154 shared points, 146 of
  them in the same direction, and drives the lid **1.96x** past where either
  expression alone would — the lash passes through the eye and lands on the
  cheek. Nothing in the weight arrays is out of range, because the collision is
  geometric. VRM 1.0 defines exactly one mechanism for it — the per-expression
  `overrideBlink`, `overrideLookAt` and `overrideMouth` — and none of the three
  tokens appeared anywhere in this repository; they survived as unread text
  inside `vrm:rawExtension`.

  All three layers landed together. `VrmExpressionAPI` gains `vrm:overrideBlink`,
  `vrm:overrideLookAt` and `vrm:overrideMouth` (additive within schema contract
  v1: an old reader ignores them and behaves exactly as before), the importer
  carries the tokens onto them, and `vrmRetarget`'s `ExpressionResolver`
  performs the arbitration — which is where it belongs, because an override is
  a statement one expression makes about *others* and therefore exists only for
  a whole sample, while the importer has a file.

  **A category is a set of preset names, not an expression.** The specification
  overrides "the mouth", never `aa`: `blink`/`blinkLeft`/`blinkRight`, the four
  `look*`, the five vowels. A custom expression is in none of them, since VRM
  reserves the preset names — and a VRM 0.x rig lands in the same sets anyway,
  because the importer already migrates `presetName` to the 1.0 spelling.

  **An unauthored attribute is not `none`.** The importer authors an override
  only where the file stated one, so a VRM 0.x expression — 0.x has no such
  field — carries none of the three rather than three tokens it never said. A
  token outside the vocabulary is kept verbatim and reported as the new
  **`VRM153`**, rather than dropped or read as "no arbitration": an override
  silently downgraded is a face that renders wrong with nothing in the log. A
  value that is not a token at all — a number, `null`, an empty string — has
  nowhere to go on a token attribute, so it survives in `vrm:rawExtension`
  alone and is reported under the same code, which is the case where the file
  states an arbitration the stage cannot. The bake refuses a token it cannot
  read out loud for the same reason.

  Four resolve rules are measured rather than asserted. `block` is a switch and
  not a steep blend — any weight above zero takes the whole category — while
  `blend` hands over its own weight. **The strongest override wins and they do
  not stack**: two expressions blending at 0.5 and 0.8 leave 0.2 of the blink,
  not 0.1, which is a face neither of them asked for. **An expression the sample
  resolves to zero overrides nothing**, so the rate is read off the *resolved*
  weight rather than the reported one — a binary expression reported at 0.4 is
  off, and reading the raw report would let it block a blink while contributing
  nothing to the face itself; a rate is also bounded to `[0, 1]` even where
  clamping is off, because it multiplies *another* expression's weight and a
  rate past 1 would invert that expression rather than suppress it. The
  arbitration is **one pass**, so an expression another override suppressed
  still overrides its own category — cascading would make the answer depend on
  the order the categories are settled in, and a pair that override each other's
  categories would have none at all. And **a binary expression is rounded again
  after a partial suppression**, because `isBinary` says the rig has no
  half-shut eyelid to land on. An expression that overrides the category it is itself in suppresses
  itself: the rig said so, and exempting it would make an override mean one
  thing for `happy` and another for `blink`; it is reported, because it is far
  likelier to be a slip than an intent.

  A suppression is **named and is not a defect** — it carries the expression
  that caused it ("blink (by happy)") and stays out of `IsClean()`, because the
  avatar's own rule being obeyed is not an error, while a producer whose blink
  track went flat has nothing to find in the weights. `motion_retarget` reports
  it as a note.

  The two VRM 1.0 assets already in the corpus state the rule and were read only
  as far as `vrm:rawExtension` until now: Seed-san's `happy` carries
  `overrideBlink: blend` — precisely what would have prevented the artifact
  above — and its `relaxed` carries `block` on the blink and the gaze. The
  corpus file that produced the measurement is VRM 0.x, which has no override
  fields at all, so its own fix stays clip-side; what this closes is the case
  where the avatar *does* state the rule and it was dropped on the floor.

- **A rig looks where the clip is looking: `LookAtEvaluate` and the gaze bake.**
  A clip named a place to look and no rig looked there. `vrmRetarget`'s new
  `LookAtEvaluator` turns that target point into one particular avatar's answer,
  and `motion_retarget` authors it — the consumer half of the reading half
  below, and the last of Motion Phase G's two resolve steps.

  **The two VRM rig types are not a spelling difference**, and that is the shape
  of the step. A `bone` rig answers with eye-joint rotations, and the eye on the
  side the gaze goes to takes the *outer* range map while the other takes the
  *inner* one — the split exists because two eyes converge, so a resolve that
  read one map for both eyes would look plausible on every symmetric rig. An
  `expression` rig answers with `motion::ExpressionWeights` for `lookLeft`,
  `lookRight`, `lookUp` and `lookDown`, and that is deliberately the value
  `ExpressionResolver` already consumes: the gaze is folded into the sample's own
  weights *before* the expression resolve, so it reaches the avatar's binds
  through the one accumulator that already sums expressions rather than through
  a second path into the same blend shapes. One weight drives both eyes there,
  so the inner map is unreachable for that type, and a rig that states a
  different one is told rather than quietly half-read.

  Four further decisions are measured by fixtures rather than asserted. **A gaze
  starts at the eyes**: the origin is the head joint plus the rig's
  `offsetFromHeadBone`, rotated *by the head*, because the offset is stated in
  the head's own space — an implementation that added it in world space agrees
  with every level-headed test and diverges the moment the character turns,
  which is why a turned head is one of them. **The two VRM spellings are one
  value**: VRM 1.0's `inputMaxValue`/`outputScale` and VRM 0.x's
  `xRange`/`yRange` plus an editable Hermite curve parse into the same
  `LookAtRangeMap`, and the 0.x linear default is that basis reduced
  algebraically to `t`, so a 0.x rig that never touched its curves and a 1.0 rig
  are the same map rather than two that happen to agree. **A gaze nobody named
  is not a gaze forward, and one the clip stops naming holds** — the eyes stay
  where the retarget put them until a first target arrives, and a sample that
  says nothing afterwards leaves the last gaze standing, which is the rule a
  blocked expression weight is already under and the one thing that keeps a
  fixed-width blend-shape array and a per-sample joint array agreeing. A target
  sitting *on* the eye origin names no direction at all and is reported, because
  that one is a defect rather than a silence. And **the
  clip's own `offsetFromHeadBone` is a fallback, not the measurement**: it
  describes the rig the clip was authored on, so it is consulted only when the
  avatar states none, and the substitution is warned about rather than defaulted.

  Two claims the tests had to earn. The eye rotation's composition — yaw about
  +Y, then pitch about +X *negated*, since a positive right-handed rotation
  about +X takes the forward axis down — is checked by aiming an identity range
  map at a target and requiring the resulting rotation to point back at it; it
  fails on either half being wrong, where asserting the two angles would have
  agreed with an implementation that had both conventions inverted. The
  end-to-end fixtures then give the four maps four *different* output scales
  (10 outer, 5 inner, 12 up, 6 down), so swapping inner for outer fails four
  assertions instead of none — verified by making both mistakes on purpose and
  watching them go red.

  New in `vrmRetarget`: `LookAtEvaluator.h` (`LookAtRangeMap`, `LookAtRig`,
  `ParseLookAtRangeMaps`, `LookAtEvaluator`, `ResolvedLookAt`,
  `LookAtDiagnostics`) and `GetJointWorldTransform`, which composes a
  `RetargetedPose`'s ancestor chain — a gaze needs to know where the head *is*,
  and a retargeted pose states only where each joint sits relative to its
  parent. The library now links OpenUSD's `js` as well as `gf`, for the raw
  look-at block alone; it is a value library, so the boundary the
  `check_boundaries.py` gate enforces is unchanged. `motion_retarget` gains
  `--no-look-at`, for a pipeline that aims the eyes itself, and reports the eye
  joints it aimed and the samples that gazed. `--no-expressions` suppresses an
  *expression*-driven gaze along with the face, because those four weights reach
  the stage as blend-shape weights and by no other route, and the run says so
  rather than counting a gaze it did not write. Everything the gaze displaces is
  named exactly once however many samples ran into it: an eye bone the clip
  itself animates, and a gaze expression the clip also drives by name.

- **A clip's gaze reaches the canonical layer: VRMA look-at animation.** Look-at
  was untouched in every layer. `usdVrmaFileFormat` now reads the
  `VRMC_vrm_animation` `lookAt` block, `motionCore` carries what it says, and
  `/Animation/LookAt` authors it — the reading half, on the shape the expression
  half already established rather than a second design.

  **What travels is a point, not a direction.** VRMA points look-at at a node
  and the character watches where that node *is*; turning that into a gaze means
  knowing where the eyes are, which is a property of a rig. So
  `HumanoidPose::lookAtTarget` carries the place the clip named and
  `LookAtEvaluate` stays the consumer step — the same division
  `ExpressionResolve` is under. It is optional rather than sentinelled because
  **the origin is a place**: a producer can legitimately look at `(0, 0, 0)`, so
  "reported no target" cannot be spelled as a value of the target, and both
  comparisons read the presence before they read the point.

  Three decisions are the read rather than plumbing, and each has a fixture.
  **A target is placed where the file put it**: a look-at node may be parented,
  so the ancestors' stated transforms are composed in — `gazing_head.vrma` puts
  the target under a node translated 1.5 m up, and a position read in the node's
  own space would be a gaze at the floor. An ancestor the clip itself animates is
  warned about (`VRMA114`) instead, because evaluating it would be a scene
  evaluation at every instant rather than a clip read. **A clip says one of three
  things**, authored apart: a channel drives the node (time samples), the file
  *places* the node and nothing animates it (one default, since glTF leaves it
  there), or nothing places it — **no `vrm:lookAtTarget` at all**, because a gaze
  the file never gave is not a gaze at the origin. Placed includes placed by a
  **parent**: a target node with no transform of its own under a positioned
  parent is at that parent, and reading only the node's own TRS would report a
  gaze the file never withheld. An **unusable** node (`VRMA110`, `VRMA111`) costs
  the target and not the declaration — the block still raised the subject and
  still measured an offset, and dropping it would leave a stage indistinguishable
  from a clip that never mentioned look-at. And **the offset travels beside the
  clip, not on its samples**: `vrm:lookAtOffsetFromHeadBone` is a measurement of
  the rig the clip was authored on, so it is `uniform`, and a file that omits it
  is warned about (`VRMA112`) rather than quietly read as zero — that zero is a
  claim about the source rig, not a neutral default.

  New diagnostics: `VRMA110` (no usable look-at node), `VRMA111` (its node is
  already driven by a bone or an expression), `VRMA112`, `VRMA113` (a
  non-translation channel on the look-at node) and `VRMA114`. Three fixtures —
  `gazing_head.vrma`, `gazing_still.vrma` and `gazing_refused.vrma` — cover the
  animated case, the one placed only by a parent, and the one whose node is
  already driven; `expressive_face.vrma` gained the declared-and-unplaced one.

- **A clip's face reaches an avatar: `motion_retarget` authors the resolved
  expression weights.** The entry below produced values and nothing wrote them.
  The bake tool now reads the avatar's expression binds and its meshes'
  blend-shape bindings off the stage, resolves the clip's named weights against
  them, and authors `blendShapes` plus `blendShapeWeights` on the same
  `UsdSkelAnimation` it already binds to the rig.

  **Nothing is authored on the meshes, and that is the answer rather than the
  omission**: UsdSkel carries blend-shape weights on the animation the skeleton
  is bound to and hands each skinned prim the subset its own `skel:blendShapes`
  names, so authoring a binding would copy one the referenced avatar already
  owns — the same reason the rig itself is referenced and never copied. What the
  join does cost is a translation: an expression binds a blend-shape **prim**
  and an animation names the **token** the mesh binding it chose, so a blend
  shape no mesh binds resolves to a weight that cannot be authored at all, and
  is reported rather than dropped.

  Three decisions are the bake rather than plumbing, and each is measured by a
  fixture instead of asserted. **An expression key is a sample**: expressions
  live on the pose, so a blink keyed between two body keys adds that instant to
  the bake and the joints are read there too — which is also why `--resample`
  now resamples once, ahead of both halves, rather than moving the body onto a
  uniform timeline and leaving the face on the clip's keys. **A weight the clip
  never states holds**: a `.vrma` prim that declares a name and authors no
  weight is not a zero, and a sample that says nothing — a USD value block, the
  one way a clip can go quiet mid-track — leaves the previous weight standing
  rather than ending a blink the clip never asked to end. And **a material
  colour is resolved and not written**, because a colour slot is a material
  input and that layer owns what an MToon or a `UsdPreviewSurface` calls it; the
  count is reported so an operator sees the boundary instead of a silent
  omission. `--no-expressions` bakes the body alone.

- **`ExpressionResolve`: a named weight becomes the binds of one particular
  avatar.** Both sides have carried the join key since the entry below; nothing
  joined on it. `vrmRetarget` gains `ExpressionResolver`, which takes the
  expressions an avatar declares (`ExpressionRig`, built from its
  `vrm:expressionName`, `vrm:isBinary`, morph-target and material-colour binds)
  and turns one sample's `ExpressionWeights` into the blend-shape weights and
  material colours that sample means on that rig. It is a plain-value step like
  the rest of the library: the caller reads the binds off the stage, and
  nothing here opens, resolves or composes one.

  Four rules are the resolve rather than plumbing. **A reported zero is
  authored** — "this expression is off now" is a statement, and dropping it
  would leave the previous sample's weight standing on the target — while an
  expression the sample never reported contributes no entry at all, which is
  the same rule `ExpressionWeights::Find` already holds one layer down. **The
  clamp lands here**: the `.vrma` reader carries a weight outside `[0, 1]`
  verbatim on purpose and the specification's clamp belongs to whoever applies
  it to a rig, so this is that layer, and the operator is told which name it
  was. **`isBinary` rounds on the way to the binds**, not only in the scalar
  query — a partly-open eyelid is exactly what the flag says the rig cannot
  show. And **a material colour is carried as `(totalWeight, weightedTarget)`**
  with an `Apply(base)` that lerps, because the material's own value is the
  caller's and this library will not read it; two expressions driving one slot
  accumulate, and a channel driven outside `[0, 1]` — past it, or below it
  through a negative bind weight — extrapolates with a warning instead of being
  quietly corrected into a value no bind asked for. A weight that is **not a
  number** clamps to 0 and is named beside the out-of-range ones, because every
  comparison against NaN is false and a range test alone would call it legal and
  pass it to the binds unreported; a bind missing half its identity — no target
  path, no material, or no colour slot — is skipped with a warning rather than
  accumulated under an empty key.

  Resolving authors nothing by itself — the values it produces reach a stage
  through `motion_retarget`, which is the entry above.

- **`vrm:expressionName` on the avatar side, which is the key an expression
  actually joins on.** A `.vrma` clip has authored the verbatim expression name
  since v0.8.0 and the importer authored none, so the two halves of an
  expression — the clip's weight and the avatar's morph and material-colour
  binds — had only their prim names in common. Those are not the same string:
  `usdVrmFileFormat` sanitizes through its own hashed-fallback table and
  `usdVrmaFileFormat` through `TfMakeValidIdentifier`, so a Japanese expression
  name lands on `Expression_739d0383` on one side and something else on the
  other, and a name that had to take a `_2` collision suffix diverges even in
  ASCII. `VrmExpressionAPI` gains `uniform token vrm:expressionName` carrying
  the canonical VRM 1.0 expression name. It is an added optional typed
  attribute, so the **schema contract stays at v1** — an old reader ignores it,
  and a stage authored before it is still a legal v1 stage. This is the
  prerequisite `ExpressionResolve` was waiting on; the resolve step itself is
  still open (Motion Phase G).

- **VRM 0.x expression presets migrate to the VRM 1.0 vocabulary.** They are two
  different name sets — 0.x `BlendShapePreset` says `joy`, `sorrow`, `fun`, `a`,
  `blink_l` where 1.0 says `happy`, `sad`, `relaxed`, `aa`, `blinkLeft`, and only
  `neutral`, `angry` and `blink` are spelled the same. A `.vrma` clip is a VRM
  1.0-era file and only ever spells the 1.0 names, so a 0.x avatar carrying its
  own vocabulary could not be joined to one at all: the join key would have
  matched on three presets out of seventeen. The reader now migrates a 0.x
  `presetName` on the way in, folding case first, so both the prim name and the
  key are the 1.0 spelling — the same normalization the importer already applies
  to 0.x weights (0..100 → 0..1), `BlendShapeGroup` and `SecondaryAnimation`. A
  custom name, an `unknown` preset and a `presetName` outside the enum are
  carried through untouched, and the raw 0.x block keeps every original spelling.

- **`VRM152`: an expression name declared more than once.** The join key has to
  be unique or it is not a key — two prims answering to one name means a
  resolver silently binds whichever it reaches first, which is the same
  invisible loss as the uniquifier bug below, arriving one layer up. Reachable
  in both versions: VRM 1.0 can declare a name under both `expressions.preset`
  and `expressions.custom`, and VRM 0.x `blendShapeGroups` is an array. The
  importer keeps the first declaration, warns, and leaves the rest in
  `vrm:rawExtension` — the rule `usdVrmaFileFormat` already applied to a clip
  (`VRMA107`). A `duplicate_expression_name.vrm` negative fixture pins it.

### Changed

- **The recorded-trace format is version 3**, adding a `lookat x y z` line — at
  most one per frame, like `contacts`. Format 1 and 2 files still parse, a
  `lookat` line in one of them is refused the way an `e` line in a format 1 file
  is, and the writer emits the current version as it always has; the committed
  corpus was regenerated for that reason rather than edited. No live producer
  emits a gaze today. Carrying it anyway is what stops a recorder from silently
  dropping a pose field and making a replay differ from the session it claims to
  reproduce.

### Fixed

- **The importer's name uniquifier could hand two source entries the same USD
  prim name, and the loser vanished without a diagnostic.**
  `VrmMakeUniqueNames` disambiguated by counting how often each sanitized *base*
  had been seen, appending `_2`, `_3`, … to the repeats — without checking
  whether some other source name already spells that result. A file naming three
  meshes `Body`, `Body` and `Body_2` produced `Body`, `Body_2`, `Body_2`. The
  second one is not an error at the USD layer: `UsdGeomMesh::Define` (like every
  other `Define`) on a path that already exists returns the **existing** prim,
  so the authorer wrote the third mesh's data over the second's and reported
  success. Measured on a fixture before the fix: five source meshes, four prims
  on the stage.

  It uniquifies against the names already claimed now — the same correction the
  `.vrma` authorer had already made for expression prims — so an earlier entry
  always keeps the name it took and a later one moves to the next free suffix.
  Every name the importer authors goes through this function: meshes, joints,
  materials, morph targets, expressions, collider groups, springs, constraints
  and animation clips. A new `usdvrm_path_util` CTest target covers the
  uniquifier directly (both orderings of the trap, sanitize-then-collide, and
  the non-ASCII and empty-name fallbacks), and `names.vrm` carries the collision
  shape end to end.


## [0.8.0] — 2026-08-31

### Fixed

- **The VRMA reader was built by every CI lane and tested by almost none of
  them.** `plugins/usdVrmaFileFormat/CMakeLists.txt` guarded its test
  subdirectory on `USDVRMA_BUILD_TESTS`, a name nothing outside that file ever
  set, while the repo root defines `USDVRM_BUILD_TESTS` and the three sibling
  bundles read it. In a composed root build `PROJECT_IS_TOP_LEVEL` is false in
  that scope, so the option defaulted **off** and the bundle registered no CTest
  target at all: the workspace lane compiled and linked
  `libUsdVrmaFileFormat` on Windows, macOS and Linux and ran zero of its tests,
  on every PR, since the workspace cells landed. Nothing failed and nothing was
  skipped — a test that is never registered is not reported anywhere, which is
  why a 100%-passing suite hid it. The committed build caches say it plainly:
  `USDVRMA_BUILD_TESTS:BOOL=OFF` in every root cache and `ON` in the standalone
  bundle cache. Renamed to `USDVRM_BUILD_TESTS`; the workspace suite goes from
  113 to 115 tests, `usdvrma_python_smoke` and `usdvrma_fixture_deterministic`
  among them, and the bundle still registers both when configured standalone.

- **A tool member of the installed product could not find the product's own
  data.** `motion_bvh_convert` derives its profile directory from its own
  executable path, and its installed-prefix rule was
  `<exe>/../share/usd-vrm-plugins/profiles/motion` — correct for a `cmake
  --install` prefix and for a member archive unpacked on its own, and one
  directory too shallow for the aggregate product, where `ost plugin product
  install` lands a tool member at `<prefix>/tools/<member>/bin/` while the
  product's data goes to `<prefix>/share/`. A converter that finds no profile
  refuses every file it is given, so the whole BVH path was unusable from a
  release artifact: the profiles shipped, byte-identical, to the directory
  WORKSPACE.md §5 names, and the tool beside them looked somewhere else. The
  locator now carries both installed layouts.

  Both three-parent rules — the new one and the repository one beside it, which
  had always been unguarded — are now offered **only when the executable really
  is in a `tools/<member>/bin/`**. From a `cmake --install` prefix they would
  otherwise climb two levels *above* it, which is where a sibling install of
  this product puts its own `share/`, and the result is not a refusal but a
  conversion reading another prefix's profile. That is the near-miss the
  no-default-profile rule exists to prevent, arriving through the search path
  instead of through a flag. Caught in review before either rule shipped, and
  reproduced first: an executable at `a/b/prefix/bin/` with a profile only at
  `a/share/…` converted instead of refusing.

  **It was found by running it, and nothing else could have found it.** The
  destination is stated in `openstrata.toml`, in the root `CMakeLists.txt`, in
  WORKSPACE.md §5 and in `ProfileLocator.h`, all four agree, and one of them was
  describing a different prefix. `ost` 0.22.3 supplied the staging in August and
  report 36 §4 recorded in as many words that the staging was what had been
  proven and not the run; this is the run, and it failed the first time.

- **A rotation too small to square came back un-normalised, in the two
  conversions that had not been fixed.** `GfQuatf::GetLength()` squares in
  float, and both `vrmAdapterVmc::ToCanonicalRotation` and
  `motionSource::ConvertRotation` divided by it while the check that admitted
  the rotation was made in another precision: `CheckTransform` sums its squares
  in double, and `ValidateSourceAnimation` asks only that the four components
  are not *exactly* zero. So a quaternion whose components sit below roughly
  `1.9e-23` passes the check, underflows the length to exactly `0.0f`, takes the
  "nothing to divide by" branch that exists for a genuinely zero rotation, and
  is returned unchanged — after which it multiplies into every composition along
  its path and collapses the chain, with `AngleBetween` reading the result as
  garbage rather than as an error. Both sites now form the length in double and
  narrow after the divide rather than before it, which keeps the whole
  representable range. `vrmAdapterMocopi` was fixed in the same shape on
  2026-08-12 and named these two as still open; they were.

  The likelihood is low and stated as such — a real sender's and a real file's
  components are O(1), and no capture in any corpus here comes near this range.
  What earns it a fix is that it fails **silently**, and that this project has
  already paid once for two magnitudes formed in different precisions (the note
  on `AngleBetween` in `libs/motionCore/src/Compare.cpp`, which cost a red
  macOS-arm64 lane that was green on x86-64). Each site has a test at a
  magnitude no session will produce, verified negatively: restoring
  `GetLength()` turns both red.

- **An adapter could export an imported target its package config never
  resolved.** A `PUBLIC` dependency lands in the exported target's
  `INTERFACE_LINK_LIBRARIES`, so a consumer doing `find_package` on the
  *installed* adapter fails at generate time with "the target was not found" —
  and CMake does not go looking for it, even when that package's own config is
  in the same prefix. Nothing in the tree could see it: a composed workspace
  build and `ost library build` both resolve every target in-tree and never open
  a config file, so the path that breaks is the standalone configure that
  roadmap §12 asks for by hand. Each adapter's boundary check now cross-checks
  its link line against its config template, so an unresolved edge is a red test
  name.

- **Every boundary check located `dumpbin` under a glob naming one Visual Studio
  release.** `dumpbin` is not on `PATH` outside an MSVC developer shell, so each
  `check_boundaries.py` falls back to searching Program Files — for
  `Microsoft Visual Studio/2022/...` literally. A Visual Studio upgraded in place
  leaves that directory empty beside a populated one for the new release, and all
  nine checks then fail with "dumpbin was not found": nine red names, none of
  them about a boundary. The release is a wildcard now, since the locator only
  ever needed `/dependents`.

- **Four defects in `vrmAdapterVmc`'s UDP receiver, all four of them copies.**
  They were found in `vrmAdapterMocopi`'s receiver on 2026-08-11 — which was
  written by copying this one — fixed there, and recorded in that file as
  still present in the sibling. They were.

  - **An over-long datagram was handed on half-read.** The receive buffer was
    exactly `MaxDatagramBytes`, and the header claimed that made truncation
    impossible. On POSIX it does the opposite: `recvfrom` truncates silently and
    returns the buffer's length, which is indistinguishable from a datagram that
    was exactly that long — so the half-read datagram reached the decoder and
    was refused there as `VRM_VMC_PACKET_MALFORMED`, blaming a sender for this
    adapter's own truncation. The buffer is now one byte above the bound, and an
    over-long datagram is counted in `datagramsTruncated` and dropped. Reachable
    over IPv6, which `listenAddress` has always accepted — and reached for real
    on the Linux lane, where `vrmAdapterVmc_udpReceiverTruncation` runs rather
    than skipping.
  - **A long finite `Receive` timeout waited forever.** 2147483.647 seconds or
    more converted to `-1`, which is not a large number of milliseconds but the
    sentinel meaning "block until something arrives" — so the one caller that
    asked for a bound got the unbounded wait a bound exists to avoid. Now
    clamped to the longest wait a poll can express.
  - **A poll wake-up was treated as traffic without inspecting `revents`.**
    `POLLERR`, `POLLHUP` and `POLLNVAL` are reported whether or not they were
    requested, so a ready descriptor with nothing to read sent a caller waiting
    indefinitely into a tight loop at 100% of a core. A ready descriptor without
    `POLLIN` is now `ReceiveStatus::Failed`.
  - **`idleReceives` counted calls that met something.** The retry tail is
    reached after an over-long datagram or a transient platform error, and
    charged both to the counter whose meaning is "found nothing waiting" — so a
    session report could describe a quiet socket while `datagramsTruncated` and
    `receiveErrors` counted what reached it.

  Two long-standing differences from the sibling receiver were decided in the
  same change rather than left to drift further. `Open` now resets the receive
  statistics, because it restarts the clock they are stamped against either way;
  `Close` releases the receive buffer. The silence timeout
  (`VRM_MOCOPI_DEVICE_UNAVAILABLE`) stays mocopi-only on purpose — this
  adapter's frozen diagnostic set has no code for it, and adding one is a
  contract change rather than a fix. It arrives when the shared transport
  library does.

- **Four of `vrmAdapterMocopi`'s six corpus binaries aborted with no message
  on a path that is not a directory.** `std::filesystem::directory_iterator`
  **throws** on one, nothing in those files caught it, so the process called
  `std::terminate` — on Windows an abort with exit `0xC0000409` and nothing
  printed at all, where each binary has a "no captures in …" line it plainly
  meant to print. Reachable two ways: a mistyped argument, and a corpus present
  at configure time and absent at test time in a relocated or packaged build
  tree. Two of the six had the check and four did not, which is what six copies
  of one scan cost: the copy a review pointed at was fixed and the others were
  not.

  The scan is now one function in `tests/corpus.h`, and so is the replay step
  the corpus passes share — push, poison, latch the restart, sample at the
  delivered frame's stored timestamp. That second half had three copies with
  **three** buffer disciplines, two of which are the two halves of
  `vrmAdapterMocopi_loopbackCorpus`'s comparison: if they drift, that test
  compares one pipeline against another and reports the difference as a
  statement about the socket. The buffer is no longer a per-copy decision but a
  property of the shared call, which poisons it the moment the push returns —
  so the file half now carries the lifetime check the wire half already had.

  Test-only; no library, tool or CTest name changes. The sibling adapter's
  tests have the same shape and the same duplication, and the arrangement
  transfers — the types do not, so a VMC copy is the same change one namespace
  over rather than a second consumer of this header.

- **A tracker frame that lost its hips rotation snapped every bone under it by
  the hips' whole orientation.** `SolveTrackerPose` composes a bone's local
  rotation as `inverse(parent chain) * observed world`, and an ancestor it did
  not author contributed identity. That is right for a bone **nobody observes**
  — nothing ever authors a spine, and a consumer leaves it at rest for the whole
  session — and wrong for a bone the assignment **did** place that carried no
  rotation in one frame: every consumer in this workspace replays with
  `missingBones = hold`, so what it holds for that ancestor is the value from a
  frame ago, not identity. The children were divided by identity and composed
  against the parent.

  Measured on a real 20 s standing session: the hips tracker sent a position and
  no rotation on **16 of 777 frames**, and on each of them the head and both
  feet moved **33.6°** while the hips did not move at all — 33.6° being the
  hips' own orientation in that frame, `2·acos(0.957319)`, to five figures. It
  survived `motion_capture` into the clip, so an avatar driven by that recording
  snapped a third of a right angle and back, sixteen times, while the operator
  stood still. Every take in the session had it: 16, 6, 5, 5 and 19 frames.

  A bone whose **assigned** ancestor could not be oriented in this frame is now
  withheld rather than authored, and reported in the new
  `TrackerSolve::withheldWithParent` — a fifth vector kept apart from
  `withoutRotation` because the two have different fixes, one a strap and the
  other the frame the strap arrived in. `vrchat_osc_record --inspect` prints it
  beside the others. The frame becomes a hold in full: the body stays where the
  previous frame left it, which is what a frame with an unknown root orientation
  says. Carrying the last known parent forward would be better motion and needs
  a stateful solve; this one is a function of one frame by construction. Worst
  single-frame step on that take: **33.60° before, 2.46° after**, with nothing
  over 5°.

  **Two tests asserted the defect**, which is the part worth recording:
  `TestAPositionOnlyTrackerCannotOrientAJoint` required the head to be `placed`
  under a rotation-less hips, and the export suite required that frame to carry
  three bones. Both now assert the opposite and say why in the file. Every
  internal check of this path passed against the defect and had to — within one
  frame the composition is exactly self-consistent — and what caught it was a
  comparison against a path that never has an absent parent, on the one take
  whose answer is known in advance
  ([report 04](docs/reports/motion/04-2026-08-31-cross-source-carry-drop.md) §5).

  **Both ways an ancestor fails to arrive are read**, which the first version of
  the rule did not do: a statement whose tracker sends no rotation produces a
  binding, and a statement whose tracker does not arrive at all produces none and
  lands in `TrackerAssignment::absent`. A consumer holds the bone identically
  under each, and the recorded session has both. A `NothingSolved` refusal now
  also says when withholding is why, because the per-region tallies are over
  solved frames and that line is otherwise the only thing a refused frame prints.

### Added

- **A consumer that is not us.** Every package this workspace installs is now
  configured, built, linked and *run* from a clean prefix by an external CMake
  project that names no workspace target — the check every other lane here is
  structurally unable to make. A composed workspace build defines each target as
  an alias in one CMake project and `ost library build` composes
  `requires.libraries` the same way; neither opens a `*Config.cmake` at any
  point, so the configuration that fails is the one nothing runs. On 2026-08-29
  it shipped: two adapters named `osc::osc` on their installed interface link
  line with no `find_dependency(osc)` in either config, and **all 17 lanes were
  green** (the fix is under *Fixed* above; this is the general answer to it).

  Four pieces, and each is separately runnable by hand because a lane nobody can
  reproduce is a lane nobody can debug:

  - [`docs/architecture/PACKAGE_CONTRACT.md`](docs/architecture/PACKAGE_CONTRACT.md),
    the binding distribution contract, derived from the CMake sources rather
    than written beside them. Per package: the name a consumer writes in
    `find_package`, the target it links, the header root, the packages that must
    resolve first, the platform libraries that travel on the link line, whether
    it is in the aggregate product, and whether standalone installability has
    been **measured** or only reviewed. Twelve packages take a `find_package`
    contract; three plugin bundles export no target and install no config **by
    design**, and the document says so rather than leaving them absent.
  - `scripts/check_package_consumer.py`, the driver for one package: install it
    and its required packages into a scratch prefix holding nothing else, copy
    the fixture **outside** this repository so it cannot resolve through the
    source tree it came from, configure, build, run, and report which of the
    contract's six acceptance criteria were met.
  - `scripts/run_package_consumer_lane.py`, every package on one host. **Which
    packages it runs is read, not listed** — every contract row, through the
    driver's own parser — so a thirteenth package cannot enter the contract
    without entering this lane.
  - `scripts/check_package_closures.py`, criterion 6, which no single host can
    answer: do the three platforms agree about the closure?

  **Twelve of twelve pass, and no config file failed** — which is not what the
  plan predicted, and is worth stating that way round. The compliance was
  already there; what is new is that it is *measured*, by something that is not
  this workspace. What did fail was the harness, three times, and each would
  have made a later run lie. **Forty-eight mutations of the installed prefix**
  back the twelve: 41 caught, 5 refused before install because masking makes
  them inert on any host, and 2 inconclusive because `liveTransport`'s one edge
  is conditional and unreached on Windows. Masking is a property of the prefix
  and true everywhere; a condition is a question about the host, and this driver
  evaluates none — collapsing those two into one refusal threw away a real
  catch, so they are two answers.

- **A PR-gating package-consumer lane on all three OS**
  ([`.github/workflows/package-consumer.yml`](.github/workflows/package-consumer.yml)).
  Three jobs — read the pins, consume on each platform, compare the three
  closures — building from a prefix that holds no build tree, which is the whole
  point and the easiest property to lose by accident. Twelve packages × three
  platforms, green, and **every workspace target in every closure is present on
  all three or on none**. The one difference the contract permits is present in
  both directions: `ws2_32` on Windows, `Threads::Threads` on macOS and Linux,
  for `liveTransport` and the three adapters that inherit it. Every `Standalone`
  cell in PACKAGE_CONTRACT.md §4 is now unqualified.

  **It copies no pin.** `release.yml` is the other hand-authored workflow here,
  and it hand-copies an X11 step, an `ost` version and three runtime digests —
  which is how it went stale and failed a tag build while every PR lane stayed
  green. So `scripts/ci_pins.py` reads the runners, the digests, the host
  packages and the Python version out of `openstrata.ci.yaml` through `ost ci
  matrix`, and the three OS come from the three `verify: test` workspace cells
  rather than a list in the YAML. The one pin that cannot come from `ost` is
  which `ost`, so it is read from the contract with a regex and then checked by
  the tool it installed. **`--expect 3` is a check, not a formality**: criterion
  6 asks whether three platforms agree, and a lane that quietly asked it of two
  would print a pass to a different question.

  Criterion 6 needed a contract before it needed a script — read strictly it is
  unimplementable and read loosely it is vacuous, because the three runtimes are
  three separate builds of OpenUSD. PACKAGE_CONTRACT.md §5.1 states the
  partition: a workspace target agrees or it is a defect; a declared platform
  dependency is present exactly where its cell says and **absent elsewhere**;
  everything else is attributed to the external package that brought it, and the
  attribution is what gets checked. Ten cases verify the comparison and each was
  made to happen, including the tenth, which is the answer that is not a
  verdict: two platforms end in a setup refusal, because a question about three
  is not answered by two.

  **The two runs it took to get green both found defects, and neither was in a
  package.** The first caught a *runtime*: a pulled runtime's CMake package
  carries the producing machine's Python paths, in `pxrConfig.cmake`'s guarded
  variables and again in sixteen imported targets'
  `INTERFACE_INCLUDE_DIRECTORIES`, and no `-D` overrides the second — the four
  packages that passed everywhere are exactly the four whose closure never
  reaches `pxr`
  ([report 37](docs/reports/ost/37-2026-08-30-v0.22.6-runtime-python-paths-from-the-producer.md),
  which carries the upstream P1). That is this track's premise arriving from a
  direction it did not predict: the lane was written to catch a package that
  could not be consumed from outside, and the first thing it caught was the
  runtime under it, for the same reason — nothing had ever configured against
  one without `ost build` in front of it.

  It also closes the raw-library half of
  [#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113): on
  `macos-15` and `ubuntu-24.04`, `vrmAdapterMocopi`'s consumer links
  `Threads::Threads` and **no** `ws2_32` — the absence a Windows run
  structurally cannot see, open since the receiver grew a platform link.

- **`scripts/check_docs.py` refuses a `*Config.cmake.in` with no row in
  PACKAGE_CONTRACT.md**, and a row naming a package that does not exist. Five
  ways to fail it, each made to fail before the check was believed. It landed
  **before** the CI cell rather than after it, on purpose: the document it
  checks was five days old, which is the moment to add such a check rather than
  after the drift has had time to happen.

- **VRC-7, the cross-source comparison: three paths, two performances**
  ([report 04](docs/reports/motion/04-2026-08-31-cross-source-carry-drop.md)).
  Five labelled sequences performed on 2026-08-15 and again on 2026-08-30,
  driven to canonical clips through `vrmAdapterMocopi`, `motionBvh` and
  `vrmAdapterVrchatOsc`, and compared at the canonical layer and nowhere lower.
  Not one physical take: this sender's transfer format is exclusive and it
  records no BVH while sending OSC, so the loss is stated rather than papered
  over — report 01's median 0.084° per bone does not survive two performances
  and no tolerance widening buys it back.

  The deliverable is the per-path carry/drop table, and **report 01's one row
  that mattered is closed**: all three paths now carry the body's travel, and
  re-running the identical 2026-08-15 capture through today's `mocopi_record`
  prints that report's sentence with the verb the other way round. What is lost
  is now distributed rather than concentrated — the BVH export re-bases the body
  to the origin and loses the room, the tracker path loses eighteen bones and a
  third of the frames its sender emits, the two kinds of root are 6 cm apart,
  and no path carries tracking state because neither wire has a field for it.

  **A labelled head turn agrees on all three paths**: a head turned to the
  operator's left arrives as a positive yaw in canonical space, from three
  derivations that share no code, and the two 2026-08-15 paths agree to 0.00°
  and 0.04 s — the control that says the measurement measures what it claims to.
  The one difference the comparison could not attribute is stated as `unknown`
  rather than absorbed: 15.5° on the right-hand head turn, which two
  performances cannot separate from a sender difference.


- **A VRChat OSC session reaches a rig, and the tracker path is connected end to
  end** (VRC-6). `vrchat_osc_record` gains `--export-trace`, which decodes a
  recorded capture, assembles it into frames, solves each against an operator's
  `--assign` statement and writes a `motion-capture-trace`. `motion_capture` and
  `motion_retarget` then replay it onto a rig **unchanged**, knowing nothing
  about VRChat OSC or about trackers — the hand-off is a file, because no tool
  in the aggregate product may link an adapter. Two supporting flags:
  `--unplaced refuse|ignore|hold` for an observed tracker no statement places,
  and `--no-root-motion` for a session whose hips position is not trusted.

  **This is the first taker of `adapters/*/tools/* -> motionTracking`**, which
  had been a permission in the workspace contract with nobody using it since
  VRC-4a. It is a *tool's* permission and the adapter library still may not name
  that package: which tracker is on which body region is an operator's statement
  about a rig, and a decoder that resolved one would have invented a calibration
  and hidden it inside itself. `--assign` is therefore **required and has no
  default**, and a near-miss region name is refused rather than guessed at.

  **The new end-to-end test asserts a partition rather than a count.** On a
  fixture that walks half a metre and turns a head, four rig joints move and
  fourteen hold their rest pose *exactly* — not within a tolerance, because
  nothing authored them and the retarget computes the same product at every
  sample. Four of the fourteen sit between a driven hip and a driven foot, which
  is where a solve that had begun estimating would show up first. It needed a
  new corpus fixture and the reason is a measurement about the other fifteen:
  every capture in this corpus drifts a millimetre and a quarter of a degree per
  frame, which proves a decoder is not returning its defaults and is **too
  little** to survive a retarget's rest-pose correction as a visible rotation —
  so a session that arrived perfectly and one that never arrived would both have
  read as "nothing moved". `rig-motion` is generated, `unobserved`, and says so.

  **One inherited justification turned out not to transfer, and the suite now
  says so.** Both sibling recorders refuse to splice a restarted capture into
  one trace because their sender's own clock goes back to zero, so the halves
  overlap in time. This wire carries no sender clock at all — three floats and
  no timestamp — so the only clock is the receiver's, and it runs *forward*
  across a restart. The refusal stands on a different footing: two peers, each
  calibrated by its own receiving application, with nothing relating the two
  tracking spaces. Copying the sibling's reason would have been a claim about
  this wire that measuring it contradicts.

  Two CTest names, `vrchat_osc_record_export` and
  `vrchat_osc_record_endToEnd`, and the second closes an absence this adapter's
  `tests/CMakeLists.txt` had stated on purpose since VRC-0.

  **Each adapter gains a `<PACKAGE>_BUILD_TOOL` option, on by default.** The
  package-consumer check installs every package from a prefix holding exactly
  its declared closure, which is what lets a mutated config file be attributed
  to the config file — and a CLI's edges are the tool's rather than the
  library's, so building one there would need packages no row of
  `PACKAGE_CONTRACT.md` names. The driver switches the tool off for every
  source tree with a `tools/` directory. All three adapters define the option
  even though only one needs it: a rule with an exception that only one of
  three files shows is a rule nobody reading the driver can check.

- **Assigned tracker observations reach a canonical pose, and the solve stops
  where IK begins** (VRC-5). `motionTracking::SolveTrackerPose` takes an
  operator's assignment and the observations it was made from and produces a
  `motion::HumanoidPose`. The pre-IK observation itself gets **no type in
  `motionCore`**, decided in the contract before any code: every consumer of
  that header takes a pose, so a tracker sample there would be a value with no
  reader in the aggregate product carrying an equality, a comparison and a
  trace-format obligation regardless. It lives beside the vocabulary and the
  assignment instead, and `motionTracking` takes the one edge that costs —
  `motionCore`, for the solve alone.

  **The solve is direct and says so.** An observed orientation becomes the
  local rotation of the bone its region names, composed as
  `inverse(parent chain) * observed` so that forward kinematics reproduces the
  observation exactly; a joint nobody observed stays at **rest** rather than
  being estimated; and an observed **position** is consumed in one place only,
  the hips, on the root/hips rule this contract already carries. Every other
  position is *reported unused* rather than dropped, because consuming one is
  IK and IK needs limb lengths that belong to a target rig this layer does not
  have. The knees and the elbows are placed onto no bone at all — a strap
  between two bones is not either of them, and with no limb lengths a bent knee
  and a rotated thigh are the same observation — and they are reported as
  unsolved rather than refused, so a rig carrying them still produces a pose.

  **`TrackerObservation`'s equality reads the flags first and the values only
  under them**, which is `motionCore::RootMotion`'s rule: an unset half is not
  required to hold its default, so a stale number under a cleared flag must not
  make two reports of "this tracker sent no position" differ.

  **The invariant is what the suite is built on**, and one mutation proved the
  fixture wrong before it proved the code right: the composition-order mutation
  survived, because the fixture turned the hips and the chest about the *same
  axis* and two rotations about one axis commute. The axis changed and the
  mutation was caught. Seventeen mutations and nineteen boundary injections in
  all, each shown to fail first.

  **The boundary check is now per file rather than per library**, which is what
  the edge cost: the vocabulary and the assignment keep the empty edge set they
  were given and are still scanned for `motionCore`, for OpenUSD in any form and
  for a bone; the solve may name `motionCore` and OpenUSD's `Gf` value types and
  nothing else; the alias between a region and a bone is forbidden in both
  halves in either direction; and **a file in neither half is an error**. The
  installed package declares `motionCore` and `pxr`, its consumer fixture
  compiles the new header, and dropping the config's `find_dependency` line
  fails that fixture at compile time.

- **`motionTracking`, a new shared library: which tracker is which body
  region** (VRC-4a). A tracker index is not a body role, so an adapter that
  mapped one onto a bone would have invented a calibration and hidden it in a
  decoder. Assignment is therefore a **third** thing between decode and solve,
  belonging to neither end, and it now has a home of its own — named in
  [WORKSPACE.md](docs/architecture/WORKSPACE.md) §1, §2 and §5 in a change of
  its own **before** any file existed, on the procedure `liveTransport` and
  `osc` were named by.

  **A region is not a bone, and that is the load-bearing claim.**
  `TrackerRegion` reads like a short `HumanBone` and is deliberately not one:
  a knee tracker sits on a strap between two bones and a chest strap observes
  a ribcage rather than the joint a solve produces. The day the two become
  aliases, assignment is a lookup and the solve has nothing left to do — so
  §2 forbids it *by name*, and the boundary check reads the sources rather
  than the link line, because an enum copied by hand leaves no link line to
  fail on. Eleven regions cover the three rigs a tracker source can present
  and nothing beyond them.

  **Explicit statement, exactly as `motion_bvh_convert` requires a named
  profile** — `t1=head t2=leftHand t3=rightHand`, no default and no name
  heuristic, because a detector written before the contract settles the
  contract on whichever rig was recorded first. Automatic assignment from
  rest geometry stays a later aid *over* this contract.

  **A set it cannot place is three answers with a case each**, and an
  observation can miss a statement in two directions: a tracker the statement
  does not place is *unplaced*, and a stated tracker that did not arrive is
  *absent*. `Refuse` reads the first, `Ignore` reads neither, and **`Hold`
  reads both** — which is what makes it the policy its own row describes, a
  rig coming up one device at a time being short of a stated tracker rather
  than carrying an extra one. `Refuse` and `Hold` both refuse and the
  enumerator is the difference a live caller acts on: `UnplacedTracker` will
  still be true next frame so a caller stops, `Held` may not be so a caller
  keeps the assignment it had. Under the other two an absence is data and a
  partial rig still assigns, on the rule a missing tracker already follows one
  layer up. `NothingPlaced` catches an empty binding set no policy objected
  to, which is what makes `Ignore` a refusal rather than a success with
  nothing in it.

  Fourteen mutations, each a plausible wrong *policy* rather than a syntax
  error, each failing a case named for what it breaks; twelve boundary
  injections, each refused. **The mutation pass found a hole and the fix was
  a deletion**: a guard refusing a second `=` in a statement could not be
  reached by any input, because `head=hips` is already not a region this
  vocabulary carries. It is gone rather than documented. It linked nothing at
  all when it landed — no `motionCore`, no OpenUSD, no platform primitive — and
  the solve entry above is what changed that, for the solve alone. It carries no
  diagnostic code either: a refusal names the event and the caller supplies the
  code, as `motionSource` and `osc` already do.

  **The `adapters/* -> motionTracking` prohibition is enforced in the three
  adapters' own checks**, and it is the first name on those lists that needed
  to be. Every other one is also a link edge, so the CMake allowlist would
  catch it anyway; this package is enums and a policy over them, so an adapter
  could include its header and name `TrackerRegion` with no link line to fail
  on — the same argument, read from the other end, that this library's own
  check makes about the bone enum.

- **The VRChat OSC adapter assembles frames, and the boundary is a
  measurement rather than a convention** (VRC-4). VMC marks a frame with a
  clock message; this wire sends three floats per address and nothing else,
  so what stands in for a clock is what a real session was measured doing: a
  frame is a **burst** of eight datagrams inside a median 0.053 ms, with
  ~17 ms between bursts. `TrackerFrameAssembler` cuts it with **two** rules,
  because they fail differently — a repeated tracker and channel closes the
  frame and needs no clock at all, and a datagram past a 5 ms window closes
  one that no repeat would. On the recorded sender the window gets there
  first and the two produce identical frames, which the suite measures by
  running one stream twice with the window on and off rather than asserting
  it.

  **Seven policies, each with a case and each with a fixture that produces
  it**: repeated updates for one tracker, partial tracker sets, timeout,
  stale samples, the head reference, source reset and calibration
  discontinuity. Three are worth naming. A **partial** sample — a tracker
  that reported a position and no rotation, which is about once a second on
  this wire — is emitted and never repaired, with flags saying which halves
  are real, because a defaulted rotation of identity is bit-for-bit what a
  tracker at rest reports. A **silence is not a restart**: a peer that
  differs is a session boundary and a gap of any length is `SOURCE_TIMEOUT`
  and nothing more, so a caller that supplies no peer never sees a restart —
  which is only testable from a file because the capture format grew a
  per-record peer in the change above. And a **recalibration** is told from
  motion by simultaneity rather than by size: every observed tracker moving
  at once, never one of them, because one tracker jumping is a tracking
  glitch and not a new room.

  Eleven mutations, each a plausible wrong *policy* rather than a syntax
  error, each failing a case named for what it breaks, with the restored
  source green — and **one did not fail on the first run**: a restart
  that kept the old session's trackers was invisible to a case that restarted
  into the same four trackers, so that case now restarts into a three-point
  rig four metres away and observes both halves of the policy. The mutation
  found a hole in the test rather than in the code. **A review then found two
  more of the same kind**, and the last two mutations are them: the
  new-session flag was a local of `Push` and was dropped whenever the datagram
  carrying the new peer contributed no message this layer accepts — ordinary
  on a well-known port — so the diagnostics said a session restarted while no
  frame said one began; and a `TrackerChannel::Count` from a caller-built
  packet indexed two fixed-width arrays out of range before the conversion's
  own guard could refuse it. Neither is reachable from a fixture. A third
  finding was a counter that could only ever be zero, and it is gone rather
  than documented. Three new corpus fixtures
  — `session-restart`, `silent-gap`
  and `calibration-jump`, the first two being the same 4.8452 s gap told
  apart by identity alone — and two new CTest names,
  `vrmAdapterVrchatOsc_frameAssembler` and
  `vrmAdapterVrchatOsc_frameCorpus`. **No body role is named anywhere in
  it**: a frame carries tracker identities, and which tracker is on which
  body region stays a generic policy outside this adapter.

- **A packet capture can say who sent each datagram, which is the only
  restart marker one of the three wires has.** The `liveTransport` capture
  format gains a `p <endpoint>` line: it names the peer of every record
  after it until the next one, `p -` says the peer of what follows is
  unknown, and `RecordedDatagram` carries a `peer` beside its bytes. Before
  it, a capture named one peer in its **header** for a whole file — which
  was free until a mocopi `VRChat (OSC)` session was recorded stopping and
  starting again: that sender marks a restart with a new ephemeral source
  port and with **nothing else**, no session identifier, no rest table and
  no handshake, so the live session saw two peers and `--inspect` on the
  same capture reported one
  ([report 02](docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) §4).
  Every fixture-driven test of restart behaviour was therefore exercising
  the silence and not the identity change, which is the difference between
  a source that paused and a second source that began.

  **No committed fixture changes a byte, and that is the property the
  spelling was chosen for.** The writer emits a `p` line only where a
  record's peer differs from the one before it, so a capture whose records
  name nobody is written exactly as it was — which is also why the format
  version stays at 1: the reader compares it for equality, so a bump would
  refuse every fixture in two corpora and turn an addition nothing yet
  reads into a whole-corpus rewrite. A change of peer is one line in a diff
  rather than one line per datagram, and the `d` record line is untouched,
  so the strictness that refuses `d 0.5 24 stray` is intact.

  All three recorders write the peer they received, and all three
  `--inspect` paths report the record's own peer where a capture carries
  one and the header's where it does not — so a capture of a two-peer
  session now reports two. The claim is measured in both directions: with
  the reader and writer reverted, `liveTransport_packetCapture` fails at
  its first peer assertion, and with the `--inspect` change reverted the
  recorder's harness reports `1 (192.168.1.8:51662)`, which is the exact
  reading report 02 recorded. A peer is transport identity and no decoder
  is given one.

  New CTest name: `liveTransport_packetCapture`. It is the first test of
  this format in the library that owns it — the three adapters' suites
  test their own magic — and the `p` line belongs to no adapter, so its
  magic is one nobody uses.

- **An artifact-only smoke for the BVH path**
  (`scripts/artifact_only_bvh_smoke.py`), which closes the v0.7.0 release
  condition *both paths running from release artifacts alone, profiles
  included* for the recorded half. It packages the aggregate product, runs
  `ost plugin product verify`, installs it to a fresh prefix **outside** this
  repository, and converts a real 17-second mocopi export there — 853 frames at
  50 Hz through 22 bound joints — with no `--profile-dir`, no
  `USDVRM_MOTION_PROFILE_PATH`, and nothing from this source tree on any search
  path.

  Two of its checks are the ones worth naming. Every shipped profile is compared
  **byte for byte** against `profiles/motion/`, because the failure that shape
  replaces was a *copy* that had stopped being the file
  `scripts/check_motion_profiles.py` validates. And after the conversion
  succeeds the installed profile is moved aside and the same command is re-run,
  which must now refuse — so "the tool found a profile" cannot pass for "the
  tool found the one this product ships".

  It runs in `release.yml` beside the clean-install smoke, against the archive
  that lane just proved digest-reproducible rather than a fresh package of its
  own. That places it in the one workflow no PR event runs, which is a standing
  caveat of that lane and not a new one.

- **`osc`, the OSC wire format once instead of once per adapter.** Packets,
  bundles and their flattening, addresses, type tags, arguments, and a refusal
  that names the byte and the address it refused at. It knows no address
  *semantics*: `/VMC/...`, `/tracking/...` and `/avatar/...` are all just
  addresses here.

  **It waited for a second consumer, and that wait is the whole of why it is a
  library now rather than in v0.7.0.** A decoder extracted on the strength of
  one caller is a decoder shaped like that caller, so the evidence had to be a
  caller that never says `VMC`. An address inventory written in
  `vrmAdapterVrchatOsc`, decoding real bytes through the VMC-owned decoder
  without moving it, needed **five VMC tokens** — one include path and four
  namespace qualifications — plus the export macro on its compile line, and its
  report on a VRChat session printed `VRM_VMC_PACKET_MALFORMED`. The plan had
  predicted three couplings: the namespace, the export macro, the diagnostic
  code. The measurement found exactly those three and nothing else.

  **A refusal carries no diagnostic code, and no neutral event enum either.**
  `liveTransport` has `TransportEvent` because its receiver raises two events a
  caller must tell apart; this decoder makes one distinction — a datagram is
  decodable OSC or it is not. Three invented neutral names would have been
  mapped straight back onto one adapter code by every caller and believed by the
  next reader. So `OscDecodeError` carries a subject and a detail, and each
  adapter supplies its own code: the same refusal reads
  `VRM_VMC_PACKET_MALFORMED` in one adapter and
  `VRM_VRCHAT_OSC_PACKET_MALFORMED` in the other, which is demonstrated rather
  than promised.

  Its allowed edge set is **empty**, and emptier than `liveTransport`'s in a way
  a reader would not predict: it links no *platform* library either. A transport
  needs a socket and a mutex; a decoder is handed a byte range. It is outside
  the aggregate product for a reason §5 of the workspace contract had not needed
  before — it names no product and opens nothing, so both of that section's
  existing clauses pass, and what keeps it out is that no member of the product
  links it or can.

  `osc_boundaries` reads `tests/` as well as `include/` and `src/`, which
  `liveTransport`'s check does not. A decoder's payloads all need *some*
  address, and the shortest path is to paste one off a real session: the suite
  that moved here had done exactly that, and every sample address was replaced
  on the way at identical length, so the byte offsets it asserts are the same
  numbers they were.

- **`vrmAdapterVrchatOsc` decodes tracker messages** (VRC-2). A known address
  becomes a tracker identity, a channel and three floats
  ([`TrackerMessage.h`](adapters/liveCapture/vrchatOsc/include/vrmAdapterVrchatOsc/TrackerMessage.h));
  an unknown one is `VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS` and the session
  continues. Nothing is converted on the way through: the values are the
  sender's own, in the sender's own space, because a documented basis is a
  hypothesis until a recorded rest pose agrees with it and that is VRC-3's.

  **Four decisions in it come from the recorded session rather than from
  VRChat's specification**, which is what VRC-1 was measured for:

  - **The identity holds a number and a name.** `head` sits in the same path
    position as `1`, `2` and `3`, so a decoder reading that segment as an
    integer drops the head and reports nothing wrong. It is the first line of
    the first test.
  - **`,fff` exactly, where the sibling would count the extra.**
    `vrmAdapterVmc` reads the form it knows and counts arguments past it,
    because VMC's messages grew by appending fields to a leading form that
    stayed what it was. Here the *arity is the meaning*: a rotation is three
    floats and therefore Euler, and a four-float rotation is a quaternion whose
    first three components are not Euler angles. So it is refused, quoting both
    tag strings, rather than half-read.
  - **A bad identity is not an unsupported address.** `0`, `9`, `01` and `hip`
    are `TRACKER_ID_INVALID`; `/avatar/parameters/...` and
    `/tracking/trackers/1/velocity` are `UNSUPPORTED_ADDRESS`. Collapsing the
    two would make a sender's bad index indistinguishable from a part of
    VRChat's surface nobody has implemented yet.
  - **It decodes to a `TrackerMessage`, not to the plan's `TrackerSample`.**
    Position and rotation arrive in separate datagrams, so no single message can
    fill both halves of a sample — and a defaulted rotation of (0, 0, 0) is
    bit-for-bit what a tracker at rest reports, which the reader could not tell
    from a measurement. The sample, the window it is assembled over and
    `TRACKER_PARTIAL` are all VRC-4's, and that code is raised nowhere in this
    change on purpose: a single message is always partial.

  **The generated corpus lands with it** — twelve captures written by
  [`tools/generate_packets.py`](adapters/liveCapture/vrchatOsc/tools/generate_packets.py)
  from the measured shapes, replayed by `vrmAdapterVrchatOsc_trackerCorpus`
  against counts derived from the generator's structure, and re-checked against
  the generator itself by `vrmAdapterVrchatOsc_packetGen`.

  **Exactly one of the twelve is the session's own shape**, and the manifest
  says which per capture rather than leaving it to be inferred: `session` for
  that one, `derived` for the five whose every address and ordering the session
  carried but whose arrangement it did not — one tracker alone, no head, a
  single channel sustained, a permanent dropout — and `unobserved` for the six
  carrying something it never sent at all, each with its reason. A corpus that
  cannot say how far a recording stands behind each of its fixtures is one a
  later reader has to guess about. The recorded half of that corpus still
  carries no bytes, and that is policy rather than a gap.

- **An address inventory for `vrmAdapterVrchatOsc`.** What a recorded session
  actually contains, counted from bytes: one row per address *and type tag
  string*, with message and datagram counts and the span each row covers.
  `vrchat_osc_record --inspect` prints it; the recording path still decodes
  nothing.

  The row key is the pair rather than the address, because a sender that spells
  one address `,fff` in most frames and `,f` in some is a sender a decoder has
  to be built around, and a table keyed on the address alone would average the
  two and hide it. `messages` and `datagrams` are counted separately for the
  same reason: they disagree exactly when a bundle repeats an address.

  It carries **no list of addresses it expects**, which is the property the
  milestone needs rather than a simplification. The risk being tested is that
  mocopi's `VRChat (OSC)` output is not the tracker subset anyone assumes, and
  an inventory that reported absences of expected rows would answer a different
  question. An address nobody predicted appears as a row.

- **`liveTransport`, the live half's shared leaf.** The UDP receiver, the
  opt-in datagram queue, the `<sender>-packet-capture` file format, and the
  diagnostic vehicle every live adapter reports through — one copy, where
  `vrmAdapterVmc` and `vrmAdapterMocopi` had two. Normalised for their vendor
  identifier and stripped of comments, the two `PacketCapture.cpp` differed by
  5 lines out of 366 and the two `UdpReceiver.cpp` by 161, and that second gap
  was the four defects above. Both receivers had named the trigger for turning
  the repetition into a library and named it exactly — a **third** recorder —
  and a third live adapter is what made it arrive.

  Its allowed edge set is **empty**, which is a measurement rather than an
  aspiration: the six files it was extracted from include their own headers and
  the standard library and nothing else. It is outside the aggregate product,
  because no tool in the product opens a transport, which is what makes every
  clip in this repository reproducible by construction. `liveTransport_boundaries`
  checks both halves of that on every build, in source and against a built
  binary, and the binary half needs no OpenUSD allowlist because nothing here
  can drag one in.

  It holds no diagnostic **code**. A code set is frozen per adapter, before its
  decoder exists, so the receiver reports a `TransportEvent` — `BindFailed`,
  `Silence` — and each adapter maps it onto its own frozen code.

- **`vrmAdapterVrchatOsc`, the third live input adapter — its scaffold, its
  frozen diagnostic set and a recorder.** VRChat OSC tracking data in; nothing
  out yet, because this change ships **no semantic decoder**. `vrchat_osc_record`
  turns a sender aimed at this machine into a `vrchat-osc-packet-capture` file
  and reports what a socket can see; `--inspect` reports on a recorded capture
  with no socket at all.

  **It is the first adapter written on the near side of the extraction above,
  and the sizes are the receipt.** The packet-capture format is one magic string
  and four forwarding calls where each sibling carries ~400 lines of it; the UDP
  receiver is a `switch` over two transport events where each sibling carried
  ~550. What is left is precisely what a shared library may not hold — a code
  table, and the map from a transport event to one of its rows. The four
  receiver defects fixed above arrive fixed here rather than copied a third
  time, which is what extracting *before* the third consumer bought.

  **A tracker source is not a pose source.** This wire carries numbered tracker
  observations, which are pre-IK, and a tracker index is not a body role — so
  the adapter will stop at a tracker frame and the humanoid solve is a separate,
  generic boundary. No VRChat-shaped type enters `motionCore` under any outcome.
  Two of the ten frozen `VRM_VRCHAT_OSC_*` codes describe states neither
  sibling's set can express: a tracker that reported half of itself, because
  position and rotation arrive on separate addresses, and a stream that is
  well-formed and unusable because it has not been calibrated.

  **The decoder is absent on purpose, and the published specification is the
  reason rather than an excuse.** A specification says what a *receiver* must
  accept; what a sender sends is a measurement. So the recorder lands first, the
  address inventory is measured from real datagrams next, and the decoder is
  designed from the inventory — the order `vrmAdapterMocopi` was forced into by
  an undocumented grammar, adopted here by choice. Every payload in every test
  is a counting pattern, and the session report deliberately declines to group
  datagrams by address.

  **One edge where the contract permits three**, and it is measurable rather
  than asserted: `motionCore` and `motionRuntime` are what an adapter takes when
  it produces canonical values, and this one produces none, so its test binaries
  import no OpenUSD at all — unlike both siblings, they need no Gf DLL directory
  on `PATH` to run.

  Bytes off the socket and bytes in the capture file are identical, asserted end
  to end through a real socket and a real file against an **independent** capture
  parser written in the test. The corpus directory is created empty, and its two
  CTest names are registered by globbing for a capture rather than for the
  directory, so they arrive with the first fixture instead of failing from today.

### Changed

- **CI is seven cells, not sixteen, and there is no scheduled lane.** The PR
  matrix carried every one of the four bundles on each of three platforms; the
  workspace cells that landed alongside them already run the root CTest suite on
  the same three platforms, and that suite contains each bundle's own tests
  (`vrmschema_plugin`; the six `usdvrm_*` including `usdvrm_baseline`;
  `usdvrmresolver_boundaries` and `usdvrmresolver_python`) — a superset of the
  discovery / one-read / one-open / one-golden a bundle cell's L2–L5 checks.
  `usdVrmaFileFormat` was the sole exception, and only because of the option-name
  bug fixed above; with that fixed, nine bundle cells were measured redundant and
  removed. Three remain, `usdVrmFileFormat` on each platform, and not for the
  pyramid: a bundle cell is the only lane that configures a bundle **standalone**
  (no root CMake tree in scope) and the only PR lane that runs `ost plugin
  package`, whose staging differs per platform. `openstrata.ci.yaml` carries the
  reasoning and what to re-measure before adding cells back.

  The `scheduled` lane is gone with its one cell,
  `usdvrmfileformat-support-windows-cy2026`. It targeted a self-hosted
  `usd-windows-real` runner that does not exist — every weekly firing from
  2026-07-27 onward was cancelled by GitHub after queueing for a runner that
  never claimed it — and it pinned a plugin artifact built against OpenUSD 26.05
  against a 26.08 runtime, which this repository's own note said not to trust.
  `.github/workflows/ost-support-matrix.yml` is deleted; `ost ci generate` no
  longer emits it — and does not notice, which is one of the three asks in
  [report 38](docs/reports/ost/38-2026-08-30-v0.22.8-workspace-cell-verbs-and-orphaned-lanes.md)
  along with the missing workspace-cell verbs that made a sixteen-cell matrix the
  only way to say what two commands say.

- **The silence timeout arrived, on the terms the fix above promised.** The
  shared receiver has one unconditionally; `vrmAdapterMocopi` exposes it and
  `vrmAdapterVmc` does not, because `VRM_VMC_*` still has no code for silence
  and inventing a second spelling of `VRM_MOCOPI_DEVICE_UNAVAILABLE` remains a
  contract change. What changed is that the difference is now one configuration
  field and one `switch` arm rather than thirty lines of receiver present in one
  copy and missing from the other.

- **A packet capture may carry a `device` header key whatever wrote it.** The
  key was `mocopi-packet-capture`'s alone; the two formats now share one header
  vocabulary, so a `vmc-packet-capture` carrying `device` parses instead of
  being refused as an unknown key. The magic line stays per adapter — a capture
  of one protocol handed to the other protocol's decoder should fail at the
  first line rather than at the first field — and no committed fixture changes
  a byte, because the writer emits only the fields a capture actually carries.

- **An OSC `t` argument reads as an unsigned time tag, in its own field.** It
  used to share `h`'s signed 64-bit path, so an NTP time tag arrived as a
  negative `integer` — and NTP seconds have had their high bit set since 1968,
  so that was every time tag any sender emits today rather than a far-future
  edge. `OscArgument` has a `timeTag` now, spelled and typed like the packet's
  own, and `integer` stays at zero for a `t` on the rule the rest of the table
  follows. Found and recorded by the characterisation step that could not change
  behaviour; decided at the extraction, before two adapters could depend on the
  answer. Nothing in VMC or in the VRChat tracker surface sends a `t`.

- **`vrmAdapterVmc` decodes OSC through `osc` and keeps only its code.** 887
  lines leave the adapter and 37 arrive, and what arrives is the part a shared
  library may not hold: the map from *this datagram was not decodable OSC* onto
  `VRM_VMC_PACKET_MALFORMED`. Every public name keeps its spelling —
  `vrmAdapterVmc::OscPacket` and the rest are the same types reached through a
  `using` — and the refusal still arrives with that code, its table's severity
  and recoverability, the offending address as its subject and the byte in its
  detail, which is what the adapter's own suite now checks. The fourteen tests
  that describe the wire format moved with it; the corpus, which reads this
  adapter's capture format over this adapter's fixtures, did not.

- **Both adapters link `liveTransport` and neither links `ws2_32` directly.**
  The platform's transport and threading primitives arrive through that
  library's exported target now. Each adapter's public headers keep every name
  they had: `Diagnostic`, `ReceivedDatagram`, `PacketCapture` and the rest are
  the same types, reached through a `using`. One call site in one VMC test
  needed qualifying, because it had been reaching the adapter by
  argument-dependent lookup on a type that now lives elsewhere.

- **Every runtime pin moved, because the runtimes they named were deleted.**
  `ost` 0.22.3 publishes the CY2026 OpenUSD runtimes as sixteen canonical leaves
  of one declared matrix — 26.05 and 26.08 x `core`/`gl`/`vulkan` per Linux and
  Windows, `core`/`metal` on macOS arm64, each tagged
  `<version>-<variant>-<os>-<arch>` — and the hand-driven runtimes this
  repository pinned from v0.5.0 onward are gone from the registry. Every digest
  the CI contract and `release.yml` carried before this change now resolves to
  `MANIFEST_UNKNOWN`, so this is a re-pin rather than an upgrade anyone chose.
  The three replacements are `26.08-gl-windows-x86_64`, `26.08-gl-linux-x86_64`
  and `26.08-metal-macos-arm64`, on the same OpenUSD source revision as before.

  **The imaging variants are the floor, not a preference.** `core` is built
  `--no-imaging`, and `cmake/UsdVrmOpenUsd.cmake` refuses any runtime without
  `usdExecImaging` — one of the six OpenExec components it probes, and the only
  one under `pxr/usdImaging` — so a `core` leaf cannot configure this workspace
  at all. What the imaging leaves add on top is evidence: their producer
  verified loader, physical device and render, where the three runtimes they
  replace recorded `not-run` for all three. The macOS target id gained its
  deployment target with them (`macos-arm64-macos130-py313`), and its toolchain
  moved to Apple clang 17 on the macOS 15.5 SDK.

- **Both CI lanes bootstrap `ost` 0.22.6.** The generated workflows and the
  hand-authored `release.yml` move together, which they have not always: the
  generated lanes were on 0.21.0 and the release lane pinned it deliberately.

  **0.22.6 rather than 0.22.3, and the three releases in between are the whole
  reason the pin is worth reading.** 0.22.3 published these runtimes and could
  not consume two of them: the macOS leaf failed `ost artifact pull` on a
  wildcard floor constraint its own producer had written (`libcxx >=17.x`), and
  every Windows cell failed `ost runtime validate` with ten checks ok, two
  skipped and zero failures. 0.22.4 fixed both and moved the failure — its new
  Windows and macOS device probing made the render check run for real, and that
  check ran `usdrecord` against whatever `python` was first on `PATH`, which on
  a hosted runner is 3.12 against a py313 runtime. 0.22.5 resolves the
  interpreter from the runtime's own contract, and moved the failure once more:
  `usdrecord` builds its GL context through PySide6/PySide2, which an OpenUSD
  built without `--usdview` does not ship and which no lane can install.
  0.22.6 preflights those imports and reports the check `skip` rather than
  `fail` when they are absent, and stops counting a software rasterizer
  (`GDI Generic` on a GPU-less runner) as a physical device. All four were
  defects in the tool, not in the artifacts: no digest below changed and
  nothing was republished.
  Regeneration also splits the runtime cache into `actions/cache/restore` and
  `actions/cache/save` and drops resumable transfer state before saving, both of
  which are 0.22.3 renderer output rather than edits here.

- **The aggregate product's member set is a declaration now.** `openstrata.toml`
  grew a `[workspace]` table naming all twenty source members explicitly, the
  seven `release_members` the product ships, and the three adapter CLIs in
  `release_exclude`. That closes a hole this repository has documented since
  v0.7.0 and could not fix: WORKSPACE.md §5 says an adapter is never part of the
  aggregate, but under `ost` 0.21.0 that held only because the tool did not
  discover `adapters/<group>/<name>/tools/<tool>/`, and 0.22.x does. Bumping the
  release lane's pin would have silently published ten members. `ost` fails with
  `AGGREGATE_MEMBERSHIP_MISMATCH` before packaging now, and `release.yml`'s
  count against the tree stays beside it, because the declaration is the thing a
  mistaken commit would edit.

  Members are named one per line rather than matched by `plugins/*`: a wildcard
  selects directories, a directory without a descriptor is a hard error, and one
  `tools/__pycache__` left by a test run was enough to make the whole graph
  refuse to resolve.

- **`scripts/check_docs.py` skips generated trees.** It walked
  `.strata/` and `dist/`, which was harmless while nothing there was a Markdown
  file; product staging puts a copy of `profiles/motion/README.md` under
  `.strata/` and every one of that README's five repository-relative links then
  resolved from the wrong directory. Nothing in a generated tree is authored, so
  nothing in one is checked.

- **The motion profiles reach the product as product-owned data.**
  `[[workspace.install_data]]` maps `profiles/motion` to
  `share/usd-vrm-plugins/profiles/motion` in the aggregate, installed once and
  digest-verified before install. v0.7.0 shipped with this recorded as a known
  limitation — no member archive carried data and none could, because a member
  descriptor's `directories:` names subdirectories of the *member* root and
  these profiles are owned by the workspace, so the only way to ship them was to
  copy the layer's data under one tool's directory. A packaged
  `motion_bvh_convert` that finds no profile refuses every BVH file it is given,
  which is why one of that release's conditions was left open rather than
  worked around.

## [0.7.0] — 2026-08-24

### Added

- **A `.vrma` clip's expressions now reach the stage.** VRMA declares an
  expression under `expressions.preset` or `expressions.custom` and animates its
  weight as the **X component of a node's translation**; `usdVrmaFileFormat`
  read neither, so a face-capture clip imported as a body and nothing else. Each
  declared expression is now a prim under `/Animation/Expressions` carrying
  `vrm:expressionName` (the name the file used, verbatim), `vrm:expressionType`,
  and a time-sampled `vrm:expressionWeight`. The weights ride on
  `HumanoidPose::expressions`, so a clip and a VMC sender now produce the same
  value type ([MOTION_CONTRACT.md](docs/design/MOTION_CONTRACT.md#expression-semantics-v070)).

  **Three of those behaviours are decisions, not details.** A weight outside
  `[0, 1]` is carried unclamped with a `VRMA109` warning, where the
  specification would clamp it — a file that said `1.5` said `1.5`, and
  correcting it in the reader would hide the authoring tool from whoever reads
  the clip; the clamp belongs to whoever applies the weight to a rig. An
  expression the clip declares and never drives is read from its node instead:
  glTF leaves an un-animated node at its own TRS, so a node that states a
  transform states a constant weight, authored as a default value — while a node
  that states none gets a prim with **no** weight attribute, because an
  unreported weight is not a weight of zero. What separates those two is what
  the file wrote, not whether the number is zero. And nothing is expanded: a VRM expression drives N morph targets across M meshes plus
  material colours, which is the *avatar's* property, so no `blendShapes`
  binding is authored and `ExpressionResolve` stays ahead
  ([motion policy](docs/design/MOTION_ARCHITECTURE_POLICY.md) §4.3).

  Expression key times join the same union every other channel is evaluated at,
  so a clip whose face keys off the body's beats gains samples on the body too
  rather than having its face resampled. The attributes are namespaced rather
  than a typed schema, which leaves the `VrmAnimationExpressionAPI` ownership
  question open at no cost: such a schema applies to exactly these prims with
  exactly these attribute names. The prim *name* is not a join key yet — the VRM
  importer sanitizes through its own private table, so a non-ASCII name lands
  differently on each side, and the avatar side does not author
  `vrm:expressionName`; that is the first thing `ExpressionResolve` has to
  settle. Verified by `expressive_face.vrma`, a generated fixture with one case
  per behaviour above — all seven `VRMA_MotionPack` clips
  carry `humanBones` and no `expressions`, so there was no vendor file to read
  this against.

- **Unlit VRM materials now carry a MaterialX network, and it is the one that
  renders.** Each `KHR_materials_unlit` material gains a second realization at
  `/Asset/mtl/<name>/mtlx`, reached through `outputs:mtlx:surface`
  ([material policy](docs/design/MATERIAL_ARCHITECTURE_POLICY.md) §5.2, Product
  P5 Step 2). It is generated from the source material, never by translating the
  PreviewSurface graph, and it keeps glTF's semantics rather than approximating
  them: base colour is `factor * texture` decoded from sRGB, and alpha coverage
  is stated as `alpha_mode` / `alpha_cutoff` so MASK is a cutout the renderer
  performs instead of a comparison emulated inside the graph.

  **This changes what you see.** A renderer picks one terminal, and Storm asks
  for `mtlx` before the universal one, so `usdview` now draws the MaterialX
  network and `/preview` becomes the path for consumers that do not speak
  MaterialX. The visible difference on a real avatar is alpha: hair strands and
  eyelashes that `/preview` draws as opaque quads — the flat blocks across the
  forehead in issue #119 — composite correctly.

  The terminal is `gltf_pbr` with its lit response zeroed rather than
  MaterialX's own `surface_unlit`, which reads backwards until you try the
  alternatives on the pinned runtime: on OpenUSD 26.08, `surface_unlit` and
  `convert_color4_surfaceshader` fail to compile in hdSt (their generated GLSL
  references undeclared `u_env*` uniforms) and a bare `surface` with an EDF and
  no BSDF renders but ignores `opacity` entirely, which would put VRM hair back
  to solid. There is no fallback to catch any of that — a material whose
  MaterialX terminal cannot be built does not revert to `/preview`, it draws as
  a flat grey default surface — so the whole table of what does and does not
  work is recorded in material policy §5.2.1, along with the note to revisit it
  when the runtime moves.

  Lit materials are unchanged and keep `/preview` alone; they are the follow-up
  half of Step 2. Apart from the two corrections listed under Fixed, nothing
  `/preview` already produced moved or changed value: across all 28 baseline
  inputs the diff is additive, verified mechanically rather than by eye.

### Fixed

- **A capture frame that reported no root sent the body back to where the
  session started.** `motion_capture` has to author a hips translation at every
  time sample, and it authored the *rest* — the session's first observed root
  position — for a frame whose root was absent. That is correct for a clip where
  no frame reports a root and wrong the moment one does: a single rootless frame
  between two that travelled teleported the avatar to the session's origin and
  back, in one frame. It now holds the last placement it authored.

  A missing root is not a missing bone, and the two fallbacks are not
  symmetric — which is why the rotation beside it still authors rest rather than
  holding. An unobserved bone has a neutral value, so the rest rotation states an
  absence; a root position has none, so the rest translation states a trip that
  never happened.

  Reachable from either live adapter — a VMC frame closes with bones and no
  `/VMC/Ext/Root/Pos` — and invisible until now because no live path composed a
  root at all, which left the substituted value at the origin and equal to every
  other frame's.

- **`KHR_texture_transform` was authored as though USD sampled glTF's UVs.**
  Two changes of variable were missing, and both are invisible on an identity
  transform — which is every transform in the vendored corpus, so no amount of
  regenerating baselines would have shown it. The importer flips V when it reads
  UVs, so a transform glTF states against its own top-left-origin coordinates
  has to be conjugated by that flip before it applies to `st`; and glTF states
  the rotation in **radians** while both `UsdTransform2d.rotation` and
  MaterialX's `place2d.rotate` are declared in **degrees**, so a 90-degree
  rotation was being authored as 1.57 degrees — visually unrotated.

  Both realizations now derive one affine map in `st` space and each spells it
  in its own vocabulary, which is not the same spelling twice:
  `UsdTransform2d` multiplies by its scale, adds its translation and negates its
  rotation, while `place2d` divides, subtracts and does not negate. Passing the
  glTF triple to both — which is what the MaterialX side did when it first
  landed — makes them sample different regions of the same texture. A fixture
  with every term non-identity now pins both against glTF's own matrix.

- **`alphaMode: OPAQUE` no longer lets the base-colour factor's alpha through.**
  glTF is explicit that OPAQUE ignores alpha entirely, so a material with
  `baseColorFactor[3] = 0.6` is opaque, not 40% transparent.
  `UsdPreviewSurface.opacity` was taking the factor unconditionally. MaterialX's
  `gltf_pbr` enforces the rule inside its own graph, so leaving this would have
  made the two realizations disagree about the same source material — visible in
  the baseline as `textures.vrm`'s `Skin` going from `0.6` to `1.0`.

### Changed

- **A mocopi session now travels.** The root and hips question v0.7.0 owed is
  answered and written down as
  [the motion contract's own section](docs/design/MOTION_CONTRACT.md#root-and-hips-v070):
  a hips translation that is a rig's only translating joint **is** body
  translation, so it reaches `RootMotion::worldPosition` as an absolute position
  in the source's own space, with the rotation at that joint as the root's
  orientation.

  `vrmAdapterMocopi` composes it in `MocopiFrameAssembler`, under the new
  explicit `BodyPlacementPolicy` whose default is `HipsOnly` — the only one of
  the four policies this protocol can express, because the device sends no
  second root channel to compose with. `BodyPlacementPolicy::None` is the
  previous behaviour and stays reachable.

  **What changes for a consumer.** A trace exported by `mocopi_record
  --export-trace` now carries `root pos` and `root rot` on every frame, and a
  clip retargeted from one moves the avatar instead of animating it in place: a
  36-second device session was measured dropping **4.81 m** of hips path
  ([report 01](docs/reports/motion/01-2026-08-15-mocopi-cross-source.md)), and
  that is the travel that now arrives. Nothing downstream changed to accept it —
  `motion_capture` already seeds the hips rest from the session's first root
  position, so what reaches an avatar is a delta, and `motion_retarget` takes
  the identical command line. `RootMotionIntake` also stops being inert on this
  path: its three settings previously selected between three identical outcomes.

  The tool's hips-path line changed its verb rather than disappearing — it now
  reports what the trace **carries** rather than what it drops, so an export from
  either side of this record reports one quantity.

  **The VMC half stays open, and says why.** `vrmAdapterVmc` still reaches both
  `/VMC/Ext/Root/Pos` and the hips local position and still composes neither: a
  sender's convention is a measurement, no real VMC sender has been recorded, and
  applying the native answer by analogy would synthesise a value from a guess
  about a product. A VMC session therefore still retargets in place, which the
  record states as a cost rather than leaving to be discovered.

- **A material is no longer a pile of shader nodes.** The UsdPreviewSurface
  network moved from the material's immediate children into a `/preview`
  `UsdShadeNodeGraph`, so `/Asset/mtl/Hair` now reads as one child in `usdview`
  instead of eight, and the surface terminal runs material → graph → shader
  rather than material → shader
  ([material policy](docs/design/MATERIAL_ARCHITECTURE_POLICY.md) §4, Product P5
  Step 1). Material **bindings** are unchanged: they still target
  `/Asset/mtl/<name>` and nothing below it.

  Consumers that hard-code `/Asset/mtl/<name>/Surface` must change — the surface
  shader is at `/Asset/mtl/<name>/preview/surface`, and textures at
  `/Asset/mtl/<name>/preview/<slot>Texture` — but consumers that ask
  `UsdShadeMaterial` for its terminal (`ComputeSurfaceSource()`) need no change,
  which is why the schema contract stays at v1: no contract path moved and no
  property changed meaning. The
  [contract](plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) now says outright that
  the shader network below a material was never contract, rather than leaving it
  to be inferred from a path table that never mentioned it.

  Nothing about the rendered result changed, and that claim is mechanical rather
  than reviewed by eye: the committed baseline digests, with the path rename
  applied and no other edit, equal the regenerated ones for all 28 inputs —
  including both vendored corpus avatars, 17 and 13 materials. 44 baseline
  artifacts moved paths; none moved a value.

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

- **The mocopi adapter has a socket, and it arrives first rather than last —
  which is a finding about the protocol, not a shortcut.** `vrmAdapterMocopi`
  grows a thin UDP receiver: a bound socket, a monotonic receive clock, a size
  limit, and no decoding whatsoever. Every datagram is handed back exactly as it
  arrived, including the ones a decoder would refuse, because a receiver that
  filtered its own input would make a corpus a description of what the receiver
  let through rather than of what a source sent.

  The plan had the transport last, for the reason its sibling did: every layer
  below it then runs from committed bytes. That order needs a premise this
  protocol does not supply. The VMC Protocol is published, so a corpus could be
  *written* before a socket was opened; this one is documented only as far as its
  transport, so there is nothing to write a corpus from and exactly one way to
  obtain one without guessing — receive it from something that already speaks the
  format. The transport is therefore not the last layer that needs writing but
  the only one that *can* be, and the rule it appears to break is the rule that
  sent it here: "the fixture-driven tests stay deterministic" is a statement
  about the layers that decode, and this one decodes nothing. It costs nothing
  the original order was protecting either — `vrmAdapterMocopi_udpReceiver` needs
  no device, no sender application and no fixture, binds loopback on an
  OS-assigned port, and never touches 12351.

  Two of the nine frozen diagnostics are raised here and no others, which is the
  first time that freeze has been paid rather than asserted.
  `VRM_MOCOPI_SOCKET_BIND_FAILED` is the one transport failure a session cannot
  continue past; a lost datagram, a transient platform error and an empty poll
  are counts and not codes. `VRM_MOCOPI_DEVICE_UNAVAILABLE` is the code the
  sibling's set does not have, and a socket is the only layer that can raise it —
  silence is invisible above, where an absence of datagrams reads as an absence
  of work. The **threshold is stated by the caller and has no default**, because
  a device being strapped on and a device switched off produce the same nothing;
  it is reported once per episode, re-armed by the next datagram, and counted
  even when the caller passed nowhere to put the message.

  `ws2_32` joins `tests/check_boundaries.py`'s allowlist with the change that
  needs it rather than having been reserved for it, and the gate is measured
  rather than assumed: widened by that one name it still refuses
  `Threads::Threads` — which stays out precisely because this adapter has no
  datagram queue and "a receiver usually needs one" is the reservation the
  allowlist exists to catch. The workspace graph gate reports the same
  `6 libraries, 9 library edge(s), valid` as before, as it does for any change
  inside an adapter.

  The new test was watched failing, and the first failure was its own: with the
  silence re-arm deleted from `Receive` the suite **hung** rather than failed,
  because the polling loops were unbounded. They are bounded on the receiver's
  own clock now, so the same defect fails in two seconds naming the line — a
  test whose failure mode is a hang reports "timed out" for every cause there is.
  76/76 ctest.

  **A review of that receiver then found six defects, and four of them the
  sibling adapter has identically** — because they were copied along with
  everything else, which turns the "a third recorder is what makes this a
  library" trigger from an argument into a measurement. All six are fixed here;
  the sibling's copies are **not** touched by this change and are recorded as
  outstanding.

  * **An over-long datagram was handed back as whole on POSIX.** The buffer was
    exactly `MaxDatagramBytes`, and `recvfrom` truncates *silently* there — a
    short read and a whole datagram are the same return value. A recorder would
    have written a packet the source never sent into a fixture and blamed the
    source. The buffer is now one byte larger so an overrun comes back as
    `MaxDatagramBytes + 1`, is counted, and is dropped. This is the worst of the
    six: it corrupts the corpus this whole track exists to record, and it fails
    plausibly rather than loudly.
  * **A large *finite* timeout became an infinite wait.** Milliseconds above
    `INT_MAX` were mapped to `-1`, which is the value both `poll` and `WSAPoll`
    read as "block forever" — the exact opposite of the bound the caller asked
    for, with no other way to stop. Clamped to `INT_MAX` now. No test
    distinguishes the two (both wait longer than any suite may run), so this one
    is verified by reading and said so rather than claimed.
  * **A poll wake-up's `revents` was never inspected.** `POLLERR`/`POLLHUP`/
    `POLLNVAL` are reported whether or not they were asked for, so a ready
    descriptor with no datagram sent the loop back to a poll that was still
    ready — a 100% CPU spin under the documented indefinite wait, in the exact
    place a comment asserted a spin was impossible.
  * **Idle accounting reached the error paths.** A call that met a truncated
    datagram or an ICMP report was counted as idle *and* checked for silence, so
    a session log could say "the source stopped sending" in the same breath as
    counting what it sent.
  * **`Open` restarted the clock but not the stats** (mocopi only). A re-`Open`
    measured silence from a time stamped in the previous epoch and went blind
    for as long as the previous session had run — on a reconnect, which is the
    one moment the code exists for. The silence reference is now two explicit
    members rather than a reading of a tally a caller may zero.
  * **`ResetStats` cleared the tally but not the episode** (mocopi only). A new
    counting window over a device that was off the whole time would have ended
    reporting `silenceReports == 0`.

  Three of the six now have tests that fail without them, and the truncation one
  is on **its own CTest name with `SKIP_RETURN_CODE`** rather than inside the
  suite — because it needs IPv6 to be reachable at all, and a skip inside the
  suite is invisible: ctest hides a passing test's output, so a lane that
  checked the claim and a lane that quietly declined print the same line.
  Reported as `Skipped`, the answer is in every lane's log. It also says where
  it *can* fail, since Windows catches that case through `WSAEMSGSIZE` with or
  without the fix and only a POSIX lane exercises the spare byte. Also
  corrected: the installed package config still claimed this library had no
  transport, in the very file whose previous comment had promised the receiver's
  change would update it. 77/77 ctest.

  The sibling's four copies are filed as
  [#112](https://github.com/animu-sphere/usd-vrm-plugins/issues/112), and the
  standalone build this change did not re-run as
  [#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113).

- **Both recorded paths reach an avatar somebody actually made, and the real one
  found what the fixtures could not.** The v0.7.0 release conditions ask for a
  *real VRM avatar* on both halves, and both now have one: the recorded path in
  `workspace_real_avatar_bake` (a new workspace test) and the live path in the
  VMC adapter's own `vmc_record_endToEnd`, each baking onto `Seed-san.vrm` — a
  committed, redistributable VRM 1.0 sample. *Seed-san model by VirtualCast,
  Inc. — VRM Public License 1.0.* The hand-authored fixtures stay beside them
  rather than being replaced: a rig shaped so a broken rest-pose correction
  cannot pass it and a rig somebody shipped are different tests, and neither
  substitutes.

  What a released model gives that a fixture cannot is three things it was never
  designed to give. **128 joints of which the humanoid binds 51** — the rest are
  hair, a backpack, ropes and heels, so "the right joints moved" stops meaning
  "three moved" and starts meaning "three moved and the hair did not". **Two
  unbound joints between bound ones** (`forearm_twist_L/R`), so the hand's
  rotation has to arrive *through* a joint the clip knows nothing about, which
  is the target-side of the path rule the converter answered on the source side.
  And **an incomplete humanoid**: VRM 1.0 makes `upperChest` optional, this model
  leaves it out, and the mocopi profile maps a source joint to one.

  That last one is a finding, and it is exact rather than approximate. The
  rotation is **dropped whole rather than redistributed**: every bone below it
  reproduces the source computed as if the source's `upperChest` had never
  moved, agreeing with that prediction to 6e-7 degrees, and costing 11.2° at
  worst on the recorded session — the same figure at the neck, the head and both
  arms. `motion_retarget` already named the bone on stderr rather than losing it
  in silence. Whether dropping it is *right* is a contract question with two
  candidate answers and one avatar behind it, so it is raised in
  [`docs/roadmap/recorded-motion-sources.md`](docs/roadmap/recorded-motion-sources.md)
  §10 and pinned here as a characterisation test, not decided.

  Neither test adds an edge. The root one names no adapter, which is why the
  live half lives with the adapter instead; the adapter one links nothing new
  and passes `motion_retarget` the identical command line, the only difference
  being that `usdVrmFileFormat` is on the plugin registry path when it runs — so
  a build tree without the importer skips the real-avatar leg rather than
  failing it. The rig-reading machinery both workspace tests share moved to
  `tests/motion/rigcheck.py`, because two copies of a quaternion chain walk are
  how a test starts agreeing with the defect it is meant to catch.

- **`motion_bvh_convert`: a recorded file, an explicitly named profile, and the
  same avatar-independent semantic clip `motion_retarget` already consumes.** It
  is the first program anywhere that holds a reader and a profile at once, which
  neither library may, and three things follow from that and land here. *The six
  semantic diagnostics are raised by this caller* — `MatchSourceProfile` returns
  a typed refusal naming the event and the CLI maps it onto the reader's frozen
  codes, deliberately not one-to-one, because an ambiguous joint name is a
  profile mismatch and has no code of its own. *There is no default profile and
  no fallback* — a missing `--profile` is `VRM_BVH_PROFILE_REQUIRED` and stops
  the run, and a profile **id** resolves to a file through a search path rooted
  at the executable, so the id a conversion records is the id the file states
  rather than whatever the file was renamed to. *Exit status splits on whose
  input was wrong*: 1 the recording, 2 the command or something it named.
  The report separates what was lost from what was not — a rig restating its
  rest geometry every frame lost nothing, a rig whose elbow translates lost
  motion, and one word for both would hide the second inside the first.

- **The semantic clip's third caller, and the decision to keep it a repeated
  shape.** `motion_capture`, `usdVrmaFileFormat` and now `motion_bvh_convert`
  all author the clip. They are not merged, because they differ in the field
  that matters most: a capture stream reports rotations relative to the humanoid
  rest and authors **identity** rests, while a recorded file states a rest and a
  profile says how to read it, so this writer authors a **real** one — which is
  what makes `vrmRetarget`'s source-rest-to-target-rest correction do anything
  at all. What is pinned by test rather than by intention is the part that is
  not a choice: the joint set in `HumanBone` order, frame time codes, and
  `scales` authored — `UsdSkel` fetches translations, rotations and scales as a
  unit and `scales` has no schema fallback, so a clip missing it does not
  animate unscaled, it silently resolves to the rest pose.

- **The recorded path end to end, onto a rig, through an unchanged
  `motion_retarget`.** `workspace_bvh_end_to_end` drives the committed export
  through both tools with the same flags a `.vrma` bake uses, and checks the
  invariant the rest-pose correction exists to hold: a bone's world rotation
  *away from its own rest* is the same on both rigs. The fixture avatar is built
  so a broken bake cannot pass — its joints are named as a DCC names them so
  nothing binds by coincidence, its proportions differ, and its arms rest 45°
  down where the recorded rig's are straight, which makes the correction a real
  rotation for four joints and identity elsewhere. Forcing the rest to identity
  fails it on exactly those four by exactly 45°. It also checks that the *set* of
  bones that move is preserved (this export never rotates its toes), that root
  motion arrives as a displacement from the source's rest over the target's own
  hips height, and that both tools are deterministic.

- **A recording plus what its producer meant becomes canonical humanoid motion.**
  `CanonicalConversion.h` is the last crossing `motionSource` has a reason to
  grant, and four of its decisions are worth reading before a converted clip is.
  *The change of basis is one signed permutation and handedness is its
  determinant* — +1 where the change is a rotation, -1 where a left-handed source
  has to be mirrored — so a position is `M v` and a rotation is
  `(w, det(M) · M v)`. That one sign also settles the angle convention: Euler
  angles are composed by the right-hand rule **always**, out of the raw numbers,
  and a left-handed source's positive angle comes out negated in canonical space
  because that is what a left-hand-rule rotation *is* once mirrored. Handling
  handedness in both places produces a body correct in every axis-aligned test
  pose and wrong the moment anything turns, which is why the rotation half is
  checked physically — a direction is rotated and the answer compared against
  where it has to end up.

  *A bound bone's local rotation is the composition of the path above it*
  ([roadmap §10](docs/roadmap/recorded-motion-sources.md), written down before the
  converter existed). A profile maps a rig onto a humanoid with fewer joints, and
  a joint between two mapped ones is on the path between them — taking a mapped
  joint's rotation verbatim would lose every rotation above it and place the arms
  and head wrong, which reads as a subtly misassembled body rather than as a
  failure. *The rest pose is built by the same walk*, from whichever rotations
  the profile calls the rest, because a second traversal is one that can disagree
  with the first. And *a quaternion track is refused with a reason*: nothing in
  the tree writes that form, so converting it would mean testing a path against a
  value this repository invented — the roadmap's other honest answer, a fixture
  said to be synthetic, stays open.

  `MOTION_CONTRACT.md` gains the two sections this forced. The **canonical basis
  is stated** — right-handed, +Y up, +Z forward, metres — because a converter is
  the first code here that has to map a profile's `forwardAxis` onto canonical
  rather than inherit it. The forward axis is a *recording* of an existing fact:
  the VMC adapter's conversion already leaves a +Z-forward sender's forward
  untouched, and the avatars this is retargeted onto face +Z by specification.
  The second section carries the path rule, the rest pose and the two root
  policies, including why `CanonicalRestPose` is not a field of
  `motion::HumanoidAnimation` and carries no parent array.

  A test that holds a reader and a profile at once — which neither library may —
  drives the one committed real export the whole way to canonical motion against
  the shipped profile written from it, and every expected number in it was
  measured out of the `.bvh` text rather than read back out of the conversion:
  27 joints, 853 frames, the hips at the root's own 0.959893 m, four bones that
  absorbed a chain, 26 of 26 non-root joints restating their rest geometry and
  nothing lost by dropping them, and the session's turn carried on the hips
  within ten degrees of what the root's own channel states.

- **A BVH document becomes source values, and the reader takes its one declared
  edge.** `BvhExtract.h` is the first code in `motionBvh` that produces anything
  a converter can use, and it is as much about what does not cross the boundary
  as about what does: a channel set becomes a track, and nothing about a unit, an
  axis, a handedness or a bone travels with it. Three of its answers are
  decisions rather than mechanics — position channels **state** a joint's local
  translation rather than adding to its `OFFSET` (read additively, a rig stands
  at twice its own height with every bone twice its length, in a file nothing is
  wrong with); a component a joint did not animate falls back to the `OFFSET`
  rather than to zero; and the Euler order is the *relative* order of the
  rotation channels, whatever position channels sit between them. Three shapes
  BVH permits and the neutral value model does not are refused rather than
  reinterpreted: two rotation channels, one axis declared twice, and a chain that
  both ends and continues.

  `VRM_BVH_INVALID_ROTATION_ORDER` is granted to the extractor by name in
  `motionBvh_boundaries` — one file, one code, in review. It sits in the frozen
  set's semantic half because most of what it can mean is, but the half the
  extractor meets needs no profile at all: a joint declaring two rotation
  channels forms no Euler order whoever wrote the file.

  **The edge cost `motionBvh` the binary half of its boundary check, and finding
  that out cost a red macOS lane.** The check inspected a built artifact and
  refused any OpenUSD import; with the edge reaching `motionCore`'s `Gf` value
  types, what it reports is the *linker's* answer rather than the library's —
  MSVC pulls only the archive members something references, GNU ld with
  `--as-needed` drops the resulting unused entries, and Apple's ld64 records
  every library on the link line whether or not a symbol is used. All three are
  correct about their own artifact, so one source tree produces two answers and
  the check was measuring a toolchain. It is removed rather than narrowed, with
  the measurement written down where it was made; the source rule — no OpenUSD
  name in any file here, extractor included — is platform independent and is what
  carries the claim. `tools/motionBvh` gains the same correction: a standalone
  configure of it now needs OpenUSD on the prefix path, because
  `find_package(motionBvh)` resolves `motionSource` and through it `pxr`, and its
  README said the opposite.

- **A profile is a file now, and the first producer is described by one.**
  `SourceProfileFile.h` settles the keys the profile sketch left open and reads
  them: a stated subset of the shape the plan already wrote, with an unknown key
  refused rather than dropped. That refusal is the point of the whole reader — a
  misspelled `requred:` a permissive parser ignored would unbind a joint the
  profile called mandatory and report nothing, which is the near-miss failure
  [§3.1](docs/roadmap/recorded-motion-sources.md) forbids arriving through a
  typo. Two more properties are decisions: every convention's `unspecified` is
  refused *where it is written*, so "there is no default profile" holds inside a
  file as well as between files; and parsing ends in `ValidateSourceProfile`, so
  no half-built profile reaches a caller who would have to re-prove it. One key
  changed on the way in — `units` became `translationUnit`, singular and saying
  which, because a profile states no angle unit at all: a format says whether its
  angles are degrees or radians and a producer does not get to disagree with its
  own format about that.

  **Written rather than borrowed, and checked against a borrowed one.** A
  document parser hands an unknown key back like any other, so the strict half
  would be written either way and it is the half where the risk is; and YAML's
  implicit typing would read a joint named `on`, `y` or `null` as something other
  than the writer's word for it. Against that, `motionSource` links exactly one
  thing, and spending that on a configuration file would be a contract change.
  What the subset owes in return is that it never disagrees with YAML *silently*
  — so every shipped profile is read a second time with a real implementation
  where one is installed, and the two readings are compared. It refuses anchors,
  tags, block scalars and nested flow forms and says so; what it accepts, it
  reads the way YAML does.

  Beside it, `profiles/motion/` and the first profile in it, written from the one
  export this pipeline has measured. What the file settles and what the profile
  decides are separated in both directions: the export states a basis, a unit, a
  root whose samples are absolute positions, and seven torso segments where the
  canonical humanoid has three — and the profile is where "the root *is* the
  hips" and "`torso_7` is `upperChest`" are *decided*, with the two middle
  choices placed by rest height and said to be judgements. It maps 22 of 27
  joints, names the 5 it ignores, and refuses a rig carrying anything else,
  which only an ignore list makes affordable. One producer and not two, on
  purpose: a second profile written from a file nobody has read would be the
  failure this plan is shaped around wearing the shape of progress.

  Two checks, because the interesting claim is not the one a library can make.
  `motionSource_shippedProfiles` reads every file in `profiles/motion/` with the
  library that defines what a profile is, so a profile added there cannot be
  unloadable and unnoticed until a conversion asks for it.
  `scripts/check_motion_profiles.py` checks the claim that a profile *describes a
  rig* — root, every mapped and ignored joint, no joint left neither, and the
  hierarchy the mapping implies — against the recorded file that names it in the
  corpus manifest. It reads both sides itself rather than calling into either
  library, and it lives in `scripts/` because it is the one check that holds a
  reader's file and a profile at once: `motionSource` may not know a reader
  exists and `motionBvh` may not know a producer does
  ([WORKSPACE.md §2](docs/architecture/WORKSPACE.md)), so a caller is what may
  hold both, and until `motion_bvh_convert` exists a script is the caller.

- **The source profile contract — what one producer's export means, declared
  where no code has a name for it.** `motionSource` grows `SourceProfile`: the
  vocabulary a profile file states by name (handedness, a **signed** up and
  forward axis, a translation unit, a root translation and rotation policy, a
  rest-pose source, an unmapped-joint policy), the joint map whose right-hand
  side is a `motion::HumanBone`, `ValidateSourceProfile`, and
  `MatchSourceProfile` for matching one against a rig before a frame is read.
  Three decisions are the substance of it. **Every convention has an
  `Unspecified` and validation refuses it**, so a default-constructed profile is
  invalid by construction — "there is no default profile"
  ([recorded-motion-sources.md §3.1](docs/roadmap/recorded-motion-sources.md))
  said somewhere it can be checked, because a silently-assumed handedness
  produces motion that is subtly misassembled rather than absent, which is worse
  than a refusal because it looks like a result. **A joint map is a hierarchy
  embedding, not a name lookup**: each bound bone's nearest bound humanoid
  ancestor has to be a source ancestor too, which is what catches the near-miss
  profile where every name matched and the body is assembled wrong. And **a
  match returns facts, never a score** — which bones bound, which required ones
  did not, which joints nothing maps, which names are ambiguous, all of them
  filled whatever the refusal, because the caller that most needs them is a
  detector reporting on candidates that did *not* match. A confidence is that
  detector's arithmetic over the counts: a weighting fitted to the exports on
  hand today would be a producer's answer reaching the format-neutral layer
  through a float instead of through an `if`. The one count that arithmetic
  cannot derive is on the match itself — `BoundRequiredCount()`, because a
  required mapping whose name the rig repeats binds nothing *and* is not
  missing, so subtracting the missing ones overstates what matched by exactly
  the figure the sketched candidate report prints.

- **The six semantic diagnostics now have a layer that can raise them, and it is
  not this one.** The question was open against the profile contract as the
  change that had to choose. `MatchSourceProfile` returns a typed
  `SourceProfileRefusal` naming the *event* in terms no format supplies, and the
  caller holding both a reader and a profile maps it onto that reader's codes.
  The two rejected answers are recorded with it: plain text alone would make
  that caller parse prose to pick a code, and a second `VRM_MOTION_SOURCE_*`
  namespace would give one event two spellings and duplicate a set whose whole
  value is being frozen and closed. A reader's `VRM_*` string appearing in
  `motionSource` is the dependency reversal
  ([WORKSPACE.md §2](docs/architecture/WORKSPACE.md)) however it got there.

- **`vmc_record --export-trace` — a VMC session reaches the product as a file,
  not as a link.** Milestone C's first item read "`motion_capture` accepts a
  live VMC source" until that edge was costed. `motion_capture` is a member of
  the aggregate product and every adapter is excluded from it
  ([`docs/architecture/WORKSPACE.md`](docs/architecture/WORKSPACE.md) §5), so
  the edge would have carried a protocol decoder, its network code and a product
  name into the product artifact — once per adapter, since `--source vmc`
  invites `--source mocopi` behind it — and put a transport inside the one tool
  whose reproducibility argument is that nothing in the intake path opens one.
  The release was already asking for the other answer: v0.7.0 requires that a
  session reach an avatar through **unchanged** `motion_capture` and
  `motion_retarget`.

  So the hand-off is the file that format was defined for. A
  `motion-capture-trace` holds "what an adapter delivered, after protocol decode
  and coordinate conversion, before any intake policy", which describes a
  `VmcFrame` exactly, and `motion_capture` replays one knowing nothing about
  VMC. §2 gains no edge. **One trace is one session**: a recording the sender
  restarted during holds two whose clocks overlap, and a spliced trace would
  replay as a session that stalls at the discontinuity, so the export is refused
  until `--sender-session` names one.

  `--max-frames` (default 200000) bounds what the export holds, because
  `--max-datagrams` cannot: a pose is 1320 bytes, a bundled sender emits one
  frame per datagram, and a million of them would be 1.26 GB against a datagram
  bound sized for 150 MB. Two accumulations, two units, two bounds.

  `vmc_record_endToEnd` drives a committed capture through both product tools
  onto a rig and checks the result through a `UsdSkelSkeletonQuery` **by joint
  name** — the three joints the session drove are the three UsdSkel resolves as
  moving. It lives with the adapter: a product test that spawned `vmc_record`
  would be the dependency this arrangement avoids, in a test directory instead
  of a link line.

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

### Known issues

- **A packaged product carries no producer profiles, so a converter unpacked
  from one refuses every file it is given.** Measured, not suspected, and
  re-measured against `ost` 0.22.2 during release preparation: the `motion_bvh`
  member of a `--workspace --product` archive is exactly
  `bin/motion_bvh_inspect.exe`, `bin/motion_bvh_convert.exe` and
  `openstrata.tool.yaml`, and the packaged converter refuses a real capture by
  naming the two directories it looked in — the first of which is the
  `share/usd-vrm-plugins/profiles/motion/` an installed prefix would have. A
  plain `cmake --install` places them correctly and is unaffected. This is why
  *"both paths run from release artifacts alone, profiles included"* is recorded
  as unmet in [v0.7.0](docs/releases/v0.7.0.md) rather than worked around.

  **The workaround exists and was rejected on ownership.**
  `directories: [bin, share]` does stage a member-root `share/` tree into the
  archive — measured, then reverted — but `directories:` names subdirectories of
  the *member root*, so it only works by copying `profiles/motion/` under
  `tools/motionBvh/`. The profiles are the layer's data, not that tool's: a
  second consumer means a second copy, and the copy that ships stops being the
  file `scripts/check_motion_profiles.py` validates. The ask is a data-only
  member, filed as
  [ost report 35](docs/reports/ost/35-2026-08-24-v0.22.2-release-artifact-membership.md)
  §4. A `--profile-dir` flag does not close it either — "works if you pass a
  flag naming a directory the artifact does not contain" is not an
  artifact-only smoke.

- **Whether the aggregate product contains the adapters' CLIs depends on the
  `ost` version, and nothing in this repository can state the answer.**
  `ost` 0.22.2 discovers a tool descriptor under
  `adapters/<group>/<name>/tools/<tool>/`; `ost` 0.21.0, which the release lane
  pins, does not. So the same `--workspace --product` command produces **9**
  members on a 0.22.2 workstation and **7** on the release runners, with no
  descriptor changing in between —
  [WORKSPACE.md §5](docs/architecture/WORKSPACE.md) requires the 7. **v0.7.0
  ships the 7**, and the release lane now counts the `tools/` descriptors and
  fails if packaging exceeds them, so the next pin bump is a decision rather
  than a silent shipment. Nothing in `openstrata.tool.yaml` or
  `openstrata.toml` can say "not a product member"; the ask is
  [ost report 35](docs/reports/ost/35-2026-08-24-v0.22.2-release-artifact-membership.md)
  §3.

- **A root-scope test guarded on `Python3_Interpreter_FOUND` after
  `find_package(pxr)` silently never registers.** `pxrConfig.cmake` runs in the
  calling scope and re-finds Python3 asking only for the Development components,
  which leaves that variable FALSE from there on even though the interpreter is
  found and good. The failure is the quiet kind — `add_test` is skipped and
  `ctest` still reports 100% over a suite one test smaller. The root
  `CMakeLists.txt` now captures `USDVRM_TEST_PYTHON` before the pxr resolution
  and guards on that; the symptom is only visible in `ctest -N`, so check the
  test *count* rather than the percentage.

### Fixed

- **The capture-trace writer could emit a file its own reader refused.**
  `provider`, `protocol` and `sourceId` were written verbatim into the header
  and read back one token at a time, so a value with a space in it round-tripped
  to `unexpected 'Avatar' after the 'sourceId' value`. Nothing had exercised it
  because every trace in this repository was generated, and a generated
  `sourceId` is `walk-01` — the first producer to supply provenance from outside
  this repository is a VMC session, whose `sourceId` is the model title a person
  typed into a sender application. Those three keys take the rest of the line
  now, trimmed at both ends; every other key in the format stays
  token-separated, and a file written before this reads back identically, so the
  format version does not move. What no line-oriented format can carry is
  refused before the first byte, beside the expression-name check that was
  already there: a line break, and padding at either end — a refusal rather than
  a trim, because trimming would silently edit somebody's recorded provenance.

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

[Unreleased]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.8.0...HEAD
[0.8.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/animu-sphere/usd-vrm-plugins/releases/tag/v0.1.0
