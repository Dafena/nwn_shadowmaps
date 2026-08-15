# NWN:EE Linux shadow-map injector

> **NOTE ON `run-shadowmap-*.sh` LAUNCHERS.** The phase-era diagnostic launchers
> named throughout these documents are NOT included in this repository. They
> were one-off scripts for specific investigations, most of which are finished,
> and several drove code paths that no longer exist. The shipped launchers are
> `run-dev.sh`, `run-nwn.sh`, `run-shadowmap-trace.sh` and `nwn-shadows.sh`;
> everything the old ones did is reachable through the `NWN_SHADOWMAP_*`
> environment variables they set.


Shadow maps for Neverwinter Nights: Enhanced Edition, injected at runtime.
Linux (`LD_PRELOAD`) and Windows (`version.dll` proxy) from one source tree.
The game is not modified — nothing is patched on disk.

Build and install instructions are in `BUILD.md`; third-party code and
licences in `THIRD_PARTY.md`.

**Status:** works, and is developed against a live game. Sun shadows use
cascaded shadow maps; local lights get their own maps filled from inside the
engine's own draw pass. See `CURRENT_TASK.md` for what is confirmed and what is
still open.

`libnwn_shadowmap.so` is an experimental `LD_PRELOAD` injector for the Linux
build of Neverwinter Nights: Enhanced Edition.  It intercepts the engine's
OpenGL renderer and replays the engine's own visible draw buckets into an
injector-owned light-space target.  The end goal is engine-level shadow maps
that can replace stencil-volume shadows.  It does **not** modify `nwmain-linux`
or require edits to the game's `.shd` files.

This is a reverse-engineering prototype, not a finished replacement for NWN's
shadow system. The CSM implementation handles the area's directional sun, which
is validated in game. Local point/spot light shadows are IN PROGRESS: their
depth map is captured and validated, but the receiver does not yet produce a
visible shadow (see below).

**This folder is the `csm_claude` fork (2026-08-09).** The parent
`linux-x86/` directory is the untouched pre-fork baseline. There is also a
cross-compiled **Windows** build in [win/](win/README.md), currently FROZEN at
the maintainer's request.

The focused Linux scene/camera/light/matrix inventory and the next read-only
call-graph investigation are recorded in [LINUX_RENDERER_MAP.md](LINUX_RENDERER_MAP.md).
For the current accepted implementation, launch choices, tuning, limitations,
and next safe sequence, read [SHADOWMAP_STATUS.md](SHADOWMAP_STATUS.md).

## Engine authority rule

When NWN exposes a decision, the injector consumes that decision directly. It
does not replace it with a camera-distance sort, visible-object inference,
render-order census, or an injector-invented heuristic. If the engine data is
unavailable, the affected injected feature safely does nothing rather than
guessing. This is especially strict for local-shadow ownership:
`LightManager::GetShadowLights()` provides NWN's priority-ordered shadow-light
list, and only that list may select local shadow-map sources. `SetLightGL` is
still used read-only for the ordinary local-light/sun-lift census; it must never
choose or reorder a local shadow source.

As of 2026-08-13, in-game validation confirms that this engine selection is
correct. The injector must not let overlay/census code overwrite its selected
position or radius. The local receiver also orients its normal offset toward the
selected light, avoiding the camera-yaw sign flip inherent in raw screen
derivatives.

## Session summary (2026-08-10)

Everything below is in this fork only; the parent baseline is untouched.

**Sun shadows: fast now.** Replay cost is `casters x cascade layers` per frame,
and a dense area measured **61,092 static draws per frame**. Fixes, in the order
they were found:
- `NWN_SHADOWMAP_CSM_BUCKET_REPLAY=1` binds each cascade layer once per BUCKET
  instead of once per draw call. Validated in game: "buttery smooth".
- `NWN_SHADOWMAP_CSM_CASCADES` (linear saving) and `NWN_SHADOWMAP_CSM_DISTANCE`
  (fit cap; improves quality rather than cutting draws, because the replay is
  not per-cascade culled).
- A static cascade cache that refits only when the camera actually moves.
- A **world-anchored static map** (`NWN_SHADOWMAP_STATIC_WORLD`, 512..16384):
  static geometry and the area sun never change, so it is rendered ONCE per area
  instead of per frame. **Validated in game on a 32x32 area** (NWN's maximum,
  320 m -- which fits inside the default 512 m box, so this is render-once for
  every possible area) and now on by default.

**Local lights (Phase 6b/6d): decode and capture validated, receiver not
working.** `PartLight` field offsets were recovered by cross-referencing four
engine accessors and confirmed live (plausible positions, radii, and a warm
torch beside cool lights). The single-face depth map captures correctly and its
PGM is recognisable. The receiver is implemented but debug 2 shows blue coverage
with no green: the cone reaches the floor, the depth comparison returns "lit".
That is the open thread.

**Settings overlay is now Dear ImGui** and works, replacing the Phase 5b
bitmap overlay that compiled but was never visible. Live sliders for every sun
and local-light tunable, a status block, and the receiver debug modes.

**A Windows port exists** (`win/`, a `version.dll` proxy built with mingw-w64
from the same source). Sun shadows, the ImGui panel and the cascade capture all
work there. Frozen at the maintainer's request.

**Rules learned the hard way** are collected under "Hard-won rules" in
[SHADOWMAP_STATUS.md](SHADOWMAP_STATUS.md) -- re-entrancy guards for any pass
that replays buckets, the `sampler2DShadow` placeholder requirement, the
local-light/receiver ordering constraint, and where diagnostic probes must sit
to be conclusive. They are cheap to respect and were expensive to find.

## Current status (2026-08-09)

Working and verified in-game:

- A `Scene::Render` detour is installed safely with bundled `subhook`.
- Relevant renderer symbols are resolved from the executable's ELF symbol table.
- The injector creates its own light depth/color framebuffer (default 2048²).
- NWN's visible scene buckets can be replayed after the engine's normal render.
  The known populated buckets are `0,1,2,3,6,11,13`.
- A directional orthographic light view works with the engine's matrix stack
  (`NWN_SHADOWMAP_VIEW=4` is the known-good direct matrix path).
- The sun CSM receiver is now available as a translucent-black composite with
  optional cascade overlap and tunable 3x3 PCF filtering.
- Local-light support begins with the read-only
  `run-shadowmap-local-light-trace.sh` candidate census. It observes the
  engine's selected shadow-light list but does not infer light layouts or alter
  rendering.
- The replay target contains actual caster silhouettes and depth values.
- Runtime GLSL interception works without changing disk shaders.  The terrain
  receiver and generic caster branches are injected only in explicit test mode.
- The normal red/green diagnostic can use a GPU color bridge: the replay's R32F
  color map is copied to a detached texture and sampled by receivers.  This
  avoids the prior one-second CPU readback cadence.
- An injector-owned fullscreen depth receiver is present.  It copies the
  current scene depth, reconstructs world positions from the camera VP inverse,
  and compares them against the light map.  This removes the requirement that
  individual material shaders perform the receiver comparison.
- Phase 3r is verified in-game for transform stability: opaque static bucket
  `0`, source-classified stock alpha-discard static foliage/placeables from
  bucket `1`, character bucket `2`, and validated hair bucket `3` cast red
  diagnostic shadows that stay world-aligned while orbiting, panning, and
  zooming.
- The built-in stencil shadow path was observed only (never disabled):
  `Scene::RenderShadows`, `RenderStaticShadows`, `RenderDynamicShadows`, and
  `vs_shadowvol`/`fs_shadowplane` were identified.  Its normal transform
  convention was confirmed, but it is not yet being replaced.
- The read-only area-frame trace has been verified while orbiting, panning,
  and zooming.  It identifies one stable area `Scene*`/camera pair and the
  exact order `Scene::Render` → shadow prioritisation → single pass/buckets →
  dynamic geometry → remaining buckets → exit.
- That trace proved the important failure mode: the projection is stable at
  area-frame entry, but the live view-stack matrix can change before the same
  `Scene::Render` exits while the camera moves.  Future shadow work therefore
  uses only the frozen entry context, never a later "last seen" matrix.

Known remaining blocker:

- The Phase 3r receiver intentionally samples only frozen cascade layer `2`.
  Its fit ends at camera-depth `16.9720` (with a 45-unit camera far plane), so
  the visible diagnostic has a hard coverage cutoff. This is not controlled by
  fog: fog is not sampled by the injector; area visibility merely changes which
  native casters NWN submits.
- Separate from that receiver-range limit, Phase 3r duplicates NWN's **normal
  camera-culled draw stream**. A tree/object can therefore stop casting when it
  is aimed out of the normal camera frustum, even if its shadow should still
  fall within the visible scene. This is a caster-submission coverage problem,
  not the former camera-relative receiver bug.
- Four cascade matrices and private static/dynamic array layers already exist,
  but every accepted native draw is still rendered only into layer `2`, and the
  receiver therefore cannot choose a nearer or farther layer yet.
- Phase 4 is per-layer capture, selection, then transition blending. Final
  filtered lighting darkening remains unimplemented.
- The final shadow factor/composite has not been implemented.  Do not use the
  diagnostic mode as a general visual mod yet.

### Phase 1 trace-only test

`NWN_SHADOWMAP_TRACE=1` now bypasses the entire legacy shadow diagnostic:
there is no injector FBO, light replay, shader patch, GPU bridge, or fullscreen
red/green receiver. It hooks only the safe exported camera, scene, draw-bucket,
and light-selection boundaries and writes a bounded call-order trace after a
loaded gameplay area is identified.

Use the dedicated launcher so inherited shell flags cannot revive a legacy
experiment:

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-trace.sh
```

The game should look exactly normal. Load an area, then orbit, pan, and zoom
for a few seconds. The launcher replaces `shadowmap-phase1.log` each run; set
`NWN_SHADOWMAP_LOG=/path/to/log` to override it. `NWN_SHADOWMAP_TRACE_FRAMES`
defaults to 90 loaded-area frames and can be raised for a longer capture.

The trace is capped correctly: after its final bounded area frame it no longer
emits repeated exit records.

### Phase 3a cascade-math validation

This is the next isolated stage. It derives four practical split depths and
texel-snapped directional-light matrices from the frozen entry camera, but does
not allocate a target, patch a shader, or submit a draw. Normal NWN rendering is
therefore unchanged.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-cascade-math.sh
```

The same `shadowmap-phase1.log` will contain a small bounded block such as:

```text
[shadowmap][csm] frame=... near=0.1000 far=45.0000 lambda=0.70 ...
[shadowmap][csm] c0 clipFar=... centre=(...) extent=... texel=... vp=...
```

The logged `vp` values should remain stable when the camera is still; during a
camera move, their changes must be smooth and only reflect the frozen entry
context for that exact frame. This stage does not test shadow visibility yet.
If the engine's exported sun vector is still zero while an area is loading, the
fit uses the injector's known fallback direction `(0,0,-1)` rather than dropping
the frame; a nonzero engine sun replaces that fallback automatically.

### Phase 3b normal-pass light-vector trace

NWN area lighting is model-driven: the area sun is an MDL. The old exported
`m_lightAreaDiffuseDirection` can therefore be zero even in a legitimate area;
it is not enough to choose a production shadow direction by itself. This
checkpoint only observes the normal area-light pass; it does not hook or react
to `SetAreaLightDirection()`.

This companion launcher retains the frozen cascade-math trace and additionally
records a bounded list of unit-length `glUniform3f`/`glUniform3fv` uploads from
the selected area’s normal material pass. Calls are forwarded unchanged; it
does not inject shaders or affect lighting.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-light-vector-trace.sh
```

Look only for `[shadowmap][lightvec]` records in `shadowmap-phase1.log`. A
repeated, non-axis-aligned vector shared by normal material programs is a
candidate sun direction. The trace logs both its uploaded `view=(...)` form and
that vector transformed by the frozen camera inverse as `world=(...)`. A
camera-space sun will vary in the former but remain fixed in the latter. It
must be validated before the first cascade depth target is rendered. In the
verified area trace that world vector is `(0.43322,0.48738,0.75814)`; NWN stores
it toward the light, so the cascade/light-ray convention uses its negation:
`(-0.43322,-0.48738,-0.75814)`. The trace now promotes it only after two
distinct material programs agree, and cascade math uses it on the following
area frame. This still does not render anything.

## Build

`subhook/` is bundled.

```bash
cd "$NWN/bin/linux-x86"
make
```

This produces `libnwn_shadowmap.so`, the DEVELOPMENT build: every panel control
visible, `.pgm` dumps and frame-cost instrumentation on, and it expects the
launcher scripts to supply its configuration.

`make deploy` produces `libnwn_shadowmap_deploy.so` instead -- the build to hand
to anyone else. It carries its own defaults (plain `LD_PRELOAD`, no launcher),
hides every control that removes shadows, and writes nothing but its settings
file. See `LINUX_DEPLOY.md`, which is the readme to send with it, and the
`NWN_SHIP` section of `AGENTS.md` for what the two builds differ on.

`cd win && make` produces the Windows `version.dll`, always a shipping build.

## Recommended diagnostic launch

The following runs the current green/red fullscreen diagnostic.  It explicitly
turns off two rejected experiments so an old shell environment cannot revive
them.

```bash
cd "$NWN/bin/linux-x86"

env -u NWN_SHADOWMAP_VERTEX_REPROJECT \
    -u NWN_SHADOWMAP_LIGHT_CASTER_MATRICES \
    -u NWN_SHADOWMAP_UNIFORM_TRACE \
    -u NWN_SHADOWMAP_RECEIVER_TRACE \
NWN_SHADOWMAP_LIGHT=1 \
NWN_SHADOWMAP_INJECT_TEST=1 \
NWN_SHADOWMAP_COLORCAST_TEST=1 \
NWN_SHADOWMAP_FULLSCREEN_RECEIVER=1 \
./run-nwn.sh ./nwmain-linux
```

Expected once an area is loaded:

```text
[shadowmap] selected area Scene=0x... eye.z=...
[shadowmap] light pass running. view=LIGHT (direct matrix write) ... ortho=on
[shadowmap] GPU color bridge ok
[shadowmap] fullscreen depth receiver compiled (program=...)
[shadowmap] fullscreen depth receiver draw live
```

The red/green mode is deliberately not normal game lighting:

- green = light comparison says lit
- red = light comparison says occluded
- blue = no usable reconstructed scene-depth sample

### Logged cascade-target test

Use the helper below for the PS4-style target validation.  It uses the full
recommended diagnostic configuration, adds `NWN_SHADOWMAP_CASCADE_TARGETS=1`,
and continuously writes both terminal streams to `shadowmap-cascades.log` in
this folder.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-cascades.sh
```

The file is replaced for each run.  To choose another location, set
`NWN_SHADOWMAP_LOG=/path/to/log` before invoking the helper.

### Safe four-layer target validation

After a normal trace has resolved the area sun, use this helper instead of the
legacy cascade-target test:

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-cascade-target-validate.sh
```

It remains in trace mode. It allocates private four-layer static and dynamic
depth arrays, attaches each of the eight layers in turn, clears each private
depth layer once, then verifies its stored far-depth value by readback. It does
**not** draw geometry, replay buckets, copy engine depth, inject shaders, or
composite any receiver. It also records a bounded census of the original draws
inside native area `RenderDrawBucket` calls, without redirecting them. Normal
NWN visuals must remain unchanged.
Look for:

```text
[shadowmap][lightvec] selected area direction light->scene=(...)
[shadowmap] cascade static target ... layer-status=[COMPLETE,COMPLETE,COMPLETE,COMPLETE]
[shadowmap] cascade dynamic target ... layer-status=[COMPLETE,COMPLETE,COMPLETE,COMPLETE]
[shadowmap][csm] target clear static: non-clear=0/...
[shadowmap][csm] target clear dynamic: non-clear=0/...
[shadowmap][csm] target clear validation: READY
[shadowmap][geom] frame=... bucket=... draws=... indexed=... programs=...
[shadowmap][csm] target validation ready: ... no rendering
```

### Native camera-depth capture (Phase 3f)

After target validation and the native draw census are established, run this
one-shot bridge:

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-cascade-camera-capture.sh
```

It lets NWN complete its original bucket-0 draw normally, then immediately
duplicates the same native driver calls into private dynamic cascade layer 0.
The engine camera, shader, material state and visible framebuffer are never
modified. The expected result is a normal-looking game plus one file,
`shadowmap_cascade_camera.pgm`, in this folder. Look for:

```text
[shadowmap][capture] dumped shadowmap_cascade_camera.pgm frame=... bucket=0 duplicate-draws=... written=.../...
```

This proves safe in-sequence geometry duplication only. It is deliberately
camera-space, not light-space: no light matrix, receiver, shadow factor, or
visible shadow is introduced by this phase.

### Normal-pass matrix trace (Phase 3g)

The next read-only step identifies which 4x4 uniform locations are the normal
camera projection/view transforms:

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-cascade-matrix-trace.sh
```

It compares each native `glUniformMatrix4fv` upload with the immutable area
camera projection, view, inverse view and `projection * view`, then forwards
the original upload unchanged. Look for `[shadowmap][mat]` records with
`kind=camera-proj`, `camera-view`, or `camera-vp`. The remaining
`object-or-other` locations are the per-object transforms to preserve when a
later light-space duplicate is attempted.

### Native light-space depth capture (Phase 3h)

This is the first light-space geometry proof. It is still a one-shot private
depth capture: no receiver or final-frame compositing is enabled.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-cascade-light-capture.sh
```

For every selected native bucket draw, the injector first lets NWN draw
normally. It then derives the local model transform from the recorded normal
`m_mv` (`inverse(frozen camera view) * m_mv`), uploads
`cascadeLightView * model` and the cascade-0 orthographic projection only to
the private duplicate, draws, and restores the exact normal `m_mv`/`m_proj`
before returning. The visible game must remain normal. It writes
`shadowmap_cascade_light.pgm`; look for:

```text
[shadowmap][lightcap] live: program=... m_mv=... m_proj=...
[shadowmap][lightcap] dumped shadowmap_cascade_light.pgm frame=... layer=... duplicate-draws=... skipped=... written=.../...
```

The PGM should be a light-space depth image rather than the camera-space image
from Phase 3f. A valid result only proves transforms and depth capture; it is
not yet a usable shadow-map implementation.

The capture also brackets NWN's `Scene::RenderDynamicGeometry` stage, because
animated creatures are not reliably submitted through the static area buckets.
The log's `dynamic-scopes` counter confirms that this separate native path was
included. Alpha-tested foliage/card geometry is deliberately not claimed by
this proof yet.

### Static-only fullscreen receiver (Phase 3i)

This is the first visible implementation slice. It does **not** use an MTR,
`.shd` file, source injection, the old red/green bridge, or a mutable material
matrix. Each selected-area frame it duplicates only the proven opaque static
bucket into the private **static** cascade layer, then reconstructs world
positions from that same frame's completed depth buffer using the frozen entry
camera inverse. A private fullscreen shader compares those positions against
that same frame's private light depth.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-static-receiver.sh
```

Shadowed pixels are deliberately painted dark red. This is a geometry and
camera-stability diagnostic, not final lighting. It includes neither animated
characters nor alpha-tested foliage/cards. Orbit, pan, and zoom while watching
the static red shapes: they must remain anchored to their opaque casters. The
launcher writes the normal combined log to `shadowmap-phase1.log`; the useful
records are `[shadowmap][static] receiver drew frame=...` and the light-capture
line with `target=static`.

The launcher's `NWN_SHADOWMAP_TRACE_FRAMES` cap limits verbose trace records
only in this mode. It does not stop the per-frame frozen context, static-depth
capture, or receiver once the log reaches its normal 90-frame boundary.

### Dynamic character-stage capture (Phase 3j)

Before dynamic casters can join the stable static receiver, the injector must
prove which engine stage actually submits their geometry. This launcher only
duplicates native draws reached while NWN executes its named
`Scene::RenderDynamicGeometry` stage into the private **dynamic** cascade
array. It has no receiver and therefore makes no visible rendering change.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-dynamic-character-capture.sh
```

Look for `[shadowmap][dynamic] ... delta attempts=... draws=...`. Nonzero draws
unblock a dynamic depth PGM and then a combined static+dynamic receiver. Zero
draws means the player is submitted through a different native draw boundary;
that is useful evidence, not a failure to be hidden with a bucket guess.

### Post-dynamic bucket capture (Phase 3k)

The named dynamic stage is a queue boundary in this Linux renderer: it produced
zero native draws. The normal-pass census instead places live geometry in bucket
`2` immediately after that stage. Phase 3k duplicates **only bucket 2** into the
private dynamic cascade array and writes `shadowmap_cascade_light.pgm`; it does
not alter the visible game and it does not enable the red receiver.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-dynamic-bucket-capture.sh
```

Move near the player, then exit after the first loaded-area frame. A fresh PGM
containing the player proves bucket 2 is a valid next dynamic-caster source. If
it contains only unrelated geometry, it stays a diagnostic and the next
post-dynamic bucket is tested separately. Override the candidate only for a
probe with `NWN_SHADOWMAP_DYNAMIC_BUCKET=<n>`.

### Alpha/card candidate capture (Phase 3m)

The combined census shows bucket `3` immediately after the proven player bucket
and it has its own program (`215`) and two native draws. It is the first
candidate for alpha-tested cards/foliage. It must be visually verified alone;
do not add it to the receiver merely because the old broad replay happened to
include it.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-alpha-bucket-capture.sh
```

The game remains visually normal. Inspect the fresh
`shadowmap_cascade_light.pgm` and `shadowmap_alpha_cards.pgm`. The latter is a
tight, enlarged crop of the former's written bucket-3 depth texels, specifically
so sparse hair cards can be inspected without being lost in a full 2048² map.
Card silhouettes with holes matching the alpha texture are a positive result.
Solid rectangles mean it is not yet safe to enable it as an alpha caster.

### Alpha shader classification (Phase 3n)

Bucket 3 did not contain the expected cards, so the next step is not another
bucket guess. This read-only trace dumps each engine fragment shader compiled
after the area loads to `/tmp/nwn_shadow_fragment_<id>.glsl`; it does not edit
the source, redirect a draw, or enable a receiver.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-alpha-shader-trace.sh
```

Load the same area with trees and the player’s hair, then exit. The injector
also emits `[shadowmap][alpha] bucket=<n> program=<n> attached=...` for every
program in the bounded area-bucket census. Those attachment IDs join the live
bucket to the matching fragment dump, so alpha-card work is based on the actual
engine draw path rather than an assumed bucket.

The current verified references are: static `mzmap_008.mtr` uses fragment
shader `190` through program `191` in bucket `0`, but it is not the tree
foliage material. The actual foliage is stock `mzmap_006.mtr`, whose per-launch
stock alpha-discard shader/program identity is classified at runtime in bucket
`1`. Dynamic `c251_hair.mtr` uses the custom alpha/dither fragments `223`
(program `224`, bucket `2`) and `214` (program `215`, bucket `3`). These IDs
are per-process diagnostics, not stable configuration values; the point is the
bucket and material-path relationship.

### Static + character receiver (Phase 3l)

Phase 3k proved that bucket `2` contains the character. Phase 3l captures
static bucket `0` into the private **static** array and bucket `2` into the
private **dynamic** array, clearing both independently every selected-area
frame. The existing fullscreen receiver reconstructs the world point once from
the same frozen camera context and marks it red if either depth array occludes
it. This is still a diagnostic, not final darkening or a replacement for all
casters.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-static-dynamic-receiver.sh
```

Expected result: the original stable opaque-static red shadows remain, with a
red player shadow added. Pan, orbit, and zoom: both must remain anchored. The
log line `[shadowmap][static] receiver drew ... static-draws=... dynamic-draws=...`
must show nonzero counts for both targets. Alpha/card bucket coverage is not yet
claimed by this phase even if some happens to appear in bucket 2.

### Phase 3p: validated alpha bucket in the combined receiver

Phase 3o proved that the bucket-3 custom-hair duplicate preserves the original
fragment discard: its enlarged private PGM contains the cut-out card silhouette
and internal holes. `run-shadowmap-static-dynamic-alpha-receiver.sh` therefore
keeps the proven static bucket `0` and dynamic character bucket `2`, then adds
only bucket `3` into the already separate dynamic depth target. It is not a
shader edit, an all-bucket replay, or final lighting.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-static-dynamic-alpha-receiver.sh
```

The expected log includes `Phase 3p enabled` and a receiver line with
`buckets=0+2+3`. Verify that the existing static and player shadows stay
anchored while moving the camera, and that the hair-card contribution does not
introduce a visual regression. Red on a caster itself is still diagnostic
self-shadowing from the current fixed depth bias, not a final-lighting result.

### Phase 3q: static foliage alpha proof

Trees are a separate static alpha path from the dynamic `c251_hair` pass.
`mzmap_006` has no custom shader: it uses NWN's stock compiled
`NO_DISCARD 0` variant and the real `fAlphaDiscardValue` material uniform. This
depth-only launcher identifies that fragment source, then accepts only a live
non-negative alpha-discard value while capturing native bucket `1`. No program
ID is hard-coded and no receiver is enabled.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-static-alpha-bucket-capture.sh
```

It must log `classified static AlphaDiscard fragment`, `selected static
AlphaDiscard program`, and write `shadowmap_static_alpha_cards.pgm`. Tree
silhouettes with cut-out holes mean the remaining fault is receiver composition,
not static foliage caster capture.

### Phase 3r: static foliage in the stable receiver

Phase 3q proved native bucket `1` contains cut-out static foliage and ordinary
alpha-discard placeables. Phase 3r adds only source-classified draws from that
bucket to the existing **static** depth array, alongside opaque bucket `0`. It
does not share the dynamic target, replay all of bucket 1, or change frozen
receiver matrices. Character bucket `2` and validated hair bucket `3` remain in
their independent dynamic target.

```bash
cd "$NWN/bin/linux-x86"
./run-shadowmap-static-dynamic-alpha-foliage-receiver.sh
```

Expected diagnostic: anchored red tree and alpha-placeable shadows, plus the
existing opaque, player, and hair shadows. The log must include `Phase 3r
enabled`, `selected static AlphaDiscard program`, and receiver buckets ending in
`+static-alpha:1`.

The current launcher uses cascade layer `2` only. Its hard distance cutoff is
expected; it is a coverage limitation of the diagnostic, not a fog setting or
a foliage-caster failure. A 32×32 area can still show many distant shadows
because NWN submits more visible geometry there, but the receiver has not yet
implemented selection among its other three existing cascade layers.

### Phase 4: cascaded coverage

Phase 4a is available as a depth-only validation harness:

```bash
./run-shadowmap-four-cascade-capture.sh
```

It preserves the exact Phase 3r caster sets, replays them into all four
corresponding static/dynamic depth-array layers, and gives every target-layer
pair independent freshness/draw counters. The visible receiver is suppressed,
so the scene should look normal. It writes eight depth images named
`shadowmap_cascade_{static,dynamic}_c0.pgm` through `c3.pgm`, and matching
`[shadowmap][csm] multi capture` records into `shadowmap-phase1.log`.

This validates cascade target ownership, clears, and transforms. It does not
remove Phase 3r's normal-camera caster culling; proper caster coverage must be
solved before this can become a complete directional-shadow implementation.

The first in-game Phase 4a run passed: all eight PGMs were written at frame 2,
with six accepted static duplicate draws and nine accepted dynamic duplicate
draws in **each** layer, and every target-layer contained non-uniform depth.
That verifies the layer-specific clear/freshness contract; it is intentionally
not evidence that the normal camera provides all required shadow casters.

Phase 4b is deliberately a **read-only culling census**, before any receiver
selection work. It confirms the source of the camera-dependent caster loss
without changing the displayed frame:

```bash
./run-shadowmap-caster-cull-trace.sh
```

It hooks `ManageSceneBSP(Scene*)`, lets NWN perform its ordinary BSP/camera
frustum processing, then logs `[shadowmap][cull]` counts for the original
`meshshadowbucket`, stencil-volume `staticshadowbucket`, and the native part /
shadow cull counters. No target, replay, shader injection, receiver, matrix,
or GL state is touched. Orbit a visible tree or placeable toward/out of the
screen edge. If `meshshadow` changes, it is direct in-engine proof that the
duplicated Phase 3r source set is normal-camera limited. The launcher rewrites
`shadowmap-phase1.log`, which can be inspected without copying console text.
The ordinary verbose trace still ends at 90 frames; the compact census remains
live afterward and records one baseline plus only changed counts.

**First in-game result:** while orbiting after the 90-frame general trace had
ended, `meshshadow` changed repeatedly between 4 and 15 entries (then back
down). `staticvolume` and the exposed cull counters stayed zero at that
post-BSP sample point. This is the required direct evidence: the original-mesh
list duplicated by Phase 3r is camera-frustum dependent, so the final caster
path must not be based only on that list.

Phase 4c is the next separate read-only probe. It uses the executable's plain
`BSPTraverse(BSPNode*, callback, user)` walker, which has no camera or frustum
plane parameters. The renderer's own callback first resolves `nodedata*` from
`BSPNode +0x70`; the regular `PartTriMesh*` array/count then live at
`nodedata +0x20/+0x28` (with a separately reported primary part at `+0x90`).
The probe counts those candidates and
does not retain a pointer or replay anything:

```bash
./run-shadowmap-full-bsp-census.sh
```

After the general trace reaches 90 frames, orbit or pan in the same area. Look
for `[shadowmap][fullbsp]`: a stable `tri-candidates` count alongside the
known-varying `normal-meshshadow` count establishes that a full engine-owned
candidate set can be enumerated safely. It does **not** yet prove that the
engine's material/transform setup can render those candidates to a light map.

To see the existing camera-limited result at the same time, use:

```bash
./run-shadowmap-full-bsp-red-diagnostic.sh
```

This deliberately retains Phase 3r's red diagnostic (red means the receiver
is shadowed by its current camera-culled caster set) while logging both
`[shadowmap][cull]` and `[shadowmap][fullbsp]`. A shadow vanishing on camera
motion while `tri-candidates` remains stable is the precise visual proof of the
problem. It is a comparison tool, not a final shadow implementation. The
visual launcher alone also enables a bounded relaxed viewport-selection gate
for compositors that draw the world into a half-resolution FBO after a
full-resolution UI pass; its log marks any selection using that fallback.

**Confirmed Phase 4c result (2026-08-09):** the selected test area has 403
plain BSP nodes, all with `nodedata`; its stable all-static source is 15 regular
mesh-array parts plus 16 primary parts = **31 candidates**, with no invalid
payloads. During the same orbit the normal `meshshadow` list changed from 2 to
15 (and later 4 to 11) while the full total remained 31. This is the direct
engine-level proof that a full, camera-independent static caster source exists.

### Phase 4d: native full-BSP static submission

`run-shadowmap-full-bsp-native-static.sh` is the first opt-in use of that
source. It is still a red diagnostic, not final lighting. Immediately after
NWN's normal camera BSP traversal, it walks the full static BSP, deduplicates
the 31 candidate part pointers for that frame, and feeds them only through
NWN's pending-list helper `AddPartToMeshBuckets(PartTriMesh*)`. NWN then
executes its own normal `Scene::AddPartsToDrawBuckets()` and
`Scene::RenderDrawBucket()` stages at their valid per-frame lifecycle point.
The existing adjacent GL duplicate sees each normal per-part transform and
writes the light-space depth into the private static cascade.

The first Phase 4d attempt incorrectly called `Scene::AddPartsToDrawBuckets()`
and replayed buckets at Scene entry. It rendered one frame, then crashed in
that helper on the next. Do not manually call the draw-bucket builder or replay
buckets from the BSP hook: its inputs are only valid after NWN's own renderer
update, as the engine's `Scene::Render()` ordering demonstrates.

```bash
./run-shadowmap-full-bsp-native-static.sh
```

Load an area, orbit and pan around static trees/placeables, and inspect
`shadowmap-phase1.log`. A valid first run includes a line like
`[shadowmap][fullsubmit] ... submitted=31 ... pending-meshshadow=...`. Red static
shadows should stay attached when the camera moves; the player is intentionally
absent in this static-only test. If the native bindings cannot be resolved or
the static cascade target is not ready, the mode stays inert rather than using a
legacy camera replay.

**Confirmed (2026-08-09):** the corrected lifecycle ran all 90 bounded traced
area frames with a stable `submitted=31` full-BSP source and no crash or
disappearing static shadows.

### Phase 4e: full-static four-cascade depth capture

```bash
./run-shadowmap-full-bsp-four-cascade-capture.sh
```

This combines the confirmed Phase 4d static source with the Phase 4a four-layer
fan-out. It is intentionally depth-only: the visible receiver is suppressed,
so the scene must look normal. After loading an area and orbiting/panning, check
`shadowmap-phase1.log` for `[shadowmap][csm] multi capture` and inspect
`shadowmap_cascade_static_c0.pgm` through `shadowmap_cascade_static_c3.pgm`.
Each should contain non-uniform depth from the full static source. Dynamic
creatures/hair are deliberately excluded; the next implementation step is a
camera-depth-selected static CSM receiver.

### Phase 4f: visible full-static CSM diagnostic

```bash
./run-shadowmap-full-bsp-csm-static.sh
```

This is the first visible four-cascade receiver. It reconstructs world position
from the completed area framebuffer and frozen entry camera, computes
`-view.z`, and selects cascade 0–3 against the exact split distances used to
build those layers. It remains static-only and red-only. Hard split boundaries
are intentional; dynamic casters, overlap blending, filtering, and final
darkening are not enabled. Orbit, pan, and zoom: shadows must remain attached
to their static casters and should not disappear when crossing the former
single-layer range.

Only after a non-camera-frustum caster submission route is mapped will the
receiver select the matching frozen `lightVP` and array layer from reconstructed
camera depth. Hard transitions will be validated first; only then will the
injector blend a small overlap and expose quality/range controls. Red remains
diagnostic until this is stable.

**Confirmed (2026-08-09):** the four static layers remained world-anchored
while orbiting, panning, and zooming. The receiver logged fresh depth-selected
static frames (`static-draws=52..56`) and the full BSP submission stayed at 31
candidates per frame.

### Phase 4g: static CSM plus dynamic character body

```bash
./run-shadowmap-full-bsp-csm-static-dynamic.sh
```

This extends the confirmed Phase 4f receiver with bucket 2 only: the already
proven dynamic character-body route. Both static and dynamic bucket depth is
duplicated into all four layers using each original draw's exact transform. The
fullscreen receiver selects the same frozen camera-depth layer for both arrays.
It refuses to draw a frame unless every static *and* dynamic layer is fresh, so
it cannot reuse a previous-frame player shadow. Hair/cards (bucket 3), split
blending, filtering, and final lighting are intentionally excluded. Success is
a stable red player-body shadow plus static red shadows while orbiting, panning,
and zooming.

### Phase 4h: complete currently-mapped caster set

```bash
./run-shadowmap-full-bsp-csm-all-casters.sh
```

This adds the validated dynamic alpha/card bucket 3 after the body bucket 2,
while retaining static opaque bucket 0 and source-classified static alpha bucket
1. The dynamic target is cleared by fresh body depth each frame, then receives
the alpha/card depth in the same four layers; its original discard behavior is
not replaced. This is the first diagnostic containing every currently mapped
caster class, but is still red-only with hard CSM transitions. Verify body/hair
and foliage shadows remain attached while moving and zooming before beginning
receiver filtering or final lighting modulation.

**Confirmed (2026-08-09):** static opaque/alpha, character body, and dynamic
alpha/card casters all render correctly together. Cascade resolution and range
were visually confirmed acceptable; the diagnostic remains stable under camera
motion.

### Phase 5a: opt-in dark shadow composite

```bash
./run-shadowmap-full-bsp-csm-shadows.sh
```

This launcher uses the complete Phase 4h caster set but replaces diagnostic red
with a translucent black overlay only where the CSM comparison reports shadow.
It is a conservative first presentation pass: it does not alter NWN materials,
lighting uniforms, or the engine's stencil renderer. It saves/restores blend
enable, RGB/alpha blend factors, blend equations, depth, colour mask, textures,
FBO, viewport, and program state around the fullscreen draw.

Default strength is `0.42`. Override it per launch, for example:

```bash
NWN_SHADOWMAP_CSM_STRENGTH=0.25 ./run-shadowmap-full-bsp-csm-shadows.sh
```

### Area shadow settings

The injector reads the active area's own directional-shadow policy:
`SunShadows` during daytime, `MoonShadows` at night, and `ShadowOpacity`
(`0..100`). These settings attenuate **only the sun/moon CSM term**; local
point/spot-light shadows remain independent. If the area disables its active
directional shadow or sets opacity to zero, the injector leaves the scene
without a sun/moon shadow just as the area requests.

`ShadowOpacity` is verified against NWN's own `shadowalpha` global and is now
applied by default to the directional CSM strength. `SunShadows` and
`MoonShadows` are also applied by default: day uses `SunShadows`, night uses
`MoonShadows`. They gate only directional CSM; local-light shadows remain
independent. Use `NWN_SHADOWMAP_AREA_SHADOW_OPACITY=0` for an opacity A/B
fallback, `NWN_SHADOWMAP_AREA_SHADOW_POLICY=0` to ignore the two boolean flags,
and `NWN_SHADOWMAP_AREA_SHADOW_FLAGS=0` to disable capture entirely. The
startup log reports the observed state as
`[shadowmap][area] directional shadows: ...`.

When NWN switches this day/night policy, the directional CSM composite now
fades rather than disappearing in one frame. The default is `0.75` seconds;
set `NWN_SHADOWMAP_AREA_SHADOW_FADE=0..10` at launch or use **Day/night fade**
in the development overlay's Sun shadows section. `0` restores the instant
switch. This is strictly a final-composite fade: it does not rebuild or blend
depth maps, move the light, invalidate the static/dynamic cache, or attenuate
local point/spot-light shadows.
`run-dev.sh` also enables the bounded read-only `[shadowmap][area-probe]` log;
it compares the candidate values with NWN's own `shadowalpha` global and has
no rendering effect.

### Scope boundary: scripted area-light direction

`SetAreaLightDirection()` is intentionally outside this checkpoint. There is
no setter hook, timed static-map rebuild, transition texture, or per-bucket
moving-sun path. The implemented area support is limited to its existing
directional policy: `SunShadows`, `MoonShadows`, and `ShadowOpacity`.

The Performance panel's **Injector shadow targets** value remains a conservative
estimate for this injector's own shadow and screen-copy textures; it is not
NWN's total process VRAM.

## HISTORICAL -- Planned local point-light quality modes

> **SUPERSEDED.** Superseded. Local lights use ONE downward face at 170 degrees, filled from inside the engine's own bucket pass. A 1/3/4-face ladder was built on 2026-08-15 and removed the same day in favour of a single face plus a high-resolution map.


Local-light shadows currently use one wide, downward-facing depth map per
shadowed light. It is the inexpensive **Contact** mode: useful for a floor
shadow under a torch, but not full point-light coverage. A body can therefore
be partly absent from the shadow when it lies outside that downward cone.

The next local-light phase is an optional **Cube** mode: six 90-degree depth
faces per selected point light, so the receiver can select the correct face for
the light-to-pixel direction. Cube mode will use dynamic casters only by
default (character body, optional alpha cards); static floors and walls receive
the shadow but are not captured as casters. It is capped at three shadowed
lights. The current Contact mode remains the inexpensive fallback.

The implementation began as an isolated one-light proof and is now part of the
regular development path. One source is the runtime-confirmed fallback; the
current workspace has compiled 1--3 slot activation pending the first two-light
in-game confirmation.

#### Cube capture and receiver proof (Phase 7a--7d)

The first implementation slice is now available but remains **non-visible**:
`./run-local-cube-probe.sh` runs the normal development launcher plus one
isolated six-face, 90-degree depth capture for the selected local light. It
does not bind the cube target to the receiver, so the game's image must remain
unchanged. It emits six `shadowmap_local_cube_[+-][xyz].pgm` files and bounded
`[shadowmap][local-cube]` per-face draw/content lines in `shadowmap-phase1.log`.
The first capture probe remains non-visible and is useful for inspecting the
six `shadowmap_local_cube_[+-][xyz].pgm` files. The later native-dynamic bridge
does not replay a bucket: it duplicates NWN's actual
`Scene::RenderDynamicGeometry` draws into all six faces, including BaseVertex
and instanced GL draw paths. This was validated in-game: the player appears in
the cube maps and casts a full point-light shadow onto the floor/walls when
`./run-local-cube-receiver-debug.sh` enables the separate receiver.

The original proof was validated with one light. As of 2026-08-13 **Local
shadow sources** is no longer a plain budget: it selects a (method, light count)
pair across three tiers, and the persisted value is the combo index.

| index | tier | fill | count |
| --- | --- | --- | --- |
| 0 | Legacy | replay of NWN's draw buckets, honouring "moving casters only" | 3 |
| 1--3 | Emitter | replay of all four buckets with nothing cleared between them | 1--3 |
| 4--6 | High | duplication of each visible draw into the light's map | 1--3 |

All three take their lights from `LightManager::GetShadowLights()` in engine
priority order, so all three stop at the three shadow-casting lights NWN
exposes; the tiers differ only in how the map is filled.

Default is index 4, **High with one light**: High is the only tier whose casters
are the meshes the player actually sees, and one source is where its cost (one
draw per caster per light) is affordable. Legacy and Emitter replay buckets
instead, a handful of calls regardless of caster count, which is why Emitter can
afford four lights.

Emitter is a deliberate reproduction of the 2026-08-10 build's capture: all four
buckets, nothing cleared between them. **Light supported** remains the separate
cheap lift/census limit and does not cap shadow sources.
camera. The log records each promotion as `[shadowmap][area-light] CSM step`.

Use `run-shadowmap-full-bsp-csm-all-casters.sh` at any time to return to the
red diagnostic for A/B comparison. The depth arrays already use hardware
linear comparison filtering (2x2 PCF); cascade overlap blending and
material-aware diffuse/ambient response are subsequent phases.

### Phase 5b: injected settings overlay

With any CSM launcher running, press `Ctrl+Shift+F11` in a loaded area to toggle
the injector-owned settings window. It is deliberately rendered after the CSM
receiver, so the panel never becomes source depth or a shadow receiver.

The first panel is keyboard-first while NWN's SDL event dispatch is mapped:

- `Ctrl+Shift+Up` / `Down`: select a row.
- `Ctrl+Shift+Left` / `Right`: edit its value.
- `Ctrl+Shift+Enter`: toggle red versus dark output.
- `Escape`: close the panel.

It exposes live output mode, shadow strength, receiver bias, and cascade split
lambda. Values immediately affect subsequent frames; they are not persisted
yet. The overlay is the foundation for a mouse-driven, tabbed ReShade-style
configuration UI after input capture is safely integrated.

### Phase 5c: opt-in cascade overlap blend

```bash
./run-shadowmap-full-bsp-csm-soft-shadows.sh
```

This retains the accepted Phase 5a caster and receiver path, but cross-fades
the depth result from the current cascade into the next cascade during the last
`0.75` world units before each split. It never changes caster submission,
targets, camera reconstruction, or the established hard-split launcher.

Override the overlap width (or set it to zero to reproduce Phase 5a exactly):

```bash
NWN_SHADOWMAP_CSM_BLEND=1.25 ./run-shadowmap-full-bsp-csm-soft-shadows.sh
```

The receiver log reports `blend=...` so the active mode is unambiguous. This is
not itself a soft-edge filter: it only changes pixels near a cascade boundary.
The texture arrays already provide hardware 2x2 comparison filtering, but that
is deliberately subtle at the validated cascade resolution.

### Phase 5d: opt-in visible PCF shadow filtering

```bash
./run-shadowmap-full-bsp-csm-filtered-shadows.sh
```

This uses the same stable Phase 5a receiver and caster maps, plus Phase 5c's
split overlap, but samples a 3x3 receiver-side PCF footprint. Its default
radius is `0.75` shadow texels, which lightly softens the edges of the
tree, placeable, and character shadows. It does not change caster geometry,
camera matrices, or depth target ownership.

Tune it per launch (zero returns to the subtle hardware comparison filter):

```bash
NWN_SHADOWMAP_CSM_PCF_RADIUS=1.0 ./run-shadowmap-full-bsp-csm-filtered-shadows.sh
```

## Useful runtime switches

| Variable | Purpose |
| --- | --- |
| `NWN_SHADOWMAP_OFF=1` | Load the library but install no hooks; A/B test. |
| `NWN_SHADOWMAP_TRACE=1` | Phase 1 only: bypass all shadow rendering and log the loaded-area camera/scene/light call order. |
| `NWN_SHADOWMAP_TRACE_FRAMES=90` | Number of loaded-area frames recorded by the trace. |
| `NWN_SHADOWMAP_TRACE_EVENTS=4096` | Hard safety bound for scene/light trace entries. |
| `NWN_SHADOWMAP_CASCADE_MATH=1` | With trace mode only: compute/log four frozen-context cascade matrices; no GL rendering. |
| `NWN_SHADOWMAP_CASCADE_LAMBDA=0.70` | Practical/logarithmic split blend used by cascade-math validation (0..1). |
| `NWN_SHADOWMAP_CASCADE_RESOLUTION=2048` | Virtual target resolution used solely for texel snapping in cascade-math validation. |
| `NWN_SHADOWMAP_LIGHT_VECTOR_TRACE=1` | With trace mode only: observe bounded normal-pass vec3 uniform uploads; never rewrites them. |
| `NWN_SHADOWMAP_CASCADE_TARGET_VALIDATE=1` | With trace mode only: allocate, clear, and check all four static/dynamic cascade layers; no geometry rendering. |
| `NWN_SHADOWMAP_CASCADE_GEOMETRY_TRACE=1` | With trace mode only: count original normal-pass draws per area bucket; no draw redirection. |
| `NWN_SHADOWMAP_CASTER_FULL_BSP_NATIVE_SUBMIT=1` | Phase 4d only, trace mode and static receiver required: append every deduplicated full-static-BSP candidate to NWN's pending native mesh bucket after `ManageSceneBSP`; NWN itself builds and renders its normal draw buckets, whose adjacent light-space duplicate feeds the private static red diagnostic. |
| `NWN_SHADOWMAP_CSM_STATIC_RECEIVER=1` | Phase 4f only, static receiver required: force four-layer static capture and select the layer per receiver pixel from frozen camera-space depth. Static red diagnostic only. |
| `NWN_SHADOWMAP_CSM_DYNAMIC_RECEIVER=1` | Phase 4g only, requires the static CSM receiver: add bucket 2 to matching dynamic layers and require every dynamic layer to be fresh before compositing. Dynamic alpha is excluded. |
| `NWN_SHADOWMAP_CSM_ALPHA_RECEIVER=1` | Phase 4h only, requires Phase 4g: add the proven alpha/card bucket 3 after bucket 2 in the same fresh dynamic layers. |
| `NWN_SHADOWMAP_CSM_COMPOSITE=1` | Phase 5a: replace red diagnostic output with translucent black compositing where CSM depth says shadowed. Requires a static CSM receiver. |
| `NWN_SHADOWMAP_CSM_STRENGTH=0..1` | Phase 5a opacity (default `0.42`). Smaller values preserve more of NWN’s original lighting. |
| `NWN_SHADOWMAP_AREA_SHADOW_FLAGS=0/1` | Capture the current area's `SunShadows`, `MoonShadows`, and `ShadowOpacity`. Default `1`; set `0` to disable the diagnostic hook. |
| `NWN_SHADOWMAP_AREA_SHADOW_OPACITY=0/1` | Apply the verified area `ShadowOpacity` to directional CSM. Default `1`; local lights are unaffected. |
| `NWN_SHADOWMAP_AREA_SHADOW_POLICY=0/1` | Apply the captured `SunShadows`/`MoonShadows` booleans to directional CSM. Default `1`; set `0` for an A/B fallback. |
| `NWN_SHADOWMAP_AREA_SHADOW_FADE=0..10` | Seconds used to fade directional CSM when NWN switches day/night policy. Default `0.75`; `0` restores the instant switch. |
| `NWN_SHADOWMAP_AREA_SHADOW_PROBE=1` | Bounded read-only log of area flags, candidate opacity bytes, and NWN's own `shadowalpha`. Set by `run-dev.sh`; never changes rendering. |
| `NWN_SHADOWMAP_CSM_BIAS=0..0.05` | Receiver comparison bias (default `0.0025`). Also editable live from the Phase 5b overlay. |
| `NWN_SHADOWMAP_CSM_BLEND=0..10` | Phase 5c world-space width of the overlap before a cascade split; `0` retains hard transitions. |
| `NWN_SHADOWMAP_CSM_PCF_RADIUS=0..4` | Phase 5d manual 3x3 PCF radius in shadow texels; `0` uses only the hardware 2x2 comparison filter. |
| `NWN_SHADOWMAP_CASCADE_CAMERA_CAPTURE=1` | With trace mode only: duplicate one selected native area bucket into private dynamic layer 0 and dump one camera-depth PGM; no visible rendering change. |
| `NWN_SHADOWMAP_CASCADE_CAMERA_BUCKET=0` | Bucket selected by the camera-depth capture (default `0`). |
| `NWN_SHADOWMAP_CASCADE_MATRIX_TRACE=1` | With trace mode only: classify native normal-pass 4x4 uniform uploads against the frozen area camera; no substitution. |
| `NWN_SHADOWMAP_CASCADE_MATRIX_TRACE_MAX=96` | Maximum matrix-upload records for Phase 3g. |
| `NWN_SHADOWMAP_CASCADE_LIGHT_CAPTURE=1` | With trace mode only: duplicate one selected native area bucket into private cascade layer 0 using recovered model/light transforms, then dump one light-space PGM; no visible rendering change. |
| `NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET=0` | Bucket selected by the light-space depth capture (default `0`). |
| `NWN_SHADOWMAP_CASCADE_LIGHT_BUCKETS=0,1,2,6` | Comma-separated native buckets accumulated into one light-space capture frame; overrides the single-bucket switch. |
| `NWN_SHADOWMAP_CASCADE_LIGHT_LAYER=2` | Cascade layer to capture and dump (`0`–`3`). The dedicated launcher defaults to layer 2, whose range is large enough to include nearby object casters. |
| `NWN_SHADOWMAP_STATIC_RECEIVER=1` | Phase 3i only, trace mode required: recapture opaque static bucket 0 every selected-area frame into the private static array and draw the frozen-context fullscreen red shadow diagnostic. This intentionally excludes dynamic and alpha casters. |
| `NWN_SHADOWMAP_DYNAMIC_RECEIVER=1` | Phase 3l only, requires the static receiver: duplicate the proven post-dynamic bucket into the independent dynamic array each frame and compose it with static receiver depth. |
| `NWN_SHADOWMAP_ALPHA_RECEIVER=1` | Phase 3p only, requires the dynamic receiver: accumulate the separately validated alpha bucket into that same dynamic array. |
| `NWN_SHADOWMAP_ALPHA_BUCKET=3` | Validated second custom-hair bucket for Phase 3p (default `3`). |
| `NWN_SHADOWMAP_STATIC_ALPHA_CAPTURE=1` | Phase 3q only: source-classified depth-only capture of static stock `AlphaDiscard` foliage; cannot be combined with a receiver. |
| `NWN_SHADOWMAP_STATIC_ALPHA_RECEIVER=1` | Phase 3r only, requires the static receiver: source-classified static stock `AlphaDiscard` foliage accumulates into the private static target. |
| `NWN_SHADOWMAP_STATIC_ALPHA_BUCKET=1` | Native bucket considered by the Phase 3q static foliage proof (default `1`). |
| `NWN_SHADOWMAP_DYNAMIC_CHARACTER_CAPTURE=1` | Phase 3j only, trace mode required: depth-only duplicate of native draws reached inside `Scene::RenderDynamicGeometry` into the dynamic cascade array. No receiver or visible change. |
| `NWN_SHADOWMAP_DYNAMIC_BUCKET_CAPTURE=1` | Phase 3k only, trace mode required: depth-only duplicate of one post-dynamic native bucket into the dynamic cascade array. No receiver or visible change. |
| `NWN_SHADOWMAP_DYNAMIC_BUCKET=2` | Candidate bucket for Phase 3k (default `2`). |
| `NWN_SHADOWMAP_SIZE=2048` | Light target resolution (power of two). |
| `NWN_SHADOWMAP_LIGHT=1` | Enable light-space replay. |
| `NWN_SHADOWMAP_CASCADE_TARGETS=1` | Create and validate opt-in PS4-style four-layer static/dynamic depth arrays.  This first stage does not redirect draws yet. |
| `NWN_SHADOWMAP_CASCADE_COPY=1` | Copy each validated replay depth map into dynamic cascade layer 0 on the GPU.  Requires `NWN_SHADOWMAP_CASCADE_TARGETS=1`; receiver output remains unchanged. |
| `NWN_SHADOWMAP_CASCADE_VERIFY=1` | Once, read and compare dynamic cascade layer 0 against the primary depth target.  Requires the cascade copy mode and causes one diagnostic readback. |
| `NWN_SHADOWMAP_COLORCAST_TEST=1` | Enable injected caster/receiver diagnostic branches. |
| `NWN_SHADOWMAP_FULLSCREEN_RECEIVER=1` | Enable injector-owned fullscreen red/green receiver. |
| `NWN_SHADOWMAP_DIR=x,y,z` | Override directional-light vector. |
| `NWN_SHADOWMAP_EXTENT=40` | Orthographic light half-width in metres. |
| `NWN_SHADOWMAP_DIST=25` | Light position offset along the light direction. |
| `NWN_SHADOWMAP_ORTHO=1` | Use the known-good orthographic light projection. |
| `NWN_SHADOWMAP_VIEW=4` | Use direct light matrix writes (known-good path). |
| `NWN_SHADOWMAP_LIGHTBUCKETS=...` | Comma-separated replay buckets; default known set is `0,1,2,3,6,11,13`. |
| `NWN_SHADOWMAP_DUMP=<seconds>` | Write `shadowmap_dump.pgm` once after the delay. |
| `NWN_SHADOWMAP_PROBE=1` | Probe draw buckets after a delayed area load. |
| `NWN_SHADOWMAP_STENCIL_TRACE=1` | Observe engine stencil passes only; does not alter them. |
| `NWN_SHADOWMAP_UNIFORM_TRACE=1` | Log a bounded set of normal/light matrix uploads. |
| `NWN_SHADOWMAP_RECEIVER_TRACE=1` | Log receiver camera-matrix deltas. |

`NWN_SHADOWMAP_VERTEX_REPROJECT=1` and
`NWN_SHADOWMAP_LIGHT_CASTER_MATRICES=1` are retained only for controlled
experiments.  They are known to produce incorrect/sparse maps and should remain
unset in normal tests.

## Architecture

```text
selected area Scene::Render
        |
        +-- engine normal camera render --> scene depth
        |
        +-- injector replay (visible buckets, light matrices)
        |       --> light depth + R32F color target
        |
        +-- GPU detached-copy bridge --> stable light color texture
        |
        +-- fullscreen receiver (optional)
                scene depth + area camera VP inverse + light VP
                --> diagnostic red / green / blue output
```

The replay intentionally uses NWN's own draw buckets instead of parsing MDLs or
maintaining a second scene graph.  This is why it naturally includes terrain,
placeables, creatures, and material state that the engine has already prepared.

## Important findings and rejected paths

### Alpha sorting

The original project was a per-triangle alpha-sort injector.  It remains in
`files/alphasort/` for reference but is not built.  It did not solve intersecting
hair-card transparency, and its `RenderFlat` detour conflicts with replaying
draws for shadow-map work.

### Standalone bucket calls

Calling `RenderDrawBucket` by itself is unsafe for all buckets and can fault.
Replaying them in the established `Scene::Render` sequence, after the engine's
normal render, is the stable route.

### Engine camera mutation

Changing the engine camera object directly caused camera snaps, disappearing
characters, missing UI, and crashes.  The accepted approach writes only the
renderer matrix-stack entries for the temporary light replay and restores all GL
state afterward.

### CPU shadow transport

The first fully working red/green route read the light map to CPU and uploaded an
R32UI mirror roughly once per second.  It was useful as a correctness reference,
but stepped visibly and was too expensive for per-frame use.

### Live FBO sampling

Sampling the active depth/color attachments directly from receivers gave
all-lit/incorrect results despite valid dumps.  A detached copy texture is
required; the GPU color bridge now does that without CPU readback.

### Vertex reprojection

Several attempts rebuilt caster positions in injected vertex shaders from
`m_vp_inv`, then a captured normal camera VP inverse.  They yielded sparse blobs,
invalid color ranges, or nearly empty targets.  The light replay's existing
matrix uploads were subsequently traced and proven correct, so this route is
disabled by default.

## Diagnostics collected so far

- Standalone probe after area load showed geometry in buckets
  `0 1 2 3 6 11 13`.
- Light target dumps contain tree/terrain caster silhouettes.
- Color target validation has reached, for example:

  ```text
  R=0.1041927..1.0000000 nonwhite=848121/4194304
  vs-hw mismatch=0 max-delta=0.0000000
  ```

  This proves the CPU reference and hardware color map agree exactly for that
  frame.
- During movement, normal camera `m_view_inv` can differ from a previously
  latched inverse by 8–13 world units.  That correlates with the observed
  detach/leak and motivated scene-scoped camera capture.
- Stencil tracing identifies the engine's plane pass as program 200, attached
  to shaders 198/199 in the observed run; those IDs are runtime-specific and
  must not be hard-coded.

## PS4 renderer reference (inspected 2026-08-09)

The local PS4 dump at
`<PS4-reference-dump>/PS4/CUSA15938/CUSA15670/` confirms that
NWN:EE has a real production shadow-map renderer on that platform.  This is
valuable architectural evidence for the Linux injector.  It must be treated as
a reference for behavior and naming, not as code to copy or execute on Linux.

Relevant files:

- `ovr/GNM.technique` — PS4/GNM technique declaration.
- `ovr/OpenGL33.technique` — portable GL-style technique/shader declarations.
- `ovr/D3D.technique` — D3D-style technique/shader declarations.
- `eboot.bin` — contains renderer/configuration identifiers such as
  `CascadeShadowSetup`, `cascadeShadowsStatic`, and `cascadeShadowsDynamic`.

The complete recovered specification, source cross-references, Linux mapping,
and explicitly non-recoverable CPU-side details are in
[`PS4_CASCADE_REFERENCE.md`](PS4_CASCADE_REFERENCE.md).

Confirmed PS4 pipeline design:

- Four directional-light cascade layers at 2048² in the PS4/GNM and D3D paths
  (the portable OpenGL33 fallback declares 1024²), stored as depth texture
  arrays with comparison sampling and linear filtering.
- Separate **static** and **dynamic** cascade arrays.  Receiver visibility is
  the minimum of both comparisons, so static geometry need not be redrawn with
  every dynamic update.
- Separate caster passes for opaque, transparent, skinned, and skinned
  transparent geometry.  This directly addresses alpha-cutout foliage and
  animated creatures rather than treating all casters as solid geometry.
- Depth-only framebuffer passes: depth test/write on, color writes off,
  back-face culling, and polygon offset (`factor 2`, `units 4` in the declared
  technique) to control acne.
- Hardware comparison sampling (`sampler2DArrayShadow` / comparison sampler),
  plus a soft-shadow helper.  The architecture performs the comparison in the
  material/deferred lighting pipeline rather than reconstructing all receivers
  in an after-the-fact debug fullscreen pass.
- Point-light shadow maps also exist, but directional cascades are the relevant
  first target.

Implication for this injector: the present one-map fullscreen red/green path is
a useful proof harness but is not the final architecture.  The PS4 reference
supports moving next toward an area-scoped directional cascade system with
separate static/dynamic targets, receiver sampling in the normal material path
or a stable world pass, and explicit transparent/skinned caster handling.

The first Linux implementation step is present behind
`NWN_SHADOWMAP_CASCADE_TARGETS=1`: it allocates four-layer 2048²-equivalent
(or `NWN_SHADOWMAP_SIZE`-sized) `GL_TEXTURE_2D_ARRAY` depth maps for static and
dynamic cascades, configures comparison sampling, and validates a depth-only
framebuffer attached to layer zero of each.  It deliberately leaves the
established single-map replay untouched until target creation is confirmed on
the user's driver and a reliable static/dynamic classification point is found.
Its sampler and wrap state now match the PS4/GNM declaration: `GL_LESS`, linear
filtering, and clamp-to-edge.

The next opt-in bridge is `NWN_SHADOWMAP_CASCADE_COPY=1`: after the existing
single-map replay is validated with its occlusion query, the injector copies
that depth attachment directly into dynamic cascade layer zero.  It is a GPU
copy, not another replay, so it neither adds CPU readback nor changes current
receiver rendering.  A successful test logs:

```text
[shadowmap] dynamic cascade layer 0 copied from validated primary light depth
```

## HISTORICAL -- Next steps

> **SUPERSEDED.** All four items are done. Cascades, the receiver and the caster split all shipped and are confirmed in game on both platforms.


1. Verify the scene-scoped fullscreen camera-inverse fix while orbiting,
   panning, and zooming.
2. If it still drifts, record the exact area scene's camera VP and scene-depth
   values in the same draw phase, rather than introduce another transform path.
3. Use the PS4 reference to design the first Linux cascade: depth-only target,
   comparison sampler, bias, and explicit static/dynamic caster separation.
4. Once the debug mask remains stable, replace red/green output with a shadow
   factor and composite it into the final world render.
5. Limit the composite to world rendering and preserve UI, menus, fog, and
   existing engine state; compare against stencil shadows before considering an
   opt-in replacement.

## Safety / recovery

The injector is local to this installation.  To return to unmodified rendering,
remove the `NWN_SHADOWMAP_*` environment variables (or launch with
`NWN_SHADOWMAP_OFF=1`).  No game executable, game resource, or Proton-prefix
file is changed by the shadow-map implementation.

## Refactored development tree

The active modular working copy is `csm_claude/refactored/`. Launch it from
that directory with:

```bash
cd "$NWN/bin/linux-x86/csm_claude/refactored"
./run-dev.sh
```

The launcher rebuilds the injector unless `NWN_DEV_NO_BUILD=1` is supplied. Its
nested path resolver finds the real `linux-x86/nwmain-linux`; an unusual layout
can set `NWN_SHADOWMAP_GAME_DIR` explicitly. The root implementation is now
split into bounded same-translation-unit subsystem modules documented in
`REFACTORING.md` and `SYMBOL_INDEX.md`. This split is compiler-token-equivalent
to the pre-refactor source and does not add multi-light behavior.
