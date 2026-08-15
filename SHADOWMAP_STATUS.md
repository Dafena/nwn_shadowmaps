# NWN:EE Linux shadow-map injector — implementation checkpoint

> **NOTE ON `run-shadowmap-*.sh` LAUNCHERS.** The phase-era diagnostic launchers
> named throughout these documents are NOT included in this repository. They
> were one-off scripts for specific investigations, most of which are finished,
> and several drove code paths that no longer exist. The shipped launchers are
> `run-dev.sh`, `run-nwn.sh`, `run-shadowmap-trace.sh` and `nwn-shadows.sh`;
> everything the old ones did is reachable through the `NWN_SHADOWMAP_*`
> environment variables they set.


## Local light shadows now fade (2026-08-13, maintainer-confirmed)

Three fades, three different causes, deliberately not merged:

1. **Day/night** -- follows `shadowalpha`, an engine global that ramps with time
   of day. Across a dawn it traces a V (falls as the moon's shadows go out,
   climbs as the sun takes over), so only the FALLING half is used, tracked as a
   peak-relative ratio that can only decrease. Scaling by the raw value instead
   removed local shadows entirely -- it reads ~0.025 mid-transition.
2. **Per-light** -- `PartLight +0x90`, NWN's own 0..1 light fade, found by
   delta-scanning the struct across a transition. Reads 1.0 when steady.
3. **Selection** -- a light NWN drops keeps its entry in our join table with a
   decaying level and is still published as an ordinary active slot until it
   reaches zero, so fading out uses the same in-band path as fading in.

Also this session: the **local shadow cadence control now works** (it only ever
gated the diagnostic cube; the production capture used a separate timestamp
nothing compared against it), plus an **"Ultra (every frame)"** level, and
freshness is judged in SECONDS against the configured interval rather than in
frames -- a frame count cannot tell a throttled capture from a stopped one, and
the shadow blinked at exactly the cadence rate.

Full detail, including the six approaches that failed and why, is in
`CURRENT_TASK.md`. The short version of the lesson: **a value that looks static
may only be unobserved**, and **suppressing the engine's own pass stops the
engine updating the state you are trying to read.**


**Forked into `csm_claude/` on 2026-08-09 — see the banner at the top of
`AGENTS.md` in this folder for what did/didn't come along.** Only the
`run-shadowmap-full-bsp-csm-filtered-shadows.sh` launcher was copied here;
the "Use these launchers" table below still documents the full set, most of
which only exist in the parent `linux-x86/` directory.

**PERFORMANCE — Phase 6c bucket-level cascade replay is IN-GAME VALIDATED
(2026-08-09). Use `run-shadowmap-full-bsp-csm-filtered-shadows-fast.sh`.**
Maintainer's verdict after the fix: shadows correct, "fps is amazing, it's
buttery smooth."

The problem: "really low performance" on a small-but-busy area (100s of
meshes vs. 15 in other tested areas). Diagnosed by elimination, not by
guessing -- dropping `NWN_SHADOWMAP_SIZE` 2048->1024 and setting
`NWN_SHADOWMAP_CSM_PCF_RADIUS=0` changed nothing, which ruled out
GPU/fill-rate and pointed at CPU/driver-call overhead. The accepted per-draw
fan-out (`DUPLICATE_CASCADE_LIGHT`) rebinds the cascade FBO, re-attaches its
depth layer and re-queries 6 pieces of GL state on **every (draw call,
cascade layer) pair** -- thousands of redundant driver calls per frame on a
busy area. `NWN_SHADOWMAP_CSM_BUCKET_REPLAY=1` binds each cascade layer once
per BUCKET and replays the whole bucket through `guarded_render_bucket()`.

**THE KEY ENGINE FACT THIS UNCOVERED — read before writing any replay code:
NWN DIRTY-CHECKS ITS UNIFORM UPLOADS, so you CANNOT apply a per-object light
transform by intercepting `glUniformMatrix4fv` during a replay.** Re-drawing
a static object whose model matrix has not changed uploads *nothing*, so
there is nothing to intercept; only skinned/animated geometry re-uploads
every draw. The first version of this path did exactly that and the
`[shadowmap][replay] ... sub-mv=` counters measured it outright: static
buckets reported `sub-mv=0` on **every** layer including layer 0, dynamic
buckets reported a full count. Static geometry therefore rendered with the
CAMERA transform into all four layers -- the four static layers came out
byte-identical (same range, same 3,070,238 texels) and no shadows appeared,
while the dynamic layers looked fine. **The working mechanism is the one
Phase 3 already validated for `render_from_light()`: write the light
view/projection straight into the engine's matrix stack (byte-exact
save/restore, flag both stacks dirty, put the current matrix mode back before
drawing) and let NWN derive and upload each object's matrices itself** --
changing the view also makes those uploads genuinely dirty, so static and
dynamic both come out correct. Do not do both: matrix-stack substitution plus
uniform interception would double-transform.
Post-fix evidence: the four static layers now report distinct ranges/texel
counts (`4194298 / 4194304 / 4194304 / 3902792`) instead of four identical
`3070238`s.

The per-draw path is untouched and still reachable by simply not setting the
flag; keep it as the A/B reference.

Updated 2026-08-09. This is the concise handoff for the current working state.
Read `AGENTS.md` for detailed reverse-engineering constraints and
`LINUX_RENDERER_MAP.md` for symbols, evidence, and phase history.

## Area shadow policy now works on Windows (2026-08-14)

`CNWCArea::UpdateShadowingLights` cannot be hooked on Windows -- `nwmain.exe`
exports nothing of `CNWCArea` and the function is not virtual -- so the injector
observes the DECISION instead of the fields. That function ends in
`AurEnableShadowing`/`AurDisableShadowing` followed by a tail call to
`AurSetDynamicProjectionLight`, and all three are exported there. The last call is
reached from nowhere else in the binary, which is what makes the preceding toggle
identifiable as the area's rather than
`CNWCVisualEffectOnObject::EnableHardCodedEffectShadow`'s.

Linux keeps the field-reading hook authoritative and runs the observed path
alongside it, warning once if they disagree -- that cross-check is the only
validation the Windows path can get. **Confirmed in game 2026-08-15: it works on
Windows, and raised no disagreement on Linux.**

`ShadowOpacity` (+0x104) is still unavailable on Windows; the on/off policy
applies at unmodulated strength there.

## Profiling: the engine ships Tracy (2026-08-14)

Both binaries have the Tracy 0.10 client compiled in and Beamdog ships the server
in `bin/<platform>/tracy/`. Enable via `settings.tml`
(`[instrumentation.tracy] enabled = true`, port 8086) and restart; the client runs
in on-demand mode so it is free until a profiler attaches. Windows reads the copy
inside the Proton prefix. 192 engine source files carry zones, including
`aurscene.cpp` and `aurlightmanager.cpp` -- a better instrument than the F3 HUD
for engine-side cost. Do not test for it by grepping `___tracy` exports; that is
GCC mangling and MSVC never emits it.

## Local shadow reach: the capture cone (2026-08-15)

Local shadows were confined to a small disc because the map is a single downward
cone covering `height * tan(fov/2)`. At the old 140 degrees that is 2.75x the
light's height -- barely 4 units for a carried torch. **Default is now 160
degrees (5.67x), maintainer-confirmed.** NWN's light radius also bounds the
receiver, so which one binds was settled by a single `LOCAL_LIGHT_FOV=172` run
rather than assumed.

The receiver's texel-size estimate follows the FOV through `nwnLocalTanHalfFov`;
it was hardcoded for 140 and would have reintroduced acne on any FOV change.

## Local shadow sources: three methods (2026-08-13)

"Local shadow sources" is a (method, count) selector; the persisted value is the
combo index. Index 0 = Legacy (bucket replay honouring "moving casters only",
3 lights); 1--3 = Emitter (bucket replay of all four buckets with nothing
cleared); 4--6 = High (per-draw duplication of the visible meshes).
**Default is 4: High, one light.**

The tiers differ ONLY in fill. All three take their lights from
`GetShadowLights()` and so stop at NWN's three shadow-casting lights.

Emitter reproduces the 2026-08-10 savepoint's capture literally. It came out
casting the engine's invisible stencil proxies because `g_localLightDynamicOnly`
("moving casters only") does not exist in that savepoint and defaults ON today:
it clears the depth after the static buckets and keeps 2/3, where the proxy
submissions live. The tier forces it off. The FILL was never the problem --
buckets 0..3 are the visible world geometry.

Cost tracks the fill: a bucket replay is a handful of calls regardless of caster
count, while High costs one draw per caster per light (1096 draws at one source,
2600 at three). That is why Emitter reaches four lights and High stops at three.

Nothing in production captures a cube -- every tier is one downward face per
light, receiver layer = slot = light. The six-face cube is a diagnostic probe.

## Platform divergence (2026-08-13)

`NWN_WIN_LOCAL_FASTPATH` (`nwn_platform.h`) carries three items as ONE decision:
the per-light caster cull, whether the current program comes from the tracked
`g_curProgram` or `glGetIntegerv`, and whether publishing a staged generation
needs ANY layer or EVERY layer to have drawn. Windows takes all three; Linux
takes none and matches the 2026-08-13 savepoint.

They ride together because the publish rule has to match whether the cull is on.
Running the cull on Linux under the strict publish rule is what produced the
Linux flicker: culling every caster of a light is indistinguishable from that
light having none, so one empty layer discarded the whole frame's capture and
alternated with the bucket-replay fallback.

## Engine authority rule (2026-08-13)

The injector must defer to NWN whenever the game exposes a decision. Do not
substitute camera proximity, camera visibility, SetLightGL order, dynamic-mesh
anchors, or other injector heuristics for an engine decision. Missing engine
data means the injected pass is suspended, never guessed.

For local point-light shadows specifically, `LightManager::GetShadowLights()`
is the sole source-selection authority: its returned non-ambient lights are
already priority ordered by NWN. The first entry owns the one-light cube path;
the following entries fill any future bounded source slots in that same order.
`SetLightGL` remains a read-only census for ordinary light lift only and is not
permitted to select, rank, bootstrap, or replace a local shadow source.

**No exception exists.** One was granted on 2026-08-13 for the Emitter tier
(census selection by camera distance) and withdrawn by the maintainer the same
day. Every tier takes `GetShadowLights()` verbatim.

**In-game confirmation (2026-08-13):** engine-priority local-light selection is
now correct. The overlay/census must remain read-only: it must never overwrite
the selected light position or radius after `GetShadowLights()` has chosen it.
This is a hard regression guard, not a tuning preference.

**Active local-light fix (2026-08-13):** the receiver normal offset is now
oriented toward the selected light before projection. Screen derivatives supply
an unsigned geometric normal; using their raw sign made the offset flip when the
camera yawed, producing detached or degenerate local shadows. Bias controls
cannot compensate for that projection-space error.

## AUDIT 2026-08-12 -- what shipped, and what is settled

Both platforms working and maintainer-confirmed in game. Windows performance
matches Linux (the residual ~1.5x under Proton is WGL->GLX translation, measured
uniformly across three unrelated GL operations -- not a defect with a fix in it).

Settled during the 2026-08-11/12 performance work, each with the measurement
that proved it:

| finding | evidence |
| --- | --- |
| `shadow_getenv` on the per-uniform path was the Windows collapse | fps unchanged with every render pass disabled; `unimat4=0` on Linux vs 4683/frame on Windows |
| the cascade fit cache never hit once | `fitcache hit=0`, 1682 sun refits; snapshot compared a different vector than the fit used |
| the lift loop ran 128 iterations/pixel unconditionally | gated on `sunShade>0`, exact because the result is multiplied by zero |
| emitters/particles were never casters | bucket 6 is 50 draws / 429 indices and the replay visits only 0,1,2,3 |
| NWN's blob shadow cannot be suppressed, its STENCIL pass can | `Creature Shadow Detail=Off` IS the blob fallback; Best is the stencil path |
| the replay is not the frame cost at low settings | ~1.7 ms of a 17.5 ms frame; 10,197 engine draws are the area |
| a 32x32 forest area is GPU-bound, not CPU-bound | NWN's own perfstats: 6.2 ms client CPU against an 18 ms frame |

Wrong answers that cost test rounds, recorded so they are not re-run: the fog
range, MSAA, hybrid graphics, per-draw driver overhead, symbol mismatches,
emitter geometry, grass render distance, and the stencil pass (twice -- once
dismissed on a false-zero counter, once confirmed).

**Instrumentation rules earned here** (see AGENTS.md for the full list): a CPU
timer around GL calls measures submission, not work; report at the point of
return; measure frequency, not blame; a per-pass counter is meaningless without
the setting that governs that pass recorded beside it.

## What currently works

The injector is an `LD_PRELOAD` library (`libnwn_shadowmap.so`) for the Linux
`nwmain-linux` executable. It does **not** modify the executable, the Proton
prefix, `.mtr` files, or `.shd` files.

The accepted directional-sun path is fully GPU-side and updates every frame:

1. A selected gameplay `Scene::Render` captures one immutable normal-camera
   context: viewport, view/projection, inverse VP, and camera position.
2. Four texel-snapped orthographic cascades are fitted to that context and the
   area sun direction.
3. NWN's own normal material submission populates injector-owned static and
   dynamic `GL_TEXTURE_2D_ARRAY` depth maps. The arrays are 2048² by default,
   four layers each, with comparison sampling and clamp-to-edge.
4. The injector copies normal scene depth, reconstructs receiver world position
   in a fullscreen pass, chooses a cascade by camera depth, and compares against
   both depth arrays.
5. The final result is translucent-black fullscreen darkening. It is separate
   from NWN materials and UI rendering.

### Confirmed caster coverage

| Class | Source | Target | Status |
| --- | --- | --- | --- |
| Static opaque world | Native full-BSP candidate submission / bucket 0 | Static CSM array | Confirmed |
| Static alpha-cutout foliage/placeables | Source-classified `AlphaDiscard`, bucket 1 | Static CSM array | Confirmed |
| Dynamic character body | Bucket 2 | Dynamic CSM array | Confirmed |
| Dynamic alpha/cards/hair | Bucket 3 | Dynamic CSM array | Confirmed |

The body bucket clears dynamic depth for the current area frame; alpha bucket 3
then adds to that same fresh target. The receiver suppresses itself rather than
using stale data if a required layer did not receive current-frame geometry.

### Stability and visual evidence

- Static and dynamic shadows stay world-aligned while orbiting, panning, and
  zooming. This fixed the former camera-following/leaking path.
- Four-cascade resolution and practical range were accepted in-game.
- Alpha-cutout static foliage and added placeables were captured and cast
  correct silhouette shadows.
- Dynamic player body and alpha-card/hair geometry were captured and cast.
- The dark composite is in-game validated. It restores GL program, FBO,
  viewport, depth, blend, colour-mask, texture-unit, scissor, and cull state.
- Manual PCF was verified to affect edges. Its original 2.0 texel default was
  too soft; the current filtered launcher defaults to 0.75 texels.

## Use these launchers

Run all commands from THIS `csm_claude/` folder, not the parent `linux-x86/`:

```bash
cd "$NWN/bin/linux-x86/csm_claude"
```

| Command | Use | Present in this fork? |
| --- | --- | --- |
| `./run-shadowmap-full-bsp-csm-all-casters.sh` | Opaque red diagnostic. Use for A/B or caster-coverage problems. | parent only |
| `./run-shadowmap-full-bsp-csm-shadows.sh` | Accepted hard-edge translucent-black sun shadows; baseline. | parent only |
| `./run-shadowmap-full-bsp-csm-soft-shadows.sh` | Baseline plus cascade-boundary overlap (`0.75` world units). This does not itself soften object shadow edges. | parent only |
| `./run-shadowmap-full-bsp-csm-filtered-shadows.sh` | Recommended sun path: dark composite + overlap + 3x3 manual PCF at radius `0.75` texels. | **yes** |
| `./run-shadowmap-local-light-trace.sh` | Read-only local-light candidate census. No rendering changes. Run in an area containing several torches/local lights. | parent only |
| `./run-shadowmap-local-light-probe.sh` | **New in this fork (Phase 6b).** Same sun path as the filtered-shadows launcher, plus the local-light depth PROBE described below. No local-light receiver yet -- check the log/PGM, not the screen. | **yes** |
| `./run-shadowmap-full-bsp-csm-filtered-shadows-fast.sh` | **RECOMMENDED (Phase 6c, in-game validated 2026-08-09).** Same sun shadows as the filtered-shadows launcher, but binds each cascade layer once per bucket instead of once per draw call. Dramatically faster on busy areas; maintainer-confirmed correct and smooth. | **yes** |
| `./run-shadowmap-local-light-shadows.sh` | **Phase 6d.** The `-fast` sun path plus the local-light depth capture AND its receiver. The receiver is implemented but not yet producing visible local shadows -- see the local-light section below. | **yes** |
| `./run-shadowmap-trace.sh` | Generic read-only engine call-order trace; no shadow rendering. | **yes** |

Anything marked "parent only" needs `cd` back to the parent `linux-x86/`
directory to run (it was deliberately not duplicated into this fork -- see
`AGENTS.md`'s banner).

The automatic log is:

```text
<this csm_claude folder>/shadowmap-phase1.log
```

The launcher truncates it at start and appends all process output through
`tee`, so later agents can read it directly without manual copy/paste.

PGM dumps also land in this folder, via `NWN_SHADOWMAP_OUT_DIR`. That variable
exists because the fork's launcher must `cd` into the parent game directory
(nwmain-linux infers its base data directory from the CWD and refuses to start
otherwise), which would otherwise send every dump into the parent baseline and
overwrite its artifacts -- which happened once before the variable existed.

## Current tuning

The recommended filtered launcher accepts one-shot overrides:

```bash
NWN_SHADOWMAP_CSM_PCF_RADIUS=0.5 \
NWN_SHADOWMAP_CSM_STRENGTH=0.30 \
NWN_SHADOWMAP_CSM_BLEND=0.75 \
./run-shadowmap-full-bsp-csm-filtered-shadows.sh
```

| Variable | Meaning | Current default |
| --- | --- | --- |
| `NWN_SHADOWMAP_CSM_STRENGTH=0..1` | Darkness opacity of a fully shadowed pixel. | `0.42` |
| `NWN_SHADOWMAP_CSM_BIAS=0..0.05` | Receiver depth-comparison bias. | `0.0025` |
| `NWN_SHADOWMAP_CSM_BLEND=0..10` | World-space overlap before each cascade split. `0` retains hard splits. | `0.75` in soft/filtered launchers |
| `NWN_SHADOWMAP_CSM_PCF_RADIUS=0..4` | 3x3 PCF radius in shadow-map texels. `0` uses only hardware comparison filtering. | `0.75` in filtered launcher |
| `NWN_SHADOWMAP_AREA_SHADOW_FLAGS=0/1` | Capture the active area's `SunShadows`, `MoonShadows`, day/night state, and `ShadowOpacity`. | `1` |
| `NWN_SHADOWMAP_AREA_SHADOW_OPACITY=0/1` | Apply the verified area `ShadowOpacity` to directional CSM; local lights remain independent. | `1` |
| `NWN_SHADOWMAP_AREA_SHADOW_POLICY=0/1` | Apply `SunShadows` by day and `MoonShadows` by night to directional CSM. Local-light shadows are independent. | `1`; set `0` for A/B fallback |
| `NWN_SHADOWMAP_AREA_SHADOW_FADE=0..10` | Composite-only fade duration when NWN switches its day/night directional policy. No CSM map rebuild; `0` is instant. | `0.75` seconds |

### Area shadow policy (implemented; first live validation pending)

The Linux final executable exports `CNWCArea::UpdateShadowingLights()`. Its
own load/update path establishes the active area fields used by the injector:
`MoonShadows` at `+0x0a8`, `SunShadows` at `+0x0c8`, `IsNight` at `+0x0dc`, and
the 0–100 `ShadowOpacity` byte at `+0x104`. The injector observes that update
after NWN runs it, validates every value, then multiplies the directional CSM
composite strength by the area's opacity. It does **not** disable the whole
fullscreen receiver, because that would incorrectly remove local-light shadows.
The calibration run observed exact matches at 0%, 50%, 92%, and 100% against
NWN's own `shadowalpha`, so opacity application is default-on. The day/night
`SunShadows`/`MoonShadows` booleans now gate directional CSM by default:
day selects SunShadows and night selects MoonShadows. They do not affect local
point/spot-light shadows. `NWN_SHADOWMAP_AREA_SHADOW_POLICY=0` is the A/B
fallback when an unusual module needs the older injector-only directional path.

The policy transition itself is composite-faded (default `0.75` seconds) rather
than popping a directional shadow in/out. `NWN_SHADOWMAP_AREA_SHADOW_FADE=0..10`
sets the duration; it is also exposed as **Day/night fade** in the development
overlay. It must remain a final-composite operation: it does not rebuild or
blend CSM depth maps, invalidate caches, change the light vector, or affect
local-light shadows.

`NWN_SHADOWMAP_AREA_SHADOW_PROBE=1` is the calibration path. It logs a bounded
record after every engine `UpdateShadowingLights()` call, including the flags,
`+0x104`, nearby fields, and the engine's own `shadowalpha` global. It is
strictly read-only and `run-dev.sh` enables it automatically.

### Scope boundary: scripted area sun

This checkpoint ends before `SetAreaLightDirection()` work. It has no setter
hook, timed static-world-map rebuild, transition depth texture, or per-bucket
moving-caster path. Directional shadows use the normal area-light snapshot;
the implemented area policy remains `SunShadows`, `MoonShadows`, and
`ShadowOpacity`, while local-light shadows remain independent.

The hard baseline launcher does not set manual PCF or overlap, so it remains
the A/B reference if a later change looks wrong or costs too much.

## THE performance finding (2026-08-10): the full-BSP submission

**200 fps -> 28 fps was caused almost entirely by `submit_full_bsp_native_static`
inflating NWN'S OWN draw list, not by anything the injector renders.** Measured
with the receiver pass switched off: our passes totalled **1.64 ms**, while the
panel reported **23,938 engine draws per frame** against a normal culled set of
roughly 2,000. Unticking the submission returned fps immediately.

That mechanism exists for a real reason -- casters outside the camera frustum
must still reach the shadow map, and without it off-screen objects visibly stop
casting. But it achieves that by pushing the entire non-culled static set into
the ENGINE's mesh buckets, so the game renders all of it to screen as well.
The console renderer (see `PS4_CASCADE_REFERENCE.md`) does the opposite: it runs
dedicated caster passes for the shadow maps and never touches the main camera
pass's culling. Ours was an architectural mistake, not a tuning problem.

**Fix: an explicit one-shot handshake.** The world map REQUESTS the complete
caster set; the submission grants it for exactly one frame; the map rebuilds
only on a frame where the set was actually present, then clears the request.
So the expensive frame happens once per map rebuild (once per area at a large
extent) instead of every frame.
An earlier attempt had each site independently ask "does the map need a
refresh?" and skip accordingly -- they run at different points in the frame with
different contexts, disagreed, and the submission effectively never got skipped.
Do not go back to that shape.

`NWN_SHADOWMAP_FULL_BSP_SUBMIT=0` (or the panel checkbox) disables it entirely:
fast, but off-screen casters stop shadowing.

## Hybrid near-field static (2026-08-10)

The world map alone forces ONE texel density on the whole area, and the
maintainer measured both ends of that: extent 32 gave crisp shadows over a tiny
radius, 64 started detaching shadow roots (bias in texel units), and 256 lost
small objects' shadows entirely. No single value works.

So static now has two sources, combined with `min()` (either may shadow a
pixel, so there is no selection boundary to get wrong):

- **Near** -- `NWN_SHADOWMAP_STATIC_NEAR_CASCADES` (default 1) cascade layers
  receive camera-fitted static casters at high density. These use the engine's
  NORMAL camera-culled set, which costs nothing extra: near casters are on
  screen anyway, so no full-BSP submission is needed for them.
- **Far / off-screen** -- the world-anchored map, as before.

Cascade 0 spans ~45 units at 2048^2 (~2 cm/texel) versus ~5 cm for the world map
at extent 128, which is what brings small close-up objects back.

## Fixed: the static cascade cache dropped alpha-cutout casters

The cache stamped its "generation" per LAYER, but the static target is fed by
TWO buckets (0 opaque, 1 alpha-cutout). Bucket 0 rendered and stamped the layer;
bucket 1 then looked already-cached and was skipped, so alpha-cutout casters
silently never entered the cascade -- reported in game as "+5 fps but missing
alpha textured meshes". Generation is now tracked per (bucket, layer).

## "Cascades" is ONE named control, not two sliders (2026-08-10)

The panel used to expose the slice count and the dynamic-slice count as separate
sliders. They are now a single Off/Low/Medium/High/Extreme/Ultra combo, matching
the other quality settings, because the two were never independent: dynamic can
never exceed the slice count, and it is the PAIR that sets per-frame cost.

| Level | slices | with moving casters |
| --- | --- | --- |
| Off | 1 | 0 |
| Low | 2 | 1 |
| Medium | 2 | 2 |
| High | 3 | 2 |
| **Extreme (default)** | **3** | **3** |
| Ultra | 4 | 4 |

Two engine limits shaped this table, and neither is negotiable without real
work: slices are `1..kCascadeCount` (**4**, and widening it means a wider depth
array plus a wider receiver shader), and **0 slices does not exist** -- every
call site clamps `<1` to 1. So "Off" means zero PER-FRAME caster work (moving
things stop casting; static shadows still come from the world-anchored map), not
"no shadows", which is the Dark composite checkbox.

The default is Extreme rather than Ultra because the maintainer measured 3/3 as
looking BETTER than 4/4 as well as costing less. The panel prints the live pair
under the combo, and derives the level from the live values by nearest match, so
an env-var override still displays sanely.

## The panel remembers its settings (2026-08-11)

Panel edits are written to **`nwn_shadowmap_settings.ini`** beside the log
(`NWN_SHADOWMAP_OUT_DIR`, else the working directory) and reloaded at startup.

- **The file wins over the environment**, because it is loaded AFTER env
  parsing. That is deliberate: the launchers set nearly every variable, so
  env-wins would make the file useless. The normal development and probe
  launchers preserve settings. `NWN_SHADOWMAP_NO_SETTINGS=1` is only a manual
  stateless A/B escape hatch if a saved value ever makes the game unusable, and
  deleting the file resets everything.
- **Saves are debounced 0.75 s** and driven by comparing values each frame
  rather than hooking each widget, so dragging a slider is ONE write and the
  panel code carries no persistence concerns.
- **The receiver debug mode is NOT saved.** Restarting into solid magenta with
  no memory of why would be its own bug report.
- Loading also resyncs the staged resolution boxes, or the panel shows
  "pending" against values that are already live.
- **"Restore defaults"** in the panel puts every setting back to the built-in
  values and saves immediately, so a reset survives a restart like any other
  edit. Deleting the .ini does the same from outside the game. The resolutions
  go through the staged Apply path rather than being written live, because they
  own GL textures.
- Panel sections start **closed**. They were all DefaultOpen, which made the
  window taller than the useful part of the screen.

## Local-light shadows: what actually fixed the acne (2026-08-11)

Characters came out mottled with a chevron pattern -- the depth comparison
flipping across a surface that samples its own shadow map. What was tried, in
order, and what it was worth:

| Attempt | Result |
| --- | --- |
| `glPolygonOffset` slope bias | helps a little; raising it detaches the shadow before it cleans up |
| Back-face-only fill (`glCullFace(GL_FRONT)`) | **NOTHING** -- NWN sets its own cull state per draw and overwrites it. Removed; making it work needs intercepting `glCullFace` for the whole pass |
| Normal-offset bias (normal from `dFdx/dFdy` of the reconstructed position) | works, but the derivative normal is garbage at dithered/alpha edges, which painted dark fringes around hair. Fixed by fading the offset out where the position moves too far per pixel |
| Alpha cards as casters | NWN dithers them in SCREEN space, so from a light they store near-solid and hair blobs across the face. Excluded by default |
| **Minimum separation in WORLD units** | **the fix.** Shadow only when the caster is genuinely nearer |

The last one is the important idea. Acne is by definition "the stored depth and
the surface being tested are the same surface", i.e. the gap is ~0; a real
shadow has a caster tens of centimetres away. Requiring a real gap therefore
removes self-shadowing outright and leaves genuine shadows untouched. It is
expressed in world units and converted per pixel through the derivative of the
perspective depth curve, with `zn`/`zf` rebuilt from the light's radius exactly
as the capture builds them -- so it means the same thing at every distance and
cannot drift from the capture. A plain depth bias is correct at exactly one
distance, which is why no value of it ever worked.

Current defaults: **No self-shadow 0.10 units**, normal bias 0.5, slope bias
2.0, alpha casters off.  The previous 1.00-unit default visibly detached
point-light contact shadows, so settings version 5 deliberately migrates the
old saved default.

## NWN's own shadows: suppress the STENCIL pass, and leave the game set to ON

The engine draws its own shadow under every creature and placeable, underneath
this module's. "Hide the game's own shadows" (Performance, both platforms,
**default ON** since 2026-08-12) skips `Scene::RenderShadows` entirely.
Maintainer-confirmed in game: with the game's shadows enabled, they disappear.

**The configuration is counter-intuitive and this is the part to remember.**
NWN's `Creature Shadow Detail` has Off / Fast / Best. Fast and Best are the
STENCIL path -- the one this suppresses. **Off does not mean "no shadow": the
engine falls back to a dark BLOB**, drawn somewhere other than
`Scene::RenderShadows`, which this cannot reach.

| `Creature Shadow Detail` | engine draws | suppressible here |
| --- | --- | --- |
| Off | blob fallback | **no** |
| Fast | stencil, **player only** | yes, but only the player's |
| **Best** | stencil, creatures AND placeables | **yes -> nothing at all** |

So: leave the game's shadows **ON and set to BEST**, and let the injector remove
them. Best is not a waste even though the result is discarded -- the setting
decides which objects the pass COVERS, and this suppresses whatever that pass
would have drawn, so a narrower setting removes less. (An earlier version of
this note recommended Fast "since the result is discarded either way". That was
wrong: Fast casts only from the player.)
Replacing the blob texture is the other route, but it needs every user to edit
their own game data, which a drop-in DLL cannot rely on.

### The measurement trap in this one

`engshadow=0` was recorded across 1091 samples and read as "that pass draws
nothing, so it is not the source". It was captured with creature shadows set to
**Off** -- the pass genuinely had nothing to draw *in that configuration*. The
same counter with shadows on tells the opposite story. **A per-pass counter is
only meaningful next to the setting that governs that pass**; record the
configuration with the measurement or the number means nothing.

En route, two instrument bugs were fixed and are worth keeping:

- `g_stencilDraws` counted in `shadow_before_draw()`, which is reached from only
  **2 of the 10** draw wrappers (this engine draws mostly through
  `glDrawRangeElements` and `glMultiDraw*`). The count now lives in
  `trace_normal_geometry_draw`, the funnel every wrapper calls.
- The per-bucket and quad census required a valid `nwn_core::g_currentBucket`,
  which is **-1 outside `Scene::RenderDrawBucket`** -- so every draw issued
  outside the bucket system was invisible to it. Now reported as
  `[shadowmap][nobucket] draws= quads= idx= prog:`.

## SOLVED: the Windows collapse was shadow_getenv() on a per-uniform path

Symptom: the same area ran fine on Linux and at ~24 fps on Windows (native AND
under Proton, confirmed on a second machine), with the injector removed entirely
it was smooth, and **no panel toggle changed anything** -- not casters, not
cascades, not lights, not even "Sun shadows" off, which disables the receiver
and the screen capture outright. A cost that survives switching off every
rendering pass is not in a rendering pass.

```c
static void trace_matrix_upload(...) {
    if (!shadow_getenv("NWN_SHADOWMAP_UNIFORM_TRACE") || ...   // FIRST condition
```

`my_uniform_matrix4fv` calls that on **every glUniformMatrix4fv the engine
issues** -- thousands per frame. `UNIFORM_TRACE` is never set, so every one took
the miss path, and the miss path is where the platforms diverge:

| | Linux | Windows |
| --- | --- | --- |
| `shadow_getenv` miss | one `getenv()` | `getenv()` **+ 27 `strcmp`** over `kWinDefaultEnv` |

`kWinDefaultEnv` exists so the DLL works without its `.bat` -- a good decision
whose cost was never considered on a hot path. Same source, same call, an order
of magnitude apart by platform.

**Fixed by memoising `shadow_getenv` itself**, not the call sites. 154 sites, 32
reachable per frame or per draw; caching them one at a time repairs what someone
thought to look at and leaves the rest armed -- which is precisely what happened:
the first fix cured the idle case and the frame rate collapsed again the moment
Cascades and light casting were switched back on. Safe because nothing here ever
writes the environment (`_putenv_s` was tried and did not survive startup).
Pointer-compare first, `strcmp` fallback aliased in so it becomes a pointer hit.

Also removed from the same path: `capture_camera_vp_inv` asked the driver for
`GL_CURRENT_PROGRAM` on every matrix upload. We hook `glUseProgram`, so the
answer was already known -- it is now tracked in `g_curProgram`.
`shadow_before_draw()` already carried the comment "glGetIntegerv is a
driver-synchronising query and this runs on EVERY draw call"; the lesson was
applied to the draw path and missed on the uniform path.

**Rule this earns:** anything reachable per draw or per uniform upload may not
call `shadow_getenv`, `getenv`, or any `glGet*`. The cost line now reports
`unimat4=N useprog=N` so the frequency of these paths is visible rather than
assumed.

### What this cost, and why it took so long

Six wrong answers before this one, every one of them plausible and every one
killed by a measurement rather than by argument: the fog range, the local-light
lift loop, the sun-hysteresis phantom, the emitter geometry, MSAA, hybrid
graphics. Two of them were mine twice over -- I reasoned from a bucket census
taken in ONE scene and generalised it, and I called two runs "identical" while
the world map differed 16x in a field I had printed myself.

What actually worked, every time: report at the point of return, measure
frequency rather than blame a subsystem, and `glFinish` before believing any GL
timing. `recv` read 0.10 ms while being the most expensive thing in the frame,
and `copy` under-reported by 100x, purely because only one of them finished.

## Windows was 2.4x slower than Linux at a QUARTER the settings (RESOLVED above)

Same area, same spot, same machine:

| | Linux | Windows |
| --- | --- | --- |
| fps | 59.8 (vsync) | **24.7** |
| Cascades | Extreme (3 slices, 3 dynamic) | Low (2 slices, 1 dynamic) |
| cascade / world / light map | 2048 / 2048 / 256 | **512 / 512 / 256** |
| `replay` | **9.38 ms** | **34.30 ms** |
| engine draws | 2897 | 2883 |

Nearly identical draw counts, 3.7x the replay time, at a quarter of the
resolution and half the cascades. That is not a workload difference.

**Symbols are NOT the cause, verified rather than assumed.** All 38
`kNwnWinSymbols` winNames exist in the shipped `bin/win32/nwmain.exe` export
table (18,245 names), and all 29 Itanium-mangled names the source resolves have
a mapping. Re-run it after any Beamdog patch:

```bash
x86_64-w64-mingw32-objdump -p bin/win32/nwmain.exe \
  | awk '/\[Ordinal\/Name Pointer\] Table/{f=1;next} f&&/^\t\[/{sub(/^\t\[[ 0-9]+\] \+base\[[ 0-9]+\] +[0-9a-f]+ /,"");print}' > exports.txt
```

Also ruled out, by reading rather than guessing: both platforms wrap the SAME
GL entry points (`glUseProgram`, `glUniformMatrix4fv`, `glUniform3f/3fv`,
`glShaderSource` and the six draw functions) -- Linux by patching `__glew*`,
Windows by handing the engine the wrapper from the hooked `wglGetProcAddress`.
The per-draw path `shadow_before_draw()` early-outs identically. The only
per-draw asymmetry left is in the driver, not in this code.

### The fit cache compared a different vector than the fit used (REAL BUG, NOT the fps cause)

**Header corrected after the fact.** This was written up as "ROOT CAUSE" and it
was not: fixing it took the replay from 17.5 ms to 7 ms and the frame rate did
not recover, because the frame rate was never limited by the replay. The bug
below is genuine and the fix stands on its own merits -- the static depth cache
had literally never hit once -- but see the SOLVED section for what was actually
costing the frames.


`update_cascade_math_from_context()` builds the fit from `g_traceAreaLightDir`
(the live vector observed from the engine's per-frame uniform uploads) and only
falls back to `eng::SunDir`. The cache's invalidation test and its snapshot both
read `eng::SunDir` **unconditionally** -- a different quantity from the one the
cached matrices were built from.

`static_world_needs_refresh()` already carries a comment about exactly this
("Compare against the SAME direction the cascades fit to, not eng::SunDir"),
because the world map had the same defect and it was fixed there. The cascade
cache was missed.

All three now go through one accessor, `cascade_fit_sun()`. Fit, invalidation
test and snapshot cannot drift apart again by construction.

**The emitters were never the cost, and the maintainer's own bucket census is
what settled it:** bucket 6 (emitters) is 50 draws / 429 indices and the replay
visits only buckets 0,1,2,3 -- it has never been a caster. Buckets 0 and 1
(static) are 92.7% of the replayed draws and 97.9% of the indices, and those are
precisely what the fit cache exists to skip.

### "NOT a Windows-only bug" -- WRONG, and here is how the reasoning failed

The obvious platform mechanism was `eng::SunDir` resolving on one side only: the
old code guarded both the compare and the snapshot with `if (eng::SunDir)`, so
an unresolved symbol would skip the test entirely and the cache would hit
trivially. **That is not what happens.** `nm` on the shipped Linux binary:

    00000000019af5e0 B _ZN8GLRender27m_lightAreaDiffuseDirectionE

The symbol is present on both platforms, so THAT mechanism was correctly ruled
out and the fit-cache bug was indeed on both.

**But the conclusion drawn from it -- "the Windows gap is headroom, not
behaviour" -- was wrong**, and it sent the search in the wrong direction for
several rounds. There WAS a Windows-only behavioural difference: `shadow_getenv`
on a per-uniform-upload path (see the SOLVED section). The error was reasoning
from one ruled-out mechanism to "therefore no platform difference exists", when
all that had been shown was that this particular mechanism was not it.

The "roughly 2x the CPU per draw call" figure in the original text was also an
artefact: it compared a Windows run at `world_map=8192` against a Linux run at
`2048`, a 16x difference in a field that was printed in both logs and not read.
The honest number, once both ran the same settings, was 4.8x -- and it was not
per-draw cost at all, it was a `getenv` miss inside the uniform wrapper.

The fit cache is now readable from the Linux panel (`fit cache: N hit / M
refit`, with a `<-- never hitting` marker) so this class of bug does not need a
log to spot again.

### MEASURED: the static fit cache never hit, not once

The instrumented run settled it. Over a whole Windows session:

| fitcache hits | refit: sun | refit: move | refit: turn |
| --- | --- | --- | --- |
| **0** | **1682** | 79 | 0 |

Every main-scene render threw the cascade fit away and re-rendered every static
caster into every near layer -- `replays=4/2854draws`, `replay=17.5ms`. The
cause is 95% the SUN, and the test was a bare `1e-3` compare against the last
fit's direction. On a persistent world with a day/night cycle that is tripped
every frame, so the static depth cache -- the entire reason the fit is held --
could never be reused.

Fixed with `kCascadeSunHysteresis = 0.01f` (~0.6 degrees on a normalised
direction, below the angle at which a refit reads as a pop). This is the SAME
fix the world map already needed for the SAME reason; the cascade path was
simply missed at the time. A rate-limited `fit refit on SUN: delta=...` line
now reports the magnitude, because a small delta every frame (drift, fixed by
hysteresis) and a large one (the vector flipping between values, which no
threshold repairs) look identical in a counter.

### The cost line was reporting a GUI element

`[shadowmap][cost]` counted `Scene::Render` CALLS and NWN makes several per
frame. Sampling every 300th caught the real scene 4 times out of 54; the other
50 lines said `enginedraws=3 replay=0.00ms` -- an accurate description of a
secondary scene and worthless for the question being asked. It now reports only
calls that actually drew the world (`g_frameDrawCalls > 100`) and the field is
named `mainscene=` rather than `frame=`.

Also visible once `recv` was measured with `glFinish` instead of being
submission time: the receiver pass really costs **5-7 ms** per main scene, not
the 0.10 ms it used to claim. That is after the lift-loop gate, and it is the
next thing to look at.

`replay` is a SUM over an unknown number of `guarded_render_bucket` calls, so
the totals above cannot say whether each call got slower or there were more of
them. The instrumentation added for the next run answers exactly that:

    replays=<calls>/<draws> | fitcache hit=<n> refit(move= turn= sun= scene= off=)

- `replays` high on Windows, similar per-call cost -> the **static fit cache is
  missing** and the `refit(...)` breakdown names which of the four conditions
  did it. `sun=` non-zero every interval means the day/night cycle is
  invalidating the fit (the cascade test is still a bare `1e-3` compare, with
  none of the hysteresis the world map needed for exactly this reason).
- `replays` similar, per-call cost higher -> it is inside the engine's own
  `RenderDrawBucket` or the driver, and the next step is a GL debug callback.

### Fixed alongside: hidden settings were still being persisted

`local_enabled` was documented as "a hidden control must not be persisted"
after it silently disabled light-cast shadows on Windows -- but the rule was
applied to that one key. Every setting the Windows panel hides (`csm_*`,
`cache_move`, and the whole local-light tuning group) was still saved and
reloaded there, each one able to fight a shipped default with no control able
to undo it. `settings_table()` is now split into a both-platforms group and a
`#if !NWN_SHIP` group (it was `#ifndef _WIN32` until the shipping policy was
split from the platform question on 2026-08-12).

`cache_move` is the one with teeth -- it is the distance the camera may move
before the cascades refit, and a refit re-renders every static caster into
every near layer. **This was NOT the cause of the slowdown above**: the live
`bin/win32/nwn_shadowmap_settings.ini` reads `cache_move=2`, the default. The
bug is latent, and was fixed on its own merits.

## The unconditional lift loop (a real waste; NOT the "regression" this claims)

**Header corrected after the fact.** The gate below is a genuine and worthwhile
optimisation -- a 128-iteration per-pixel loop whose result was multiplied by
zero on most of the screen -- but it was NOT the fps regression. The premise
that led here ("the build from before light-cast shadows was fine in the same
place") was later withdrawn by the maintainer: the older DLL was tested again in
that area and ran just as badly. The actual cause is in the SOLVED section and
predates the local-light work entirely.

Kept in full because the gate is correct and the reasoning about exactness is
still the right way to think about that shader.

Reported: an emitter-heavy desert area ran ~13 fps on Windows, while the build
from before light-cast shadows existed was believed fine in the same place.

What the local-light work added to the receiver's FRAGMENT shader, all of it
unconditional:

```glsl
float lampLit=0.0;
for(int li=0;li<nwnLampCount&&li<128;++li){ ... }   // up to 128 lamps, PER PIXEL
```

63 lamps in that area meant 63 iterations on every receiver fragment, over the
whole screen, every frame -- including the sunlit majority where the result is
multiplied by a `sunShade` of 0 and thrown away. **The captures were never the
cost.** The log proved the caster cap works (`capturing=0` with "Lights casting
shadows" off) and fps did not move, because the lift runs whether or not
anything captures.

Gated exactly, not approximately:

```glsl
if(sunShade>0.0&&nwnLocalLightLift>0.0){ ...loop... }
```

`sunShade` is 1-lit, so 0 means the sun already leaves the pixel lit and the
loop's only effect (`sunShade *= 1-lift*lampLit`) provably cannot change it.
This is the same shape as the `worldStatic` hoist a few lines above -- correct
work, run unconditionally, in the one shader that touches every pixel.

`nwnLocalShadeCov` needed no gate: it already returns early on
`nwnLocalLightEnabled==0`, loops `nwnLocalSlots` (0 when nothing captures), and
rejects a fragment on `d2>=r2*r2` before any texture fetch.

### The timer lied, and that is why the first answer was wrong

`recv=0.10ms` in the `[shadowmap][cost]` line was **submission time**: the
`glFinish` that makes it a GPU number was gated behind `g_receiverDebug`. A
fill-rate bound pass costs ~0 ms to submit, and the GPU stall is billed to
whichever GL call blocks next -- here `replay=34.30ms`, which is exactly the
wrong subsystem and cost a full test round.

The finish now also runs on the frames that PRINT (one stall per 300 frames, no
measurable cost), and the line carries `lamps=<census>/<cap> localslots=<n>` so
the loop's real iteration count is visible instead of inferred.

**Rule, third time it has bitten this project: a CPU timer around GL calls
measures submission, not work.** Either finish, or do not report the number.

## Sun shadows must not darken what a LOCAL LIGHT lights (2026-08-11)

Reported symptom: walk under an awning holding a torch and the torchlight, the
character and the floor all dimmed together. The receiver painted flat
translucent black over every sun-shadowed pixel and knew nothing about lamps --
but being out of the SUN says nothing about a torch two feet away.

The sun term is now lifted where a local light reaches:
`sunShade *= 1 - lift * lampLit`, with a "Lifts sun shadow" slider (0 restores
the old behaviour).

**`lampLit` uses NWN'S OWN attenuation, not a curve of ours.** Three sources had
to be right, and each one was found by measurement after a guess failed:

1. **Which lights.** `LightManager::GetShadowLights` returns only the
   SHADOW-CASTER subset -- measured: the carried torch and nothing else while
   four lamps lit the area. The real set comes from hooking **`SetLightGL`**,
   the engine's own "enable this light in GL" call, read-only.
2. **Their radius.** The raw `PartLight` field read **0.1 for every lamp**
   through that path, which made the lift die within 10 cm. Ask the engine:
   **`GetLightAdjustedRadius(PartLight const*)`**.
3. **The falloff.** Linear was tried (50% lift at half the radius -- a few lamps
   erased every shadow), then `t^2`, both invented. The real curve is in the
   shipped shaders (`ComputePointLightSource`, data/base_shaders.bif):

   ```glsl
   f   = d*d / range2            // range2 == lightColor[n].a
   att = (1 - f) / (lightMaxIntensityInv + lightFalloffFactor * f)
   ```

   `lightMaxIntensityInv` and `lightFalloffFactor` are read at runtime with
   `glGetUniformfv`, the same trick that fixed the fog.

This lands on the engine's lit pools exactly, because **NWN's point lights do no
occlusion of their own** -- the pools ARE this formula. A screen-space luminance
heuristic was built first and thrown away once the real curve was found.

Light count follows NWN's "Lighting Max Lights" video setting: the shader array
is `vec4[128]`, the panel's "Lights for lift" picks how many upload (default
32), and the NEAREST to the camera win the slots. Every light is tested per
pixel, so that combo is also the cost control.

## ANTIALIASING broke native Windows: glCopyTexSubImage2D on an MSAA framebuffer

**Root cause of the "Linux fine, native Windows no shadows" session.** Found by
the maintainer: NWN's in-game **antialiasing was ON** on the Windows machine.
Turning it off made everything work immediately.

The receiver grabbed scene depth with:

```c
gl::CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], w, h);  // -> DEPTH_COMPONENT24
```

That call is not valid here on two counts: it copies from the READ BUFFER (a
COLOUR buffer) into a depth texture, and it is invalid outright when the source
framebuffer is MULTISAMPLED. NVIDIA's **Linux** driver accepts it anyway --
which is why Linux and Proton worked all along, with the same binary -- while
the **Windows** driver raises `GL_INVALID_OPERATION` (1282). The receiver's
whole draw block was gated on the error flag being clear, so it silently drew
nothing: no shadows, and no debug mode either, because the shader never ran.

```
BLOCKED at glErrorBeforeDraw (a=1282 b=1920 c=1080)
```

**FIXED.** `capture_scene_depth()` uses `glBlitFramebuffer(...,
GL_DEPTH_BUFFER_BIT, GL_NEAREST)`, default ON
(`NWN_SHADOWMAP_DEPTH_BLIT=0` falls back to the copy). Verified in game on Linux
with AA on and off. **Antialiasing can stay enabled on Windows.**

Getting there took three attempts and each failure had its own signature. All
three bugs were in the new code, not in the engine:

| # | Defect | Symptom |
| --- | --- | --- |
| 1 | `glReadBuffer(GL_NONE)` issued while the READ binding was still the ENGINE's, and never restored | whole scene darkened |
| 2 | `while (gl::GetError() != GL_NO_ERROR) {}` -- unbounded | image froze outright |
| 3 | forcing `GL_READ_FRAMEBUFFER` to 0 before the blit | depth all-far, every pixel discarded |

(3) is the one worth remembering. `glCopyTexSubImage2D` reads from whatever is
bound to **`GL_READ_FRAMEBUFFER`**, and the receiver only ever checked the
**DRAW** binding (`oldFbo != 0`). NWN actually renders with `readFbo=1,
drawFbo=0`, so the copy had been implicitly sourcing FBO 1 all along while the
blit took the window, which holds no usable depth. A replacement for a GL call
must reproduce its IMPLICIT state dependencies, not just its arguments.

Evidence for each step, in order:
```
BLOCKED at glErrorBeforeDraw (a=1282 b=1920 c=1080)          <- the original bug
scene depth via glBlitFramebuffer (... readFbo=1 drawFbo=0)  <- (3) identified
scene-depth copy probe 1: range=3581490389..4294967295 non-far=863207/1931520
```
The middle line is why the log now prints BOTH framebuffer bindings.

Diagnostic that paid off: **"only the magenta debug mode shows"**. Modes 1 and 2
sit after `if (d >= 0.999999) discard;` and mode 3 returns before any sampling,
so "only 3 works" is a direct statement that every pixel is discarded -- the
depth capture read all-far. That one observation from the maintainer localised
defect (3) faster than any amount of reading the code.

## An EMPTY cascade layer is not a STALE one (2026-08-10)

The bug that made native Windows render no shadows for a whole session while
Proton, running the SAME DLL, was fine.

The receiver required each live cascade layer to be BOTH stamped with the
current frame serial AND to have a non-zero draw count:

```c
if (g_cascadeDynamicCaptureFrameLayer[layer] != serial ||
    !g_cascadeDynamicCaptureDrawsLayer[layer]) return;   // suppress everything
```

But the stamp is written in the same branch that issues `ClearDepth(1.0)` +
`Clear(GL_DEPTH_BUFFER_BIT)` for that layer. So `frame == serial` already proves
the capture pass ran and wiped last frame's depth, and a layer that then took
zero draws is legitimately EMPTY -- nothing in that slice casts. Sampling it
correctly returns "not occluded".

Conflating "empty" with "did not run" suppressed the ENTIRE receiver -- static
and world-anchored shadows included -- whenever ONE slice happened to have no
casters. Proton always had geometry in the dynamic bucket, so it never tripped.
The freshness test is now the frame stamp alone.

The evidence line, once the gate reported at the point of return:

```
BLOCKED at dynLayer0Stale (a=0 b=8924 c=8924) staticTex=1127 worldValid=1 serial=8924
```

`b == c` (captured this frame) with `a == 0` (zero draws) is exactly the
"empty, not stale" case.

**Two diagnostics were wrong before this one, and both wasted a test run.**
First the gate report listed ~10 conditions and printed all of them green while
the receiver still refused -- the blocking return was not among the ones it
knew about. Then the reason was RECORDED at the return but PRINTED from the top
of the next call, so a log line read `staticTex=1095 ... REASON=noStaticTex`:
a reason from one call beside values from another, which sent the search after
a stale-static-layer theory that was never real. A report that can disagree
with itself is worse than no report. Report AT the decision point, with the
values that decided it.

## Fog occlusion (2026-08-10) -- READ THE ENGINE, DO NOT ASK THE USER

Shadows are composited AFTER the engine has fogged the pixel, so without this a
distant shadow stays crisp and black inside haze that has washed the geometry
out -- a dark smear floating in the fog. "Fade shadows into fog" multiplies both
shadow terms by `1 - fogAlpha`.

The range is read from the engine automatically. Two dead ends came first, and
both are worth not repeating:

1. `glGetFloatv(GL_FOG_START/END)` reads **nothing**. NWN does not use
   fixed-function fog at all; it fogs in its shaders.
2. Scanning `g_nativeTransformSlots` for the uniform found nothing either. That
   list only holds the few programs our own caster replay binds, and
   `glUseProgram` is not interposable here (PATH B), so the engine's fogged
   scene programs never enter it.

What works: the shipped shader source is plain text inside
`data/base_shaders.bif` (`strings` it), and `inc_standard` documents the packing
exactly -- confirmed independently by the [Shader Engine Support wiki
page](https://nwn.wiki/spaces/NWN1/pages/14614573/Shader+Engine+Support):

```glsl
uniform vec4 fogParams;
#define fogEnabled          (fogParams.x)
#define fogStart            (fogParams.y)
#define fogEnd              (fogParams.z)
#define fogInvEndMinusStart (fogParams.w)
float GetFog(float d) { return (d - fogStart) * fogInvEndMinusStart; }
fFogFragCoord = GetFog(-vPosView.z);          // vertex stage
GetFogAlpha(f) = clamp(f, 0.0, 1.0);          // fragment stage
```

`-vPosView.z` is exactly the `z` the receiver already computes for cascade
selection, so the ramp matches the engine by construction.

A uniform's CURRENT VALUE reads back with `glGetUniformfv` -- no hook, no
interception, and none of the dirty-check trouble that forced the matrix work
through the engine's own matrix stack. The programs are found by ENUMERATING GL
program objects (`glIsProgram` over ids 1..4096, rescanned every 600 frames
because programs compile as areas load), caching those that expose `fogParams`.

**The caveat above came true immediately, and it was destructive.** Taking the
FIRST program with `fogEnabled != 0` picked program 143 on Windows, holding
`start=0.0 end=0.1`. The receiver multiplies the shadow by `1 - fog`, so a 10 cm
fog end zeroed EVERY shadow in the game -- and because the Windows panel has no
Diagnostics section, there was no in-game way to see why. Selection is now:

1. **Reject a degenerate range** (`end - start < 5.0` units). Real area fog spans
   tens of units; anything shorter is some other use of the uniform, and
   trusting it makes the feature destructive rather than merely wrong.
2. **Majority vote** among what survives, so one odd program cannot outvote the
   scene shaders that all carry the real range.
3. **Log the full census once** (`[shadowmap][fog] census program N: ...` plus
   the chosen pair and its vote count), so the next surprise is read off the log
   rather than guessed at.

Lesson for any future "read a value out of the engine" feature: a wrong reading
must degrade to the feature being OFF, never to it erasing the output. The fade
had no floor, so a bad number was indistinguishable from a broken renderer.

The manual "Fog start"/"Fog end" sliders survive only as a fallback and are
HIDDEN whenever the engine value is readable; an area with fog off correctly
reports "no fog in this area" rather than an empty manual range.

## Performance controls (all added 2026-08-10)

**These are all in the ImGui panel** (Ctrl+Shift+F11 -> "Performance"), which is
now the primary interface -- the environment variables below remain as startup
defaults and for A/B scripting. Everything applies live except the two
resolutions, which reallocate textures and are staged behind an "Apply
resolution" button.


Replay cost is fundamentally `casters x cascade layers` per frame. On a dense
area that measured **61,092 static draws per frame** (~15k casters x 4 layers),
which is what these exist to attack. Listed cheapest-to-try first.

| Variable | Meaning | Default |
| --- | --- | --- |
| `NWN_SHADOWMAP_CSM_BUCKET_REPLAY=1` | Bind each cascade layer once per BUCKET instead of once per draw call. Validated, large win. | off (set by the `-fast` launchers) |
| `NWN_SHADOWMAP_CSM_CASCADES=1..4` | Fewer layers is a LINEAR saving: 2 layers = half the replay cost. In the panel this and `..._DYNAMIC_CASCADES` are ONE named control (see below). | `3` |
| `NWN_SHADOWMAP_CSM_DISTANCE=<units>` | Cap the shadow fitting distance. Does NOT remove draws by itself (the replay is not per-cascade culled) but packs the layers into a shorter range, so quality rises and a lower cascade count stops being noticeable. `0` = camera far plane. | `0` |
| `NWN_SHADOWMAP_CSM_STATIC_CACHE=0/1` | Refit the cascades (and therefore re-render static) only when the camera moves past a threshold. Static layers holding the current fit generation are skipped and simply re-stamped fresh. | `1` (on) |
| `NWN_SHADOWMAP_CSM_CACHE_MOVE=<units>` | Movement before a refit. The fitted extent is padded by this, so the view stays covered in between. | `2.0` |
| `NWN_SHADOWMAP_CSM_CACHE_TURN=<cos>` | Camera turn tolerated before a refit. | `0.995` |
| `NWN_SHADOWMAP_STATIC_WORLD=1` | **World-anchored static map** — see below. | off |
| `NWN_SHADOWMAP_STATIC_WORLD_SIZE=512..16384` | Its resolution. | `4096` |
| `NWN_SHADOWMAP_STATIC_WORLD_EXTENT=<units>` | Half-size of the covered box (`256` = a 512-unit square). | `256` |

### World-anchored static map (the real fix for static cost) -- VALIDATED

**In-game validated 2026-08-10 on a 32x32 area dense with static and dynamic
meshes: "improved performance a lot".** It is now ON by default in
`run-shadowmap-local-light-shadows.sh`, and toggleable live in the panel.

**It is render-once-per-area for EVERY NWN area, by construction.** NWN tiles
are 10 m and the maximum area is 32x32 tiles = 320 m across; the default
extent of 256 covers a 512 m box, so no legal area can force a re-render
through movement. Only an area change, a sun change, or (impossibly) walking
past 0.6*extent from the centre triggers one. Watch the panel's "Static map
renders so far": it should read 1 per area.

The cascades are fitted to the CAMERA, so static depth stops matching as soon
as you move -- which is why the cache above still re-renders while walking.
But NWN static geometry never moves and the area sun never changes, so the
static map does not need to follow the camera at all. Anchored to the WORLD it
is rendered **once per area** and reused for every later frame; per-frame
static caster cost becomes zero rather than "zero until you walk 2 m". Dynamic
casters stay on the camera-fitted cascade array, where a few hundred draws do
not matter.

The whole trade-off is texel density, hence the user-selectable size. The
startup log prints the actual figure:

| SIZE | over a 512-unit box | VRAM |
| --- | --- | --- |
| 4096 | 12.5 cm/texel | ~64 MB |
| 8192 | 6.3 cm/texel | ~256 MB |
| 16384 | 3.1 cm/texel | ~1 GB |

It re-renders only when the area changes, the sun changes, or the camera moves
past `0.6 * EXTENT` from the map centre (re-centred and texel-snapped, because
without snapping every edge shimmers when it moves). Repeated
`[shadowmap][world] static map rendered #N` lines while walking mean `EXTENT`
is too small for that area. Known limitation: anything the engine classifies as
static but which actually animates (a swinging door) keeps a stale shadow until
the next refresh. `NWN_SHADOWMAP_STATIC_WORLD=0` reverts to the camera-fitted
path for A/B.

## Important current limitations

### Local lights: field decode done, depth PROBE implemented, still no shadow composite

Everything in the sections above is for the directional area sun only. Extra
NWN local lights still do not cast shadows that affect what you see on
screen -- Phase 6b below is a depth-only probe, not a receiver/composite.

A full-coverage solution cannot just add local lights to the four sun
cascades:

- A spot light needs its own perspective depth map.
- A point light needs six depth faces (cube map) or an equivalent projection.
- The renderer must choose a bounded nearby-light set; rendering shadow maps
  for every local light is not viable.

**Phase 6a (read-only census, done):** `LightManager::GetShadowLights(int)` is
hooked to report its CExoArrayList-shaped result's pointer/count and candidate
PartLight pointers. It never writes to the engine.

**Phase 6b (single-light depth probe) is IN-GAME VALIDATED (2026-08-09).**
The PartLight field layout below is confirmed against a live session, not just
by disassembly: the census decoded every candidate with zero failures and
produced self-evidently real values -- plausible world positions on the same
scale as the camera eye, sane radii, and semantically meaningful colours (one
warm torch-like light at `(16.44, 8.17, 2.92) r=13.30 rgb=(1.75,1.50,1.00)`
alongside cool `rgb=(1.30,1.92,1.82)` lights on round placement coordinates).
Selection correctly tracked different lights as the camera moved. The depth
capture reports `buckets-ok=4/4` with ~318k non-uniform texels, and the
maintainer confirmed the dumped PGM looks correct for that light's position
(the player is legitimately absent from it -- the area has several lights and
that one does not face the character). Still depth-only: no receiver, so it
has no effect on screen yet.
Original derivation, retained because it is the evidence trail: disassembling
four independent engine
accessor functions (`PartOutside`, `GetLightAdjustedRadius`,
`GetLightAdjustedColor`/`PartLight::Mat`, `LightManager::GetNearestLights` --
see the offset comment above `PartLightInfo` in `nwn_shadowmap.cpp`) gave a
disassembly-cross-referenced PartLight field layout: world position at
`+0xac`, radius at `+0x70`, RGB colour at `+0x64`, ambient-only flag at
`+0x80`. The census hook now decodes every candidate with `read_part_light()`
and keeps the nearest non-ambient-only one as `g_localLightSelected`.
`capture_local_light_shadow()` (gated on `NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE`)
builds a single wide-FOV (140 degrees) perspective view from that light's
position aimed **straight down**, replays the same accepted
static/alpha/dynamic-body/dynamic-alpha bucket set (0-3) into its OWN private
depth-only FBO/texture (never the sun's `g_fbo`/cascade arrays), and restores
everything afterward. **There is still no receiver -- nothing samples this
depth map and it has zero effect on what renders.** Run
`./run-shadowmap-local-light-probe.sh` in an area with a visible
torch/lantern and check `shadowmap-phase1.log` for
`[shadowmap][local-light] capture frame=... pos=(...) r=... rgb=(...) ...`
lines (do the logged position/colour look like a real torch?) and
`shadowmap_local_light.pgm` (written `NWN_SHADOWMAP_LOCAL_LIGHT_DUMP` seconds
after load, default 45) for a recognisable silhouette -- the same PGM
acceptance test every earlier phase in this project used (both confirmed --
see the validation note at the top of this section).

**Gotcha that silently disabled this probe once:** do NOT gate local-light code
on `g_usable`. That flag belongs to the legacy single-map target created by
`create_target()`, which is only called on the NON-trace path -- and every
launcher enabling this probe runs in trace mode, where `SceneRender_detour`
returns before that call. `g_usable` is therefore permanently false here, and
gating on it produced a perfectly silent failure: census logged normally, no
target, no PGM, no error. The probe owns `g_localLightTargetUsable` instead.

**AIMING THE SINGLE FACE — measured, not reasoned.** The first version aimed
the cone from the light at the frozen camera eye, on the theory that this
guarantees overlap with what is on screen. In game (receiver debug 2) the local
term was zero EVERYWHERE, in every area, including with a carried torch as the
only light source. The aim is wrong exactly when the light is on or near the
player: the axis then runs backwards from the player up to the camera, so the
cone covers the air between them while the ground around the light -- the only
place a shadow would read -- lies outside the frustum and returns "not
shadowed". The face now points **down** (`NWN_SHADOWMAP_LOCAL_LIGHT_DIR`,
default `0,0,-1` in NWN's Z-up world) with a 140-degree FOV
(`NWN_SHADOWMAP_LOCAL_LIGHT_FOV`): at height h above the floor that covers a
ground disc of radius `h*tan(70deg)` = 2.7h, which is what puts a torch's own
surroundings inside the map at all. A point light properly needs six faces;
down is the one worth having first.

**Debug 2 now also reports COVERAGE in blue**, because the old two-channel
version could not distinguish "outside the light's frustum" from "lit" -- both
produce shade 0 -- which is precisely what made the mis-aimed cone hard to
identify. Blue with no green = the cone reaches but nothing is shadowed; no
blue = an aiming/FOV problem, not a shadow problem.

**Phase 6d (local-light RECEIVER) is implemented and NOT WORKING YET -- this
is the open thread on Linux.** The receiver samples the local depth map with
`nwnLocalShadeCov()` and combines it with the sun term by "darkest wins"
(`max`, not sum, so overlapping shadows do not stack into a black hole), using
the same translucent-black presentation and its own strength/bias
(`NWN_SHADOWMAP_LOCAL_LIGHT_RECEIVER/STRENGTH/BIAS`).

Current measured state, via receiver debug 2 in a torch-lit area: **blue
coverage appears on the floor, green does not.** So the cone now reaches the
receiving geometry (the down-aiming fix worked) but the depth comparison
returns "lit" for every covered texel. That is a narrow, well-localised bug --
the remaining suspects are the sign/scale of the stored depth versus the
reconstructed `s.z`, the comparison direction (`GL_LESS` + `s.z - bias`), or
the world map being rendered with a projection that does not match
`g_localLightVP`. Everything upstream is confirmed working: field decode,
selection, `buckets-ok=4/4`, and a recognisable PGM.

Also still open: the single 140-degree face is not point-light coverage. A
wall torch shadows the floor beneath it but not the wall beside it, and only
the nearest non-ambient-only light casts at all. Six-face (cube) coverage and
multi-light selection remain future work, as does deciding how/whether any of
this should begin to replace the legacy stencil shadows.

### Hybrid local-light cube shadows (Phase 7: one-light proof validated)

The current one-face map is intentionally retained as the **Contact** mode: it
is cheap and works for a downward floor-contact shadow, but it cannot represent
the full silhouette from a point light. In particular, a torch beside a
character may capture only the feet/legs while the upper body lies outside the
down-facing cone. Raising the map resolution or changing PCF changes sharpness,
not that missing coverage.

The approved replacement is a selectable hybrid policy:

| Mode | Capture | Intended use |
| --- | --- | --- |
| Contact | Existing one wide, downward-facing perspective map per selected light | Cheap floor-contact shadow; compatibility fallback |
| Cube | Six 90-degree perspective faces per selected point light | Correct full-body silhouettes on floors and walls |

The cube mode has a fixed **Shadow Casting Lights** budget of **1--3**. This
means the maximum number of expensive shadow-map sources, not the number of
ordinary NWN lamps. The injector may census hundreds of engine lights cheaply,
but a lamp earns a cube slot only if its own effective range overlaps a relevant
dynamic actor near the player. All other lamps keep their normal NWN lighting
and cost no injected cube capture or receiver work. As the player walks, a
bounded roster replaces sources with the newly relevant light/actor pairs;
camera movement must never be an input to that choice. The current one-cube
implementation already uses the engine's adjusted, per-lamp lighting radius
for roster eligibility. Its 2x cube far range is depth-coverage margin only;
the future roster must preserve that distinction and must not impose one global
cutoff.

A naive three-light implementation is 18 depth captures per update, so it must
be introduced as a measured, staged path rather than enabled wholesale. It will
keep the existing **dynamic casters only** policy: dynamic body bucket 2 is the
default caster set, alpha bucket 3 remains opt-in, and buckets 0/1 may be
replayed only as NWN warm-up then cleared. Static geometry is deliberately not
captured as a caster, but remains a receiver, so a creature's torch shadow can
land on a wall or floor.

Implementation order:

1. **Done:** allocate an isolated six-layer-per-light depth-array target and
   validate six matrices, depth layers, and state restoration.
2. **Done:** add a cube receiver that selects the face from the
   light-to-receiver vector. Its active depth test now uses the same panel
   controls as Contact: 3x3 PCF radius, texel-scaled normal offset, and
   world-space minimum separation converted with the cube face's actual
   near/far planes. Contact remains the compatibility fallback.
3. **In progress:** replace the one-light proof with a player-centric,
   world-space light/actor roster capped by the overlay's **Local shadow
   sources** value (1--3). Preserve slots with hysteresis; do not give every
   visible lamp a map. Skip faces with no dynamic caster coverage and refresh
   only when a selected light or dynamic caster actually changes.
4. **Then:** expose mode, shadow-light count, resolution, update budget, and
   per-light capture timing in the overlay. Conservative initial target:
   256--512 px per face and one shadowed light.

**Phase 7a/7b status (capture and convention validated):**
one-shot cube-face dump. It allocates a private six-layer
`GL_TEXTURE_2D_ARRAY` for one selected lamp, renders six 90-degree
`+X/-X/+Y/-Y/+Z/-Z` views, and replays the complete 0--3 engine bucket order
per face. The probe also validates six axis points against the capture matrices
(`axes=6/6`, zero centre error). It remains rendering-neutral.

**Phase 7c/7d status (validated in-game, still opt-in):**
`run-local-cube-receiver-debug.sh` enables the isolated cube receiver. The
initial standalone bucket replay captured no dynamic geometry. That was expected
once measured: NWN submits creatures through `Scene::RenderDynamicGeometry`,
not a callable standalone bucket. The accepted implementation therefore arms
the real native dynamic stage, records each normal draw's model-view/projection
uniforms, reconstructs the model from the frozen normal-camera inverse, and
duplicates the same engine draw once into each `+X/-X/+Y/-Y/+Z/-Z` cube face.

The bridge must cover every engine draw entry point, including
`glDraw*BaseVertex` and instanced variants. Before those wrappers were included,
the dynamic stage logged but every face had `draws=0`; after inclusion, the face
dumps visibly contain the player/torch geometry and the ordinary (receiver
debug `0`) game image shows a correct point-light occlusion shadow from the
dynamic character. This is the first validated full point-light silhouette in
the Linux injector.

The proof is deliberately **one selected light** and remains opt-in: it has no
effect on `run-dev.sh`, Contact remains the default path, and static geometry
is not a cube caster. The single-source selection has been moved from
camera-relative order to the captured world-space dynamic anchor, and its
eligibility now uses each lamp's adjusted lighting radius; the cube receiver
captures farther only as a depth-coverage margin.
The persisted 1--3 source control is currently a budget declaration only: the
injector still owns one cube texture. Turning it into a real roster requires
per-slot targets, matrices, freshness, and receiver iteration; it must retain
the rule above that non-overlapping lamps have zero injected shadow work.

Do **not** confuse this with the sun CSM/static-world cache: local cube maps
are dynamic-caster maps and are not a vehicle for reintroducing costly static
caster replay. Do not remove the Contact path until cube mode has an explicit
in-game A/B validation and a measured fallback.

#### Local point-light selection is solved (2026-08-13)

The selected local shadow light now follows **NWN's own authoritative
`LightManager::GetShadowLights()` choice**. Player-carried lights, including the
torch, are selected when the engine selects them. Do not add a camera-distance,
screen-visibility, world-census, or other injector heuristic on top of that
decision: game-owned selection has priority over an inferred replacement.

The remaining local-light defect is **not selection**. It is a projection/depth
association bug: the cube-capture bridge previously recovered a dynamic draw's
model from its normal `m_mv` with the view inverse frozen at `Scene::Render`
entry. Camera yaw/zoom can change the normal draw's current view after that
point, which produces a mismatched model and makes shadows drift, detach, or
overextend. The bridge now records the matrix-stack view inverse alongside each
normal `m_mv` upload and uses that paired inverse for the private cube-face
draw. The entry inverse remains a fallback only. The native stencil result is
the behaviour reference: shadows stay anchored to their caster and fade with
distance rather than stretching unpredictably.

#### Local cube casters use visible dynamic meshes, not stencil proxies (2026-08-13)

NWN's own light selection remains authoritative, but **stencil-shadow proxy
geometry is not the source for local shadow-map depth**. During
`Scene::RenderDynamicGeometry`, the cube bridge now duplicates only original
draws that had an RGB colour-write channel enabled and a non-stencil program.
Depth-only, stencil-only, and learned stencil-program submissions are rejected.
This makes local cube shadows follow the mesh visibly rendered to the player,
rather than an invisible engine-only mesh marked for stencil shadow generation.

The capture status line reports the rejected no-colour, stencil, and no-program
draw counts so this filtering can be verified in an in-game test. This filter
does not affect engine light choice, static sun-cascade casters, or the normal
scene draw itself.

### Legacy stencil shadows are still active

The stencil system (`Scene::RenderShadows`, generated shadow volumes,
`vs_shadowvol`/`fs_shadowplane`) has only been observed. It is **not disabled**
or replaced. Do not skip or rewrite stencil calls until the local-light policy
and final shadow composition are explicitly decided and isolated in an opt-in
probe.

### The settings overlay is now Dear ImGui (Phase 5b superseded, awaiting test)

`Ctrl+Shift+F11` toggles an ImGui panel with live sliders for the sun
composite (strength / bias / cascade blend / PCF radius), the local-light term
(enable / strength / bias), the receiver debug modes, and a read-only status
block (FPS, cascade far clips, sun direction, caster draw counts, selected
local light position/radius/colour). Every control writes the injector's live
globals, which the receiver re-reads when it uploads uniforms, so edits apply
on the next frame with no extra plumbing.

This replaces the hand-rolled 5x7-bitmap overlay, which compiled and toggled
but was never visible and was deferred pending a GL-state diagnosis. Rather
than diagnose a bespoke renderer, the panel now uses a battle-tested one.
`NWN_SHADOWMAP_OVERLAY_LEGACY=1` restores the old path for A/B.

Implementation notes that matter:

- ImGui is vendored under `imgui/` (MIT) and is the project's **only** external
  dependency. It is confined to `nwn_overlay_imgui.cpp`; no ImGui header
  reaches `nwn_shadowmap.cpp`, which only sees `nwn_overlay.h`.
- Only the **OpenGL3 backend** is vendored. There is deliberately no platform
  backend: NWN's SDL event queue is never touched. Mouse/keyboard are POLLED
  via `dlsym`'d `SDL_GetMouseState` / `SDL_GetKeyboardState` (SDL2 is
  statically linked into `nwmain-linux` and exports 639 `SDL_*` symbols) and
  fed to ImGui as synthetic events.
- **Input capture is implemented** (awaiting test). `SDL_PollEvent` in this
  binary is not real SDL code: it is a five-byte thunk
  (`push rbp; mov rbp,rsp; pop rbp; jmp QWORD PTR [rip+rel32]`) that jumps
  through a **writable function pointer**. So the hook patches that pointer --
  the same technique the GLEW entry points use, and strictly better than
  detouring the thunk: nothing is relocated, the original is a plain pointer we
  keep, and undo is a single store (done in `shadowmap_fini`, so unloading
  cannot leave the event pump aimed at freed code). The slot address is decoded
  from the thunk at runtime and the opcode bytes are verified first, so a
  differently-shaped `SDL_PollEvent` refuses to patch instead of corrupting
  something. `mprotect` is applied in case the page is RELRO.
  The detour swallows only what ImGui is actually using
  (`io.WantCaptureMouse` / `WantCaptureKeyboard`), so **clicks outside the
  panel still reach the game**, and it forwards mouse-wheel events to ImGui
  since polling `SDL_GetMouseState` cannot recover the wheel. No SDL headers
  are needed: every `SDL_Event` starts with a `Uint32 type` at offset 0, and
  `SDL_MouseWheelEvent` keeps x/y at 16/20.
  `NWN_SHADOWMAP_OVERLAY_NO_INPUT_CAPTURE=1` disables the swallowing for A/B.
  Two log lines confirm the mechanism at runtime -- one when the patch installs,
  one on the first event actually seen -- because the game could in principle
  pump events through `SDL_PeepEvents`/`SDL_WaitEvent` instead, in which case
  the patch would install cleanly and silently never fire.
- ImGui is compiled `-fvisibility=hidden`. This is an `LD_PRELOAD` library, so
  anything it exports interposes process-wide; the build is verified to export
  **0** ImGui symbols.
- The overlay pass sets `g_overlayPassActive`, **not** `g_inOurPass`. Reusing
  `g_inOurPass` would send the overlay's draws through
  `shadow_before_draw()`'s caster path, which colour-masks out every program it
  does not recognise as an injected caster -- i.e. it would have silently
  muted the entire panel, reproducing the exact bug this port replaces. See the
  re-entrancy rule in `AGENTS.md`: every injector-owned pass needs its own
  short-circuit flag.

### Current compositor is intentionally simple

It darkens every shadowed world receiver with uniform translucent black. It is
not material-aware: it does not distinguish direct diffuse from ambient, use
surface normals, or model multiple light colours. Fog is not an input to the
receiver; apparent range follows frozen camera clip/cascade coverage and normal
area submission.

## Hard-won rules (2026-08-10 session)

Every one of these cost real debugging time. They are cheap to respect and
expensive to rediscover.

1. **Any injector pass that calls `guarded_render_bucket()` must set a flag
   that short-circuits both `SceneRenderDrawBucket_trace_detour` and
   `shadow_before_draw()`.** That helper re-enters our own detour. Existing
   flags: `g_cascadeReplayActive`, `g_localLightPassActive`,
   `g_overlayPassActive`. Without one you get either unbounded recursion and a
   stack overflow (the cascade replay: crashed the client with no message) or a
   silent 16x slowdown (the local-light probe: launched a full 4-layer cascade
   replay per replayed bucket, and presented only as "fps is as bad as before
   the optimisation").
2. **A `sampler2DShadow` must ALWAYS have a comparison-mode depth texture bound
   to its unit.** If it does not, the program is invalid at draw time and the
   driver DISCARDS THE ENTIRE DRAW -- silently, and `glDrawArrays` still
   returns normally. Adding the local-light sampler without binding a
   placeholder killed the SUN shadows in every launcher, including ones with
   local lights switched off. `g_shadowSamplerDummyTex` (1x1, "always lit")
   exists for exactly this; bind it whenever the real map is absent.
   The receiver now checks `glGetError()` after its draw and says so, because
   "receiver drew" previously meant only "we called glDrawArrays".
3. **Do not run the local-light capture before `draw_static_receiver`.** It was
   tried: the receiver's `glCopyTexSubImage2D` scene-depth copy then came back
   completely empty (`range=4294967295..4294967295, non-far=0/1931520`, no GL
   error), its background test discarded every pixel, and both sun and local
   shadows vanished. The capture runs after the receiver and the receiver
   consumes a one-frame-old local map, which is harmless because the light's
   view/projection is world-space. The exact state that breaks the copy has NOT
   been root-caused; the ordering is a deliberate, evidence-backed workaround.
4. **Diagnostic probes must sit BEFORE the code they are meant to exonerate.**
   Receiver debug 1 and 2 sit after the background-depth discard, so when that
   discard was the bug they showed nothing and looked identical to "the pass
   never ran". Debug 3 (solid magenta, before any sampling or discard) is what
   separated "not reaching the framebuffer" from "reaching it and discarding",
   in one run. Similarly, debug 2 originally could not distinguish "outside the
   light frustum" from "lit" -- both give shade 0 -- which is what made a
   mis-aimed local-light cone so hard to spot; it now reports coverage in blue.
5. **When a theory fails twice, stop theorising and add a counter that can only
   be explained one way.** Counting uniform interceptions per (bucket, layer)
   disproved the cascade theory and found the real cause in a single run.

## Discarded / unsafe approaches

- Per-triangle alpha sorting did not solve the original hair issue. Alpha
  sorting is a separate future project from shadow maps.
- Calling `Scene::RenderDrawBucket` standalone is unsafe for arbitrary buckets.
  Use the accepted in-sequence native full-BSP submission route.
- Replaying normal meshes with a guessed camera/light transform caused
  camera-relative shadows, horizon leaks, and broken terrain. Use frozen
  selected-area context and texel-snapped CSM matrices only.
- Directly sampling live game FBO attachments from receivers caused feedback /
  ownership problems. The receiver samples detached injector-owned depth.
- CPU readback bridges update in visible steps and are too slow. The accepted
  path remains GPU-to-GPU every frame.
- Do not substitute stencil-volume draws as depth casters: they are generated
  volume geometry, not original material mesh silhouettes.
- Do not mutate NWN shader files or camera objects. The injector owns targets
  and receiver shader and restores engine state after private passes.

## HISTORICAL -- Next implementation order

> **SUPERSEDED.** The "open thread" here -- the local-light receiver returning lit for every covered texel -- was fixed long ago. Local shadows work on both platforms.


Steps 1-4 of the original local-light plan are DONE and validated (census,
`PartLight` decode, single-light projection, caster capture). What remains:

1. **Fix the local-light receiver.** This is the open thread. Debug 2 shows
   blue coverage but no green: the cone reaches the floor, the depth
   comparison returns "lit" for every covered texel. Suspects, in order --
   the comparison direction/bias (`GL_LESS` with `s.z - bias`), a mismatch
   between the projection the map was rendered with and `g_localLightVP`, and
   the depth range mapping. Everything upstream is confirmed.
2. Extend beyond one 140-degree downward face toward real point-light coverage
   (six faces / cube), and beyond a single nearest light.
3. Decide explicitly how the local-light term combines with sun, ambient and
   the NWN stencil lighting before widening its use.
4. Only then consider opt-in replacement of legacy stencil darkening.
5. Verify the world-anchored static map in game (built, not yet play-tested):
   confirm it renders once per area, and pick a `SIZE`/`EXTENT` that looks
   right at the area scales actually used.
6. Overlay polish is usability work and not a prerequisite for any of the
   above. Its one known gap is Windows input capture (see below).

## Windows build

There is a working cross-compiled Windows port in `win/` (a `version.dll`
proxy, built with mingw-w64). **It is frozen at the maintainer's request --
do not rebuild or modify it until they say otherwise.** Sun shadows, the ImGui
panel and the full cascade capture are confirmed working there. See
`win/README.md` for the install, the platform differences, and the specific
crashes that were fixed; the shared `nwn_shadowmap.cpp` builds for both
targets, so **a Linux change can break the Windows build** -- `cd win && make`
is the check.

## Build / recovery

```bash
make -B
```

Build output is `libnwn_shadowmap.so` in this directory. To run unmodified
rendering, start NWN normally or set `NWN_SHADOWMAP_OFF=1`. No game files need
restoration because the injector changes process state only.
