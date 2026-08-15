# Linux renderer map

This is the working map of the **exported renderer-facing symbols** in the
current `nwmain-linux`. It constrains future injector work to observable engine
boundaries rather than guessing from a fullscreen effect.

It is deliberately not a catalogue of every dynamic symbol in the executable:
the binary exports 30,613 functions, of which 547 belong to renderer-relevant
classes. Most are unrelated UI, resource, gameplay, or utility methods. The
symbols below cover the scene/camera/light/matrix path needed for directional
shadow maps.

## Confirmed control points

| Area | Exported functions | What is established |
| --- | --- | --- |
| Frame boundary | `Camera::Render(bool)`, `Camera::RenderScene()`, `Scene::Render()` | Verified while orbiting/panning/zooming: one selected full-viewport area scene has a stable entry camera contract. `Scene::Render()` is the safe detour and exact immutable-context capture boundary. |
| Scene content | `Scene::AddPartsToDrawBuckets()`, `Scene::RenderDrawBucket(int)`, `Scene::RenderSinglePass()`, `Scene::RenderDynamicGeometry()` | The visible material geometry reaches the established bucket replay through this path. Bucket membership has been observed in a loaded area, not guessed. |
| Legacy shadows | `Scene::RenderShadows(int,bool)`, `RenderStaticShadows()`, `RenderDynamicShadows()`, `RenderShadowPlanes(int,int)`, `DoGobShadows(PartLight*)` | This is the existing stencil-volume system. Static/dynamic routines emit generated shadow volume geometry, not original caster meshes. They are timing/classification evidence only. |
| Light selection | `LightManager::GetShadowLights(int)`, `PrioritizeShadow()`, `PrioritizeLights(Vector const&)`, `EnableLightsDynamicOnly()` | Phase 6a observes the returned CExoArrayList identity/count and opaque candidate pointers only. Local-light layouts, attenuation, and shadow projection types remain unmapped. |
| Camera state | `Camera::GetClipDist`, `SetClipDist`, `SetViewAngle`, `SetViewPort`, `GetPosition`, `GetOrientation` | These are the legitimate inputs to a cascade setup. The injector already captures projection/camera state inside the selected gameplay `Scene::Render`. |
| Matrix state | `aurMatrixStack::Perspective`, `Orthographic`, `SetMatrix`, `ApplyMatrix`, `SetIdentity`; `RenderInterface::SetViewTransform`, `SetPerspectiveTransform`, `SetOrthoTransform`, `SetMatrixMode`, `PushCurrentMatrix`, `PopCurrentMatrix` | The known-good light replay writes/restores this renderer state; mutating the `Camera` object itself is prohibited because it caused camera/UI corruption. |
| GL/FBO state | `RenderInterface::{Gen,Bind,Delete}Framebuffer`, `SetFramebufferRenderTarget`, `CopyFramebuffer`, `SetColorMask`, `SetDepth*`, `SetPolygonOffset`, `SetActiveTextureAndBind` | These confirm the engine-level API surface needed for injector-owned depth arrays and fully restored replay state. The current injector uses direct GL only where necessary and restores state. |

## What is already documented

- [README.md](README.md) records the current detours, observed draw buckets,
  diagnostics, launch switches, known motion failure, and tested fallbacks.
- [PS4_CASCADE_REFERENCE.md](PS4_CASCADE_REFERENCE.md) recovers the production
  cross-platform cascade target/pass/receiver contract.
- `nwn_shadowmap.cpp` records the exact resolved symbol names and the evidence
  around failed paths (standalone bucket calls, camera mutation, and stale
  matrix sources).

## Verified runtime trace (2026-08-09)

`NWN_SHADOWMAP_TRACE=1` has now completed a normal-rendering in-game orbit
capture. The selected area scene follows this observed order:

```text
Scene::Render entry
  PrioritizeShadow
  RenderSinglePass / GetShadowLights / early buckets
  RenderDynamicGeometry / remaining visible buckets
Scene::Render exit
```

At entry, the captured projection remains stable for a given camera setup and
the frozen view inverse correctly follows the real camera. Crucially, the live
matrix-stack view hash can change **before the same Scene::Render exits** during
orbit/pan/zoom. This directly explains the old receiver failure: it combined
light/depth work originating at frame entry with a later, unrelated stack view.

`ShadowFrameContext` now snapshots scene, camera, viewport, eye, projection,
view, and rigid view inverse at area-scene entry. It is logging-only under the
trace launcher. `run-shadowmap-cascade-math.sh` uses that immutable snapshot to
derive four split frusta, fit directional-light orthographic matrices, and snap
their light-plane centres to a virtual 2048² texel grid. It makes no GL call and
cannot affect NWN rendering. The exported sun vector is used only when nonzero;
early/empty engine sun state falls back to the known `(0,0,-1)` diagnostic
direction so the validation does not silently lose every frame.

The first cascade-math run recovered valid ordered clips
`3.7200 / 8.2501 / 16.9720 / 45.0009` at a 45-unit camera far plane. Its
stationary frames have repeatable centres/extents/VP hashes (up to expected
texel-grid boundary changes). That validates the frozen camera and fit math,
not the light direction: `m_lightAreaDiffuseDirection` was zero in the tested
area. This is consistent with NWN area lighting being sun-MDL driven and with
the current dynamic-area-lighting support.

`NWN_SHADOWMAP_LIGHT_VECTOR_TRACE=1` is the next bounded observation layer. It
patches only the GLEW `glUniform3f`/`glUniform3fv` function pointers during
trace mode and forwards every call unchanged. It logs normalised vec3 uploads
from the selected area’s normal material pass so a repeated real lighting
direction can be identified without decoding private light-manager layouts. In
the first run, one unit vector was repeated across eight material programs but
rotated with the camera, strongly indicating a view-space sun direction. The
trace now also writes each candidate transformed by the frozen view inverse;
only a camera-invariant world result can become a cascade light direction.
That test succeeded: every normal material program resolved to world
`(0.43322,0.48738,0.75814)`. The established NWN convention says that is
scene-to-light, so the cascade’s light-to-scene direction is
`(-0.43322,-0.48738,-0.75814)`. The trace promotes this only after agreement
from two distinct programs; the following frame's non-rendering cascade math
uses it, allowing the fitting rule and sign to be validated before target work.

### Phase 3c target validation (2026-08-09)

`run-shadowmap-cascade-target-validate.sh` keeps the Phase 1 trace branch in
control and layers a private target-layout check on top. Once the normal pass
has promoted the area-light direction, it recomputes the frozen-context fit,
allocates separate `GL_TEXTURE_2D_ARRAY` depth textures for static and dynamic
cascades, and attaches **each** of four layers to its own private FBO. It then
clears each private depth layer exactly once and verifies the far-depth contents
by readback. This stage renders no geometry and never enters the legacy
depth/replay, shader injection, or receiver paths. It is therefore the first
GL target-write test that is mechanically separated from the unstable red/green
prototype.

### Phase 3e in-sequence geometry census

The same launcher now emits a bounded `[shadowmap][geom]` summary for each of
the first native area `RenderDrawBucket` calls: draw-call count, indexed/array
vertex totals, and the active material programs. It observes the original GL
draw imports while NWN performs the normal pass; it neither duplicates nor
redirects those draws. This identifies the actual caster-bearing engine work
that a later depth-only pass must reproduce, without returning to unsafe
standalone `RenderDrawBucket` calls.

### Phase 3f native camera-depth duplication

`run-shadowmap-cascade-camera-capture.sh` is the first controlled geometry
write after the census. It selects native area bucket `0`, lets each original
draw reach the normal framebuffer once, then repeats that exact GL call into
private dynamic cascade layer `0` while the engine's camera, program, VAO and
uniform state are still live. The target uses a viewport with the normal camera
aspect ratio and is cleared once for the capture frame. Full state restoration
follows every duplicate call.

The phase writes `shadowmap_cascade_camera.pgm` once and does not touch the
matrix stack, light transform, material source, receiver, or visible framebuffer.
It is therefore a strict test of the mechanism that the earlier standalone
`RenderDrawBucket` PGM could not prove: in-sequence direct geometry duplication.

### Phase 3g normal-pass matrix trace

`run-shadowmap-cascade-matrix-trace.sh` hooks only the GLEW
`glUniformMatrix4fv` dispatch pointer while trace mode is active. For each
selected-area normal-pass upload it compares the passed matrix with the frozen
projection, view, view inverse and projection-view product, records the program
and uniform location, and forwards it untouched. This identifies the exact
camera matrix locations that a future light-space duplicate must temporarily
replace, while retaining each program's per-object matrices.

### Phase 3h native light-space depth duplication

`run-shadowmap-cascade-light-capture.sh` repeats the proven Phase 3f native
bucket duplication, but only after the normal `m_mv` and `m_proj` uploads have
been recorded for the active program. The duplicate derives
`model = frozenViewInverse * normal_m_mv`, uploads
`cascadeLightView[0] * model` and `cascadeLightProjection[0]`, performs the
native draw into private dynamic layer 0, and restores the exact two normal
uniform values before returning. It deliberately does not change the engine
matrix stack, camera object, material source, receiver, or visible framebuffer.

It writes one `shadowmap_cascade_light.pgm`. A capture can accumulate a
comma-separated set of observed buckets before dumping at `Scene::Render` exit;
the dedicated launcher uses `0,1,2,6` and cascade layer 2 so nearby object
casters appear alongside the terrain. This remains a depth-only proof with no
receiver, so it is not a shadow implementation yet.

The same phase now also brackets `Scene::RenderDynamicGeometry`, the distinct
native route used for animated character geometry. Its counter is reported as
`dynamic-scopes` in the PGM log. Alpha-tested card/foliage capture remains a
separate validation step.

### Phase 3i static-only fullscreen receiver

`run-shadowmap-static-receiver.sh` is the first intentionally visible Linux
shadow-map slice. It is not connected to any `.mtr` or `.shd`: the injector
copies the completed selected-area depth buffer, reconstructs world position
with `ShadowFrameContext::viewProjectionInverse`, and samples the private
static cascade array with the matching `CascadeMathState::lightVP[layer]`.
Both matrices and the duplicate light depth carry the same area-frame serial.

The scope is deliberately only opaque static bucket 0, rendered into the
private **static** array every area frame. The output is a dark-red shadow mask
so pan/zoom stability is unambiguous. It excludes `RenderDynamicGeometry` and
all alpha/card validation. If this mask drifts while the camera moves, the
failure is in the frozen camera reconstruction or cascade fit; it is not
per-material receiver transport.

### Phase 3j dynamic character-stage capture

`run-shadowmap-dynamic-character-capture.sh` is an isolated depth-only probe
of `Scene::RenderDynamicGeometry`. While that exact engine routine runs, the
same native duplicate bridge writes only to the private **dynamic** array;
bucket capture is explicitly disabled and there is no receiver. The first log
reports the delta attempts and duplicate draws observed inside the stage. This
keeps the validated static receiver untouched while establishing whether the
animated player is submitted through this named renderer boundary or a later
native draw path.

### Phase 3k post-dynamic bucket capture

The Phase 3j counter established that `Scene::RenderDynamicGeometry` queues
dynamic work but makes zero native GL draws in the Linux build. The normal-pass
census immediately afterwards records bucket `2` as the first live candidate
(programs `173,167,224`). `run-shadowmap-dynamic-bucket-capture.sh` therefore
captures just this bucket into the dynamic cascade array and emits a PGM. It
does not compose with the static receiver yet: the image must first show the
player before bucket 2 is accepted as the dynamic source. This is the controlled
replacement for the old red/green path's broad `{0,1,2,3,6,11,13}` replay.

### Phase 3l static + dynamic receiver

Phase 3k confirmed bucket `2` produces seven recovered native depth draws and
contains the player. The combined diagnostic therefore keeps the successful
bucket `0` static capture in `g_cascadeStaticTex` and captures bucket `2` into
`g_cascadeDynamicTex`. Each target owns a separate per-frame clear, serial, and
draw counter. The fullscreen receiver samples both with the same reconstructed
world point and the same frozen camera/light matrices; either depth failure
marks the pixel red. It deliberately does not reuse the former all-bucket
receiver, which mixed target ownership and caused camera-motion drift.

### Phase 3p validated alpha receiver extension

The normal-pass source mapping and Phase 3o depth crop establish that bucket `3`
is a second `c251_hair` custom-shader pass and that the duplicate draw retains
its own alpha discard. The opt-in Phase 3p launcher accumulates bucket `3` into
the **dynamic** private depth array after bucket `2`; it does not touch the
static bucket-0 array, edit a material, or broaden the replay to unclassified
buckets. This preserves the per-frame clear/serial ownership that made Phase 3i
and Phase 3l camera-stable. The fullscreen receiver still uses the frozen entry
camera inverse plus the same light VP for all three accepted buckets `0+2+3`.

### Phase 3q static foliage alpha proof

The tree issue cannot be inferred from the accepted dynamic hair path:
`mzmap_006` is a stock renderer material, not the prior custom MZLM material.
Phase 3q observes shader source at compile time, identifies the compiled stock
`NO_DISCARD 0` + `fAlphaDiscardValue` fragment signature, resolves that source
against the current program's attached shader objects at draw time, and reads
the live discard threshold. It targets the observed static bucket `1`, writes
an enlarged PGM, and has no receiver. This determines whether foliage is absent
from the caster map or only absent from the final receiver composition.

### Phase 3r static foliage receiver

Phase 3q captured foliage and an unrelated alpha-discard placeable in the same
private PGM, so coverage is not tileset-specific. Phase 3r keeps the source
classifier active only while native bucket `1` executes, then duplicates only
accepted draws into the **static** private array. Opaque bucket `0` remains
unchanged in that same target. Buckets `2+3` remain independently owned by the
dynamic target, preserving the camera-stable frozen-context contract.

The Phase 3r video test exposed a distinct limitation: all four accepted paths
are intercepted from NWN's already normal-camera-culled draw stream. Their
shadow transforms are stable, but an object that leaves the normal camera's
submission frustum can stop contributing to the private depth map even though
its projected shadow remains visible. This is not the earlier moving-camera
matrix defect. A final shadow implementation needs a light/cascade-aware caster
submission route; the existing stencil path is useful to observe selection and
timing, but it produces stencil volumes rather than original caster meshes.

### Phase 4 cascade coverage diagnosis and contract

Phase 3r currently captures and receives only layer `2`, even though the
injector owns valid four-layer static/dynamic arrays. The logged frozen fit is
`c0=3.7200`, `c1=8.2501`, `c2=16.9720`, `c3=45.0009`; layer 2 is therefore a
deliberately middle-distance proof, not full camera coverage. The 32×32-area
test showed red foliage shadows far into the visible scene, but this is not
evidence that fog or area size drives sampling: the receiver never reads fog.
It reflects NWN submitting more visible native static draws, while all sampled
points still happen to project into the single selected layer.

Phase 4a is implemented as the opt-in depth-only launcher
`run-shadowmap-four-cascade-capture.sh`. It retains the accepted `0+1` static
and `2+3` dynamic caster paths, duplicates every accepted native draw into all
four matching layers, and clears/tracks each target-layer pair independently
per frozen area-frame serial. The visible receiver is deliberately suppressed.
The expected evidence is eight PGM files,
`shadowmap_cascade_{static,dynamic}_c0.pgm` through `c3.pgm`, and matching
`[shadowmap][csm] multi capture` records in `shadowmap-phase1.log`.

This validates target/layer ownership only. It inherits Phase 3r's normal-camera
caster culling, so a successful Phase 4a run is not authorization to implement
the final receiver until non-camera-frustum caster submission is mapped.

**First in-game validation:** all eight files were produced at area frame 2.
Static layers `c0..c3` each recorded six duplicate draws; dynamic layers each
recorded nine. All eight had non-uniform depth, proving the separate static /
dynamic clear and per-layer freshness counters work. This establishes the
cascade storage contract, not full caster visibility coverage.

### Phase 4b: culling census before receiver selection

The immediate next step is deliberately not another visible receiver. The
opt-in `run-shadowmap-caster-cull-trace.sh` path hooks
`ManageSceneBSP(Scene*)`, calls the original unchanged, and then only reads the
counts at `+8` of the engine's `meshshadowbucket` and `staticshadowbucket`
`CExoArrayList`s plus `countculledpart`, `countculledshadows`, and
`countbackshadows`. No draw, target, matrix, shader, or GL state changes.

Disassembly resolves the causal chain: `ManageSceneBSP` traverses the scene and
calls `ProcessTriMeshParts(..., List<Plane>&)`; that routine calls
`PartOutside` with the normal camera-plane list **before**
`AddPartToMeshBuckets(PartTriMesh*)`. The latter unconditionally appends the
already accepted original mesh to `meshshadowbucket`. Therefore the Phase 3r
duplicate cannot contain a caster the normal camera rejected. Meanwhile
`staticshadowbucket` contains `PartProjection` stencil volumes, confirmed by
the native stencil rendering path, so it cannot be substituted as a material
depth caster source.

**In-game census result (2026-08-09):** after the normal trace's frame-90 cap,
the selected area's `meshshadowbucket` count moved repeatedly from 4 to 15 and
back down as the user orbited. The observed `staticshadowbucket` count and the
three exposed cull counters were zero at this post-BSP observation point. The
varying original-mesh count is direct live confirmation of the normal-camera
frustum limitation; it is not inferred from the red diagnostic.

### Phase 4c: full static-BSP candidate census

`run-shadowmap-full-bsp-census.sh` is a distinct read-only probe. In the same
post-`ManageSceneBSP` hook, it calls the binary's resolved plain traversal:

```cpp
BSPTraverse(BSPNode* root, void (*callback)(BSPNode*, void*), void* user)
```

Unlike `BSPTraverseVolumeApprox`, this routine has no camera position or
`List<Plane>` input. Disassembly shows a simple recursive visit of each node,
and NWN's own `ManageSceneBSP` callback establishes the indirection: it first
loads `nodedata*` from `BSPNode +0x70`, then `ProcessTriMeshParts` consumes the
resident original-mesh array/count at `nodedata +0x20/+0x28`. Its separate
primary mesh pointer lives at `nodedata +0x90`. The probe therefore only sums
those fields, emits `[shadowmap][fullbsp] nodes=... tri-candidates=...`, and
returns.
It never stores a candidate pointer, invokes an engine mutator, modifies a
bucket, or calls GL.

The required result is a stable `tri-candidates` value while the adjacent
normal `meshshadowbucket` observation changes under orbit/pan. That proves the
full static candidate population is available to a later explicitly designed
light-frustum path. It does not mean that replaying it through current material
state is safe; that needs its own transform/material submission mapping first.

**Observed 2026-08-09:** `BSPTraverse` visited 403 nodes in the selected world
area. All 403 resolved through `node +0x70` to valid nodedata. Their stable
source population was 15 regular array entries plus 16 primary part entries,
for 31 static candidates and zero invalid payloads. At the same time the normal
post-BSP `meshshadowbucket` varied from 2 through 15 as the camera moved. The
candidate source is therefore demonstrably world-owned and camera-independent.

### Phase 4d: native full-static submission (implemented; lifecycle-corrected re-test pending)

The full candidate pointers cannot be rendered by guessing a `PartTriMesh`
vtable entry: that bypasses NWN's material routing and per-part model transform.
Two resolved helpers provide the native route instead:

```text
AddPartToMeshBuckets(PartTriMesh*)       -> material-indexed mesh buckets
Scene::AddPartsToDrawBuckets()           -> native draw manager + model transforms
```

The correct injected source point is immediately after `ManageSceneBSP()`;
`Scene::Render()` subsequently calls `Scene::AddPartsToDrawBuckets()` itself:

```text
ManageSceneBSP(Scene*)
  [Phase 4d appends non-culled full-BSP candidates with AddPartToMeshBuckets]
RenderInterface::CheckAndUpdateUniformDirtyDataForFrame(...)
Scene::AddPartsToDrawBuckets()
Scene virtual render pass -> RenderDrawBucket(...)
```

The original Phase 4d manually called `Scene::AddPartsToDrawBuckets()` plus a
direct `RenderDrawBucket()` loop at Scene entry. It rendered frame 2, then
aborted at `Scene::AddPartsToDrawBuckets()+0xd0a` on frame 3. That helper resets
and consumes engine-owned lists whose full lifecycle is not valid at entry, so
manual building/replay is forbidden. The corrected path only appends candidates;
NWN builds and renders its own manager at its normal point, where the existing
GL wrapper gets each exact normal `m_mv`/`m_proj` and immediately duplicates it
into the private static cascade. This avoids both the crash and the historic
post-render cached-matrix camera-following error. Its success criterion is a
`[shadowmap][fullsubmit]` record with `submitted=31` and
`pending-meshshadow=...`, followed by red static shadows that remain
world-anchored while orbiting.

**Confirmed (2026-08-09):** the corrected implementation ran the bounded
90-area-frame trace with a stable 31-candidate submission every frame, no crash,
and no static-caster disappearance. The next safe step is
`run-shadowmap-full-bsp-four-cascade-capture.sh`: it combines that source with
the existing depth-only four-layer fan-out. It must produce non-uniform full
static PGM layers before any visible camera-depth cascade selector is enabled.

### Phase 4f: visible frozen-depth static selector (in-game validated)

`run-shadowmap-full-bsp-csm-static.sh` enables the only visible CSM receiver
currently permitted. It uses the completed area depth buffer plus the immutable
entry `viewProjectionInverse` to reconstruct a world point. The shader then
transforms that point by the same frozen camera `view` used by
`update_cascade_math_from_context()`, derives `-view.z`, selects the first of
the four `clipFar` values that contains it, and uses that layer's corresponding
`lightVP`. There is no mutable post-render matrix dependency.

The output remains static red diagnostic only, with deliberately hard split
boundaries. It was validated in game: full-static shadows remained world-anchored
while orbiting, panning, zooming, and crossing split ranges. The next controlled
extension is dynamic character capture; overlap blending, PCF/bias tuning, and
final lighting modulation remain later work.

### Phase 4g: bucket-2 dynamic CSM extension (implemented; test pending)

`run-shadowmap-full-bsp-csm-static-dynamic.sh` adds only the previously proven
character body bucket (`2`) to Phase 4f. The original bucket draw is duplicated
into all four dynamic array layers and the fullscreen receiver samples the same
frozen-camera depth-selected layer used for static depth. It requires fresh
depth in all four static and dynamic layers in the current scene serial. This
prevents stale player depth from recreating camera-following artefacts. Bucket
`3` alpha/hair remains excluded until this narrow body test is confirmed.

### Phase 4h: dynamic alpha/card CSM extension (implemented; test pending)

`run-shadowmap-full-bsp-csm-all-casters.sh` adds the already mapped dynamic
alpha/card bucket (`3`) after Phase 4g's dynamic body bucket (`2`). Both use the
same dynamic layer array: bucket 2 creates fresh per-frame depth and bucket 3
adds its original alpha-discard depth after it. The fullscreen receiver still
chooses a single layer solely from frozen camera view depth. This is the complete
currently-mapped caster set (`0 + static-alpha 1 + 2 + 3`) and remains a
red-only, hard-split diagnostic.

**Confirmed (2026-08-09):** Phase 4g body and Phase 4h alpha/card extensions
were both stable in-game. All mapped caster classes contribute correctly, with
visually acceptable cascade resolution/range and no camera-relative movement.

### Phase 5a: presentation composite (validated)

`run-shadowmap-full-bsp-csm-shadows.sh` is an opt-in compositor over the proven
Phase 4h depth contract. Its receiver discards lit pixels as before, but emits
black with configurable alpha instead of opaque red for shadowed pixels. The
injector explicitly saves/restores RGB/alpha blend factors and equations, plus
the normal receiver state, so it cannot inherit or leak a blend mode into NWN.
This is deliberately not a replacement for material-aware lighting yet; it is
a stable, easily A/B-tested darkening pass before cascade transitions. The
linear comparison samplers already provide hardware 2x2 PCF.

### Phase 5b: injector-owned settings overlay (deferred)

The first ReShade-style panel is toggled by `Ctrl+Shift+F11` from a loaded CSM
area. It is drawn after the receiver with independently saved/restored GL state
and an embedded 5x7 font atlas, avoiding any external UI library in NWN's
process. It edits output mode, composite strength, receiver bias, and cascade
lambda live via Ctrl+Shift+arrow/Enter controls. It deliberately polls SDL
keyboard state without consuming events; a mouse-interactive overlay must wait
for a safe event-dispatch interception rather than guessing at NWN input state.

The hotkey state is confirmed, but the panel is currently not visible despite
compiling. Keep the overlay isolated from the accepted shadow pass until its
GL-state gate is diagnosed separately.

### Phase 5c: receiver-only cascade overlap blend (implemented; test pending)

`run-shadowmap-full-bsp-csm-soft-shadows.sh` retains the accepted Phase 5a
caster/receiver contract and sets `NWN_SHADOWMAP_CSM_BLEND=0.75`. During the
last overlap width before each split, the receiver samples the next valid layer
and smoothly cross-fades its combined static/dynamic shadow result. A blend
width of zero is exactly the Phase 5a hard-split path. No capture transform,
source, target, or scene selection changes in this phase.

### Phase 5d: receiver-only PCF filter (implemented; test pending)

`run-shadowmap-full-bsp-csm-filtered-shadows.sh` adds
`NWN_SHADOWMAP_CSM_PCF_RADIUS=0.75` to the accepted Phase 5a contract (and keeps
the Phase 5c overlap). It performs nine stable shadow comparisons in a 3x3
footprint around the projected map coordinate, combining static/dynamic depth
at each tap. This deliberately changes the visual silhouette edge; Phase 5c
alone does not. Radius zero preserves only the array textures' normal hardware
2x2 comparison filtering.

Only after that non-camera-frustum caster submission route is proved may the
fullscreen diagnostic choose a layer by reconstructed camera depth and use the
matching `lightVP`. Hard boundaries are the first test. Overlap blending, PCF,
final darkening, and user-facing range/resolution controls follow only after an
anchored multi-layer proof **and** that caster route.

### Phase 3m alpha/card candidate probe

The normal census records bucket `3` directly after the accepted player bucket:
two native draws with program `215`. It is captured alone into the dynamic depth
array by `run-shadowmap-alpha-bucket-capture.sh`. The PGM must show proper
alpha-cutout silhouettes before this bucket is permitted to affect the combined
receiver; a solid card would create the wrong foliage shadows.

### Phase 3n fragment-source alpha classification

Bucket 3's isolated PGM did not show foliage or the player's hair cards. The
injector can already intercept NWN's `glShaderSource` dispatcher, so the next
read-only probe dumps real fragment sources to `/tmp/nwn_shadow_fragment_<id>.glsl`.
It also runs the bounded normal geometry census and logs
`bucket -> program -> attached shader objects` as `[shadowmap][alpha]` lines,
so the fragment source is identified at its actual native draw point. This hook
does not mutate shader text and does not run a depth duplicate or receiver.

#### Verified material correlations (2026-08-09)

The maintainer supplied two known alpha-discard materials and Phase 3n tied
them to live native programs:

| Material | Behaviour | Fragment shader object | Native program / bucket |
| --- | --- | --- | --- |
| `mzmap_008.mtr` | static custom MZLM alpha clip; not the foliage material | `190` | program `191`, bucket `0` |
| `mzmap_006.mtr` | stock tree foliage alpha discard (`fAlphaDiscardValue`) | runtime-classified | expected program `152`, bucket `1` |
| `c251_hair.mtr` | dynamic palette hair; custom `fs_plt_tinter` forces its own discard/dither path | `223` | program `224`, bucket `2` |
| `c251_hair.mtr` | second dynamic hair variant/pass | `214` | program `215`, bucket `3` |

This is source-level evidence, not a heuristic: fragment `190` contains the
`mzlm_fs` `PunchThrough` uniform but belongs to `mzmap_008`, which is not the
tree foliage. `mzmap_006` instead selects the stock `NO_DISCARD 0` shader
variant with `fAlphaDiscardValue`; Phase 3q resolves its per-launch fragment
and program identity at runtime. `214` and `223` contain the custom hair
parameters and `FORCE_ALLOW_DISCARD`. The dynamic receiver currently captures
bucket 2; bucket 3 must be added only after its separate depth capture proves
that its alpha discard survives the duplicate pass.

### Phase 3o enlarged alpha-card depth view

`run-shadowmap-alpha-bucket-capture.sh` now keeps the exact Phase 3m bucket-3
duplicate intact and writes `shadowmap_alpha_cards.pgm` in addition to the
normal full cascade PGM. It finds the non-clear depth bounding box, adds a
12-texel margin, and nearest-neighbour enlarges it toward 512 pixels. This is
diagnostic-only: no shader is modified, no receiver is enabled, and the live
framebuffer is untouched. It answers whether the verified hair pass' original
fragment `discard` survives the duplicate before bucket 3 is merged anywhere.

## Next implementation boundary

1. Identify and validate the normal-pass directional-light vector with the
   read-only uniform trace; do not promote the `(0,0,-1)` fallback to a
   production light.
2. Validate the PS4-style static/dynamic depth arrays only after that fit and
   direction are accepted, attaching every layer in turn with complete GL-state
   restoration. Then add a first clear-only depth layer, still without replay.
3. Establish original normal-mesh caster collection; stencil-volume routines
   remain classification/timing evidence only.
4. Add a receiver that consumes the same frozen context for its current frame;
   do not reuse the legacy last-seen inverse.
5. Move the validated factor into directional lighting and retire the
   red/green diagnostic only after the normal composite is correct.

## Decision

Do not inspect or hook all 30,613 exported functions. The focused trace has
already supplied the needed frame contract; the appropriate next work is its
isolated cascade-math validation, not another fullscreen experiment.

## Symbols and engine facts added 2026-08-09/10 (csm_claude fork)

### PartLight layout (validated in game)

Recovered by cross-referencing four independent engine accessors that all agree
(`PartOutside` at 0x47a1e0, `GetLightAdjustedRadius` 0x4c0c10,
`GetLightAdjustedColor` 0x4c0b60 / `PartLight::Mat` 0x450510, and
`LightManager::GetNearestLights` 0x4c15f0), then confirmed live -- every
candidate decoded, with plausible world positions, sane radii, and semantically
meaningful colours (a warm torch at `(16.44, 8.17, 2.92) r=13.30
rgb=(1.75,1.50,1.00)` beside cool `rgb=(1.30,1.92,1.82)` lights on round
placement coordinates).

| Field | Offset | Type |
| --- | --- | --- |
| World position | `+0xac` | 3 x float |
| Radius | `+0x70` | float |
| Colour (RGB) | `+0x64` | 3 x float |
| Ambient-only flag | `+0x80` | int32 |

`LightManager::GetShadowLights(int)` returns a CExoArrayList: data pointer at
`+0`, element count at `+8`. Note colour components legitimately EXCEED 1.0
(NWN over-brightens), so anything clamping them to 0..1 for display -- e.g.
ImGui's colour swatch -- will show white; that is a display artifact, not a
decode error.

### NWN dirty-checks its uniform uploads

The single most important engine behaviour found this session. Re-drawing an
object whose model matrix has not changed uploads NOTHING, so a per-object
light transform CANNOT be applied by intercepting `glUniformMatrix4fv` during a
replay: static geometry is missed entirely while skinned geometry (which
recomputes every draw) works, which is a confusing half-working failure. Use
the matrix stack instead. Measured with per-(bucket, layer) interception
counters: static `sub-mv=0` on every layer, dynamic a full count.

### `SDL_PollEvent` is a jump thunk, not code (Linux)

At 0xe2bd50: `push rbp; mov rbp,rsp; pop rbp; jmp QWORD PTR [rip+rel32]`,
reaching a writable function pointer at 0x18af388. Patching that pointer is the
cleanest hook available -- no subhook, no prologue relocation, undo is one
store. The same shape applies to `SDL_WaitEvent`, `SDL_WaitEventTimeout` and
`SDL_PushEvent`. Verify the opcode bytes before trusting the decoded slot.
(On Windows the same symbol is a real exported function instead, and detouring
it yields no trampoline -- see `win/README.md`.)

### Windows executable (`bin/win32/nwmain.exe`)

Stripped (MSVC 2017), but exports **18,245 named symbols** covering every
function AND every static data symbol the injector needs, so `GetProcAddress`
replaces the ELF `.symtab` walk. It imports **OPENGL32.dll**, so the renderer
approach ports unchanged. It exports **no `__glew*` symbols at all** -- modern
GL is resolved through `wglGetProcAddress` into private pointers, so the Linux
GLEW-pointer patch has no equivalent there. SDL is statically linked (629
`SDL_*` exports). Full Itanium -> MSVC name map: `win/nwn_win_symbols.h`.
