# NWN:EE Linux shadow-map injector — agent context (refactored workspace)

**This `csm_claude/refactored/` directory is the only active development
workspace as of 2026-08-13.** The maintainer runtime-tested the modularized
single-local-light build and confirmed it preserves the accepted behavior.
All new source edits, builds, launch tests, logs, and documentation changes
belong here. The parent `csm_claude/` tree is historical/reference material;
do not develop or deploy from it.

The exact post-refactor, pre-multilight runtime-confirmed state is frozen at
`../savepoints/2026-08-13-refactored-single-light-runtime-confirmed`. Never edit
that savepoint in place.

The game executable remains outside this source tree. The launchers resolve it
through their documented `GAME_DIR` fallback or `NWN_SHADOWMAP_GAME_DIR`.
Everything owned by the injector (`.so`, objects, logs and settings) stays in
this directory.

Read this before editing `nwn_shadowmap.cpp`.

Read [SHADOWMAP_STATUS.md](SHADOWMAP_STATUS.md) immediately afterward. It is
the current checkpoint: accepted sun CSM path, launchers, tuning, deferred
overlay, local-light limitation, and next implementation order.

For an active in-progress edit, read [CURRENT_TASK.md](CURRENT_TASK.md) next.
It is the compaction-safe, authoritative task checkpoint. Use
[SYMBOL_INDEX.md](SYMBOL_INDEX.md) to inspect only narrow source windows; never
dump `nwn_shadowmap.cpp` broadly. After every atomic patch, record the result and
next test in `CURRENT_TASK.md` so a compacted task resumes without replaying the
conversation. This directory is not a Git repository; do not retry Git commands
here.

## PLATFORM RULE (ABSOLUTE, NO EXCEPTIONS)

**If something works on Linux and does not work on Windows, the change is made
FOR WINDOWS ONLY. If the code is shared, you create a SEPARATE PATH for Windows.
You do not modify the shared path. Period.**

This is not a guideline, a default or a preference. It is the maintainer's
standing instruction and it overrides any judgement about elegance, duplication
or "it should be equivalent".

**Applies to shaders as much as to C++.** A GLSL expression used by both
platforms is shared code. Changing its output scale to fix a Windows symptom is
exactly the prohibited move.

**Why, in the maintainer's own words and in scars:**

- 2026-08-15, the sun-shadow lift. Windows: a torch blanked the sun's shadow
  across the whole area. The fix normalised `att` in the receiver shader --
  ONE expression, shared by both platforms and by every lamp -- and it broke
  Linux, which had been confirmed working twice that same day. Reverted.
- 2026-08-13, the local-light caster cull and the current-program lookup. Both
  were Windows cost cuts applied to shared code; both regressed Linux (flicker,
  then a hang). They now sit behind `NWN_WIN_LOCAL_FASTPATH`.
- 2026-08-15, "Light supported". Replaced by a live read of the engine's
  max-lights on BOTH platforms; Windows reported 64 against a shipping default
  of 8 and local lighting visibly regressed. Reverted.

Three times in three days, the same mistake: a Windows problem fixed in shared
code, breaking a working Linux.

**How to comply.** `NWN_WIN_LOCAL_FASTPATH` in `nwn_platform.h` already exists
for exactly this and is the pattern to follow -- a compile-time switch, Linux
keeping the behaviour it was confirmed with, Windows getting its own. For
shaders, branch on a uniform the host sets per platform, or compile a second
variant. Never "improve" the common expression.

**Before touching anything shared, ask: is Linux currently working?** If yes,
whatever you are about to do is Windows-only.

## Engine authority rule (non-negotiable)

If NWN exposes a decision, use NWN's result. Do not substitute a camera-distance
sort, visibility inference, render-order census, dynamic-anchor ownership, or
other injector heuristic. Missing engine data is a safe no-op, not permission to
guess. In particular, `LightManager::GetShadowLights()` is the only permitted
selector for injected local shadow sources; its non-ambient entries are already
ordered by the engine's priority. `SetLightGL` may be used read-only for the
ordinary local-light/sun-lift census, but must never select, rank, bootstrap, or
replace a local shadow-map source.

**No exceptions currently exist.** One was briefly granted (2026-08-13) for the
Emitter tier, which selected from the `SetLightGL` census by camera distance
while reproducing the 2026-08-10 build. The maintainer withdrew it the same day:
every tier now takes `GetShadowLights()`. What makes Emitter the 08-10 path is
its FILL, not how its lights are chosen. If a future tier seems to need census
selection, this is the precedent -- it was tried, and removed.

`AGENTS.md` and `CLAUDE.md` are intentionally the same file (`CLAUDE.md` is a
symlink).  Update `AGENTS.md`; do not replace the symlink with a copied file.

## STATE OF THE PROJECT (audited 2026-08-12)

Working and maintainer-confirmed in game on both platforms. Windows performance
matches Linux. Both shipping builds are trimmed and verified.

### Inventory

| file | lines | what it is |
| --- | --- | --- |
| `nwn_shadowmap.cpp` | 11690 | everything: hooks, cascades, receiver, local lights, settings, instrumentation |
| `nwn_overlay_imgui.cpp` | 711 | the ImGui panel. The ONLY file that may include an ImGui header |
| `nwn_oit.cpp` | 701 | order-independent transparency experiment. **Inert unless `NWN_OIT=1`** -- reads plain `getenv`, so the shipping defaults table cannot switch it on. Linked but dead weight (~19 KB) |
| `nwn_platform_win.cpp` | 286 | Windows shim: dlsym/mprotect/IAT/WndProc/log redirect |
| `nwn_overlay.h` | 143 | panel <-> injector state struct (pointers to live tunables) |
| `nwn_hooks_core.h` | 97 | cross-module contract (`g_currentBucket`, draw observer, owned-pass flag) |
| `nwn_platform.h` | 93 | platform shim + **`NWN_SHIP`** policy macro |
| `win/nwn_win_symbols.h` | ~105 | Itanium -> MSVC symbol map, 43 entries |

### Build outputs

| command | output | policy |
| --- | --- | --- |
| `make` | `libnwn_shadowmap.so` | development, everything visible |
| `make deploy` | `libnwn_shadowmap_deploy.so` | shipping, for testers (`LINUX_DEPLOY.md`) |
| `make portable` | same, built in `debian:11-slim` via docker | shipping, **for OTHER people's distros** |
| `cd win && make` | `win/version.dll` | shipping, always |

**A binary's glibc floor comes from the build host, not the source.** Built on
CachyOS the `.so` demands GLIBC_2.43 and silently fails to load nearly
everywhere; `make portable` builds the identical source in Debian 11 and lands
on **GLIBC_2.29 / GLIBCXX_3.4.21**. The 2.43 requirement traces to three float
math symbols (`acosf`, `atan2f`, `sqrtf`) plus `__isoc23_*` and `dlopen@2.34` --
nothing the code asks for. An AppImage does NOT address this: it bundles no
glibc, and this is a library injected into the user's own process, not an app.

**Both builds must stay warning-clean** (`-Wall -Wextra`). They are, as of the
audit. A new warning means something real: the last two were a genuinely dead
`g_lampStableRadius` and a Windows-side static guarded on the wrong condition.

### Audit results, 2026-08-12

- 0 warnings, Linux and Windows
- 0 dead statics or uncalled functions (`fault_handler`, `phdr_cb`,
  `shadowmap_init/fini` are address-taken or attributed -- not dead)
- 0 parameter-shadowing locals (`-Wshadow`)
- 0 undefined-function calls, 0 space-indentation, 0 malformed continuations
- all 38 Windows symbols present in the shipped `nwmain.exe` export table
- panel-label `strings` check: the 7 development-only controls are absent from
  BOTH shipping binaries and present in the development `.so`

### Known-inert code, deliberately kept

- `nwn_oit.cpp` -- experiment, off unless `NWN_OIT=1`. Ships in the binary.
- `g_cascadeMaxDistance` (`NWN_SHADOWMAP_CSM_DISTANCE`) -- env-only. **Capping it
  never helped**, because the replay is not culled per cascade; the useful
  distance control is the dynamic-layer count in "Cascades".
- **Area directional-shadow policy (implemented 2026-08-12; live validation
  pending):** Linux `CNWCArea::UpdateShadowingLights()` is observed after the
  engine updates its own state. The verified final-build layout is MoonShadows
  `+0x0a8`, SunShadows `+0x0c8`, IsNight `+0x0dc`, ShadowOpacity `+0x104`
  (`0..100`). These values attenuate only `sunAlpha` in the CSM receiver;
  local point/spot-light shadows remain valid. Bounds-check failures deliberately
  fall back to prior behaviour. The 2026-08-12 calibration matched `+0x104`
  to the engine's `shadowalpha` exactly at 0/50/92/100%, so ShadowOpacity is
  default-on (`NWN_SHADOWMAP_AREA_SHADOW_OPACITY=0` is the A/B fallback).
  SunShadows/MoonShadows now also default-on for directional CSM: day uses Sun,
  night uses Moon, while local lights stay independent. Set
  `NWN_SHADOWMAP_AREA_SHADOW_POLICY=0` for the prior injector-only directional
  behaviour. `NWN_SHADOWMAP_AREA_SHADOW_FLAGS=0` disables capture for
  executable-version fallback.
- **Day/night policy fade:** `NWN_SHADOWMAP_AREA_SHADOW_FADE` defaults to 0.75
  seconds and is a directional CSM **composite-only** visibility transition.
  Do not turn it into a cascade rebuild, cache invalidation, depth-map blend,
  light-vector change, or local-light attenuation. `0` is the explicit
  compatibility/instant-switch setting.
- **Scope boundary — no scripted area sun:** this checkpoint ends immediately
  before `SetAreaLightDirection()` work. It deliberately contains no setter
  hook, timed static-world rebuild, transition texture, or per-bucket moving
  caster experiment. Area `SunShadows`, `MoonShadows`, and `ShadowOpacity`
  remain supported as read-only directional-shadow policy.
- **Local-light cube mode (Phase 7 proof implemented, not production):** the
  current one-face downward map remains Contact mode. An opt-in six-face
  point-light path now captures one selected light into an isolated six-layer
  depth array and samples it in a separate receiver (`run-local-cube-receiver-debug.sh`).
  It does NOT replay dynamic buckets: NWN submits characters through
  `Scene::RenderDynamicGeometry`, so the injector duplicates the real native
  dynamic draws into each cube face after reconstructing model space from the
  frozen normal camera transform. Include standard, BaseVertex, and instanced
  GL draw wrappers or the engine stage will run while cube faces remain empty.
  In-game validation: the player appears in face PGMs and casts a correct
  full point-light shadow. Dynamic body is the caster; alpha remains opt-in;
  static world stays receiver-only. Do not replace Contact yet. One-cube
  selection is now world/dynamic-anchor-relative and uses each lamp's own
  adjusted lighting radius. The cube far plane is 2x only for depth coverage,
  never for roster eligibility. The agreed production model is NWN-style **Shadow
  Casting Lights**: census all lamps cheaply, but allocate only a 1--3
  player-centric light/actor roster to expensive cube maps. Non-overlapping
  lamps must do zero injected cube capture/receiver work. The persisted
  **Local shadow sources** budget now activates 1--3 existing per-slot targets,
  matrices, freshness metadata and receiver iteration. It consumes NWN's
  `GetShadowLights()` order verbatim; **Light supported** is a separate cheap
  lift/census limit and must never cap shadow sources. One source is the frozen
  runtime-confirmed fallback; two sources are the next runtime gate. Publish a
  staged set only when every requested slot received a visible dynamic draw;
  never expose a cleared partial layer. Do not add
  static-world-caster work to this feature.
- `[quads]` / `[nobucket]` census -- built to hunt NWN's blob shadow, which was
  solved another way. Gated on `g_costReport`, so absent from shipping builds.
  Keep: it is the only instrument that sees draws outside the bucket system.

## Scope

This folder contains an experimental local `LD_PRELOAD` injector for NWN:EE's
Linux executable:

- Source: `nwn_shadowmap.cpp`
- Settings overlay: `nwn_overlay.h` / `nwn_overlay_imgui.cpp` (Dear ImGui)
- Vendored ImGui: `imgui/` (MIT; core + OpenGL3 backend only) -- the project's
  ONLY external dependency. Keep it confined to `nwn_overlay_imgui.cpp`: no
  ImGui header may be included from `nwn_shadowmap.cpp`. It is built
  `-fvisibility=hidden` because this is an `LD_PRELOAD` library and any
  exported symbol interposes process-wide (verified: 0 ImGui symbols exported).
  There is no ImGui platform backend on purpose -- input is polled from
  statically-linked SDL, never taken from NWN's event queue.
- Build: `Makefile`
- Output: `libnwn_shadowmap.so`
- Hook helper: `subhook/`
- Cross-platform shim: `nwn_platform.h` / `nwn_platform_win.cpp`
- **Windows port: `win/`** -- a `version.dll` proxy cross-built with mingw-w64
  from the SAME `nwn_shadowmap.cpp`. **The 2026-08-10 freeze is LIFTED**; it is
  maintained alongside Linux and performance-matches it as of 2026-08-12.
  Because the engine source is shared, a Linux-side change CAN break the
  Windows build; `cd win && make` is the check and must stay warning-clean.
  See `win/README.md`.
- **Three build outputs, two policies:**
  `make` -> `libnwn_shadowmap.so` (development, everything visible);
  `make deploy` -> `libnwn_shadowmap_deploy.so` (shipping, for testers);
  `cd win && make` -> `win/version.dll` (shipping, always).
  See the NWN_SHIP section below and `LINUX_DEPLOY.md`.
- Project record and launch instructions: `README.md`
- Current shadow implementation handoff: `SHADOWMAP_STATUS.md`

The target is engine-level directional shadow maps that eventually replace the
engine's stencil-volume shadows.  The present output is a diagnostic shadow
mask, **not** a finished lighting replacement.

## PS4 reference build

The local PS4 dump at
`<PS4-reference-dump>/PS4/CUSA15938/CUSA15670/` is an
architectural reference.  Do not copy proprietary platform code from it or try
to execute it on Linux.  The useful public-facing design facts are documented
in this folder's `README.md`; the complete recovered renderer contract and
source cross-reference are in `PS4_CASCADE_REFERENCE.md`.

The focused Linux renderer-facing symbol inventory and required next read-only
call-graph trace are in `LINUX_RENDERER_MAP.md`. Do not expand scope to all
30,613 dynamic exports: use the mapped scene, camera, light, matrix, and FBO
boundaries first.

Important confirmed design: the PS4 renderer declares four 2048² directional
cascades in separate static and dynamic depth texture arrays, uses comparison
sampling/soft shadow filtering, and renders separate opaque, transparent,
skinned, and skinned-transparent caster passes.  This supports a Linux design
with static/dynamic separation rather than a single all-caster map.

`NWN_SHADOWMAP_CASCADE_TARGETS=1` creates and validates the two four-layer
depth-array targets. Linux caster classification is now proven for the first
useful slice: opaque static bucket `0`, source-classified stock alpha-discard
static bucket `1`, dynamic character bucket `2`, and the validated dynamic
hair bucket `3`. Do not broaden those accepted sets or replay all buckets;
unclassified material paths still require isolated proof.

`NWN_SHADOWMAP_CASCADE_COPY=1` is the first safe population bridge: it copies
the validated existing light depth into dynamic array layer zero on-GPU.  It is
not a substitute for distinct cascade replay or static/dynamic classification.
`NWN_SHADOWMAP_CASCADE_VERIFY=1` is a one-shot diagnostic that readbacks and
compares that layer to the primary depth target; it must never become per-frame.

Do not edit `nwmain-linux`, game resources, or the Proton prefix as part of this
injector.  The injector must operate solely through runtime hooks and its own GL
resources.

## Build and test

```bash
cd "$NWN/bin/linux-x86"
make
```

The normal current diagnostic launch is documented in `README.md`.  Keep the
following experimental switches unset unless a specific test calls for them:

- `NWN_SHADOWMAP_VERTEX_REPROJECT=1`
- `NWN_SHADOWMAP_LIGHT_CASTER_MATRICES=1`

They are retained as controlled experiments and are known to yield sparse or
incorrect light maps.

Before describing a visual path as working, the maintainer must play-test it in
an actual loaded area.  Main-menu rendering and static screenshots are not
sufficient; test orbit, pan, and zoom as well.

## Current architecture

1. `Scene::Render` is detoured with `subhook`.
2. The injector resolves private NWN symbols from the executable's ELF symbol
   table.
3. The regular engine camera render produces scene depth.
4. The accepted diagnostic replays opaque static bucket `0` and
   source-classified static alpha-discard draws from bucket `1` into a private
   static depth array; character bucket `2` and validated dynamic hair bucket
   `3` use a separate private dynamic array.
5. Both targets are cleared, populated, and serial-validated per selected area
   frame, using the frozen entry camera context and recovered light matrices.
6. An injector-owned fullscreen receiver reconstructs world position from scene
   depth and currently writes a red diagnostic shadow mask. It samples only
   cascade layer `2`; it is not final lighting.

The active area scene is identified as `g_areaScene`.  Full-size UI/transition
scenes also exist and must never be treated as the gameplay area merely because
their viewport is large.

## Established facts

- Direct camera-object mutation caused camera snaps, UI loss, disappearing
  characters, and crashes.  Do not revisit it; temporarily write the renderer
  matrix stack and restore state instead.
- Light replay is valid: dumps contain terrain/tree silhouettes and the color
  target agrees exactly with the hardware reference in known-good frames.
- `RenderDrawBucket` cannot safely be called standalone for every bucket.  Use
  the established replay in the `Scene::Render` sequence.
- Directly sampling a live FBO attachment from receivers failed.  Use detached
  copies.
- CPU readback/mirror was a correctness baseline, but it visibly stepped at
  roughly one second and is too slow for the final path.
- The engine's stencil path has been observed only.  `Scene::RenderShadows`,
  `RenderStaticShadows`, `RenderDynamicShadows`, `vs_shadowvol`, and
  `fs_shadowplane` are known; do not disable or rewrite stencil behavior yet.
- The Linux binary confirms that `RenderStaticShadows()` and
  `RenderDynamicShadows()` render generated stencil-volume geometry (including
  `shadowstaticgeom`/`shadowdynamicgeom`), not the original material meshes.
  They are useful for classification and timing only; never feed those draws to
  a depth-map target as a shortcut.

- **The engine's shader SOURCE is on disk and readable.** `data/base_shaders.bif`
  stores the `.shd` files as plain text -- `strings -n 2 base_shaders.bif` dumps
  all of them, `inc_standard` included. That is the authority for any question
  about what NWN's shaders do or how a uniform is packed, and it settled the fog
  work in one command after two rounds of guessing. The [Shader Engine Support
  wiki page](https://nwn.wiki/spaces/NWN1/pages/14614573/Shader+Engine+Support)
  documents the same uniforms and agreed exactly.
- **An engine uniform's live value can simply be read back** with
  `glGetUniformfv`, given the program id -- no hook, no interception, and none
  of the dirty-check trouble that forced per-object transforms through the
  matrix stack. Program ids are enumerable with `glIsProgram` over a bounded id
  range, which is how the fog range is found (`poll_engine_fog`); do NOT expect
  `g_nativeTransformSlots` to contain engine scene programs, since
  `glUseProgram` is not interposable here.

## Status 2026-08-11: Windows performance matches Linux

The long-standing "Windows runs the same area several times slower" problem is
FIXED -- maintainer-confirmed in game, on the area that provoked it. Cause was
`shadow_getenv()` on a per-uniform-upload path, not the port, the hooks, the
symbols, the GPU, MSAA, emitters, the fit cache or any render pass; all of those
were investigated and eliminated, several of them wrongly accused first. See the
HOT-PATH RULES below and the SOLVED section in `SHADOWMAP_STATUS.md`.

Fixed along the way, each a real bug on its own merits:

- The cascade fit cache compared `eng::SunDir` while the fit used
  `g_traceAreaLightDir`, so the static depth cache had **never hit once**. One
  accessor, `cascade_fit_sun()`, now serves fit, test and snapshot.
- The sun-shadow lift loop ran up to 128 iterations per pixel unconditionally,
  including on pixels the sun already lit where the result is multiplied by zero.
- Settings the Windows panel hides were still being persisted there, able to
  fight a shipped default with no control able to undo them.

New Performance controls on both platforms: **Sun shadows** (master A/B --
skips the receiver and the screen capture), **Moving casters**, **Fixed
casters**. With all three off the injector draws nothing, which is the fastest
way to establish whether it is costing anything at all.

## Current validated diagnostic and next blocker

Phase 3r is in-game verified for transform stability while orbiting, panning,
and zooming: the shadows it has captured stay world-aligned, and tree foliage,
an unrelated alpha-discard placeable, the character, and hair all cast. The old
camera-relative receiver leak is not present in this accepted path.

**Newly confirmed separate blocker:** Phase 3r duplicates NWN's already
camera-culled normal draw stream. A caster can therefore disappear from the
private depth map when it leaves the normal camera frustum, even while its
world-space shadow should still be visible. This is caster-submission coverage,
not a light-matrix or receiver-reconstruction defect. Do not mistake a static
camera result for correct shadow-caster culling.

The remaining coverage limitation is intentional: the fullscreen receiver uses
only cascade `2`. The frozen camera fit has clips
`3.7200 / 8.2501 / 16.9720 / 45.0009`; layer 2 therefore only covers the
middle ~17 camera-depth units. A large 32x32 area visibly retains shadows far
into the view because NWN submits many more visible casters, but this does not
mean fog or area size controls the receiver. Fog is not an input to the depth
comparison; the final far boundary still needs real cascade selection.

## Project convention: new settings go in the ImGui panel (2026-08-10)

**Maintainer's instruction: from now on, anything new that is tunable must be
exposed in the ImGui overlay, not only as an environment variable.** Env vars
remain as the startup default and for A/B scripting, but the panel is the
primary interface.

How to add one, following what is already there:

1. Add the live global in `nwn_shadowmap.cpp` and parse its env var at init.
2. Add a pointer to it in `NwnOverlayState` (`nwn_overlay.h`) and populate it
   in `draw_shadow_overlay_imgui()`.
3. Add the widget in `nwn_overlay_imgui.cpp`.

Anything the receiver re-reads per frame (strengths, biases, counts, toggles)
then applies live with no extra plumbing. Anything that owns a GL object -- the
two RESOLUTIONS -- must instead be STAGED: the panel edits a `pending*` value
and raises `applyResolution`, and `apply_resolution_change()` does the
reallocation from the render path where a context is current. Do not reallocate
textures directly from panel code.

## Two builds per platform: NWN_SHIP, not _WIN32 (2026-08-12)

`make` -> `libnwn_shadowmap.so` (development). `make deploy` ->
`libnwn_shadowmap_deploy.so` (what a tester gets). `win/make` is always a
shipping build.

**`_WIN32` and "is this shipped" are different questions and were conflated.**
`_WIN32` had drifted into deciding what a USER may see, which is why a Linux
build could not be handed to anyone. `NWN_SHIP` (nwn_platform.h) now answers
that separately; `_WIN32` is back to platform mechanics only -- dlsym vs
GetProcAddress, SDL_PollEvent vs WndProc, sigsetjmp, GL interposition. Do not
guard user-facing policy on `_WIN32` again.

A shipping build:
- carries `kWinDefaultEnv`, so **no launcher script is needed** -- plain
  LD_PRELOAD works and the env still overrides
- hides every control that REMOVES shadows. They read as "the mod is broken":
  the diagnostic A/B switches, Diagnostics, the frame-cost block, Refit after,
  and the Sun shadows / Local light tuning sections
- writes no `.pgm` dumps and runs no frame-cost instrumentation, which is not
  just noise -- it costs a `glFinish` on every reporting frame
- persists ONLY settings its panel can reach (see the rule in
  `settings_table()`; a hidden control whose value is saved can fight the
  shipped default with no way to undo it, and that has happened twice)

Verify a trim rather than trusting the flag -- panel labels are in the binary:

```bash
for s in "Moving casters" "Fixed casters" "Diagnostics" "Refit after"; do
  echo "$s: $(strings libnwn_shadowmap_deploy.so | grep -c "^$s")"   # want 0
done
```

`LINUX_DEPLOY.md` is the tester-facing readme; send it with the .so. Note the
**glibc floor** (currently 2.43, from the build host) -- it is the most common
reason the library silently does nothing on someone else's distribution.

## LOCAL SHADOW REACH IS THE CAPTURE CONE, NOT THE LIGHT RADIUS (2026-08-15)

Symptom: local shadows formed a small disc around each light, far smaller than
the engine's own stencil shadows. **Cause: the single downward capture cone.**

The map is one perspective face aimed straight down from the light. At height h
above the floor it covers a ground disc of `h * tan(fov/2)`:

| FOV | reach | |
| --- | --- | --- |
| 140 | 2.75 x h | the old default -- a torch ~1.5 units up covered barely 4 units of floor |
| **160** | **5.67 x h** | **current default, maintainer-confirmed "so much bigger"** |
| 170 | 11.4 x h | |
| 175 | 22.9 x h | hard limit |

The receiver ALSO stops at NWN's light radius, so either could have been the
binding limit -- they need opposite fixes, and the two were distinguished by one
launch with `NWN_SHADOWMAP_LOCAL_LIGHT_FOV=172`. It was the cone. Do not assume
that next time; ask the same question the same way.

Wider is not free: the same texels spread over a bigger disc, so shadows soften,
and past ~170 the cone edge distorts badly. 160 is the stopping point unless
someone specifically wants reach over sharpness.

**The FOV drags a shader constant with it.** The receiver's texel-size estimate
was hardcoded `2.747` -- `tan(70)`, correct only at exactly 140 degrees -- so
tuning the FOV would silently under-estimate texel size and bring back acne. It
is now the `nwnLocalTanHalfFov` uniform, derived from the live value. **Any
future FOV change must keep going through that uniform.**

## PANEL: two controls fixed (2026-08-15)

**"Local shadow range" is DELETED, and it never did anything.** Verified before
removal: `g_localCubeRangeMultiplier` was persisted, clamped, logged and
epoch-bumped, and read by NOTHING. It fed the cube probe's eligibility test,
which went in the overnight audit. Control, variable and the `local_cube_range`
config key are all gone.

**"World extent" is a four-step combo, 32/64/128/256** (was a 32..1024 slider).
It is NOT a speed setting and the label now says so: the map is the same size
either way, so this only chooses how much ground that resolution is spread over.
Small = sharper but rebuilt as you walk, and each rebuild is one slow frame, so
it hitches while moving. Large = softer but rebuilds stop. **256 is the correct
ceiling**: it covers a 512-unit box and the largest NWN area is 32x32 tiles =
320 units, so it is built once and never again. Above 256 only wastes
resolution, which is why the old slider's 1024 was a trap. Sharpness at a large
extent comes from "Static world map" instead -- that costs VRAM, not frame time.

## AUDIT 2026-08-14 (overnight): the cube probe and the last diagnostics are GONE

832 source lines and ~23 KB of binary removed, every build warning-clean, all
three binaries rebuilt, and **maintainer-confirmed in game on 2026-08-15**.
Removed:

- **The Phase 7a six-face cube probe, entirely.** Target, capture, dump,
  validate, per-face ledgers, the receiver's `nwnLocalCube*` GLSL (uniforms, both
  functions, debug 4/5 branches), the draw macro's cube branch, its env vars
  (`NWN_SHADOWMAP_LOCAL_CUBE_PROBE` / `_RECEIVER` / `_DUMP` / `_DYNAMIC_DUMP`).
  No production path ever captured a cube -- every tier is one downward face per
  light -- so this was a superseded experiment costing a branch on the hot draw
  path and a live sampler binding.
- **The `selfcheck` block** (face-coverage stats + `.pgm`, ~158 lines).
- **13 write-only globals**, several of them fossils of fixed bugs:
  `g_localDupMsShown` / `g_localDupDrawsShown` (left from the "Shown copies read
  zero" fix), `g_haveAreaShadowObserved` (added and orphaned the same day),
  `g_localLightPickCount`, `g_haveLocalLightSelected`, `g_mtxMode` +
  `NWN_SHADOWMAP_MTXMODE`, `g_engineShadowLightInfo`, `g_engineShadowLightSeen`,
  and others.
- **`kBucketsDynamic` / `kBucketsDynamicNoAlpha`**, unreachable arrays kept alive
  by `(void)` casts.

**KEPT, and each looks removable but is not:**

| thing | why it stays |
| --- | --- |
| `g_localCube{Quality,SourceBudget,UpdateSeconds,RangeMultiplier}`, `kLocalCubeCadenceSeconds` | production settings; "Cube" is historical naming, and the persisted config keys match these names |
| `g_localCubeDynamicSkip{NoColour,Stencil,NoProgram}` | shared with the VISIBLE capture's reject ledger |
| `g_stencilPrograms` / `g_stencilProgramCount` | the classifier that keeps engine stencil proxies out of High's maps |
| `local_cube_visible_dynamic_draw`, `end_local_cube_dynamic_transform` | stale names, live functions on the visible path |
| `nwn_oit.cpp` | reachable (`nwn_oit_frame()`), opt-in via `NWN_OIT=1` |
| `g_casterFullBspNativeReports` | reads as write-only to a naive scan; it is read by `++x < 8` |

`reset_local_cube_dynamic_anchor` became `reset_engine_shadow_light_anchor` --
after the cube fields went, resetting engine shadow-light state is all it did.

**`check_shaders.py` is new and was not optional.** The receiver shader is built
from C++ string literals and only ever compiled inside a live GL context, so a
syntax error in it is invisible until the game runs -- and it is the pass that
draws every shadow. It extracts each shader exactly as the C++ concatenates it
and runs `glslangValidator`. It caught a real half-finished edit during this
audit (the uniforms were removed while both function bodies still referenced
them). Run it after ANY edit to those strings.

## THINGS THAT LOOKED LIKE BUGS AND WERE SETTINGS OR STALE STATE (2026-08-15)

Four separate hours went into symptoms whose cause was configuration or my own
recent change, not the subsystem being blamed. The pattern is worth more than
the individual fixes.

**"Only one of two torches casts" was `cap=1`.** The log said
`candidates=3 cap=1 -> capturing=1` -- NWN offered three, we took one. The stored
light count was 1 from an earlier dev session, and "Local shadow sources" is
`#if !NWN_SHIP`, so a shipping build could not change it. **Shipping now pins it
to 3 and ignores the persisted value.** A hidden control whose value still
persists is a trap: the user sees the effect and has no way to reach the cause.
Any control removed from a shipping panel must have its value pinned too.

**"Lights stopped fading out" was the layer allocation.** The join table
publishes a dropped light as an EXTRA slot while it decays -- that IS the
fade-out. Sizing the depth array to `lights * faces` exactly left it with no
layer, so lights popped. It is `lights + 1` now; the fading cap of one is why a
single spare suffices.

**"The image flickers" was `glGetError`.** `capture_scene_color` returned
`gl::GetError() == GL_NO_ERROR` without draining first, so an error raised by
the ENGINE read as our failure, toggling the self-illumination guard on and off
and changing shadow darkness frame to frame. Drain first, then latch: a capture
that worked once keeps working, and one bad frame must not flip a visible
setting. The receiver's draw gate already documented this exact trap.

**"Local shadows look washed out" was the guard slider at 0.00.** It reads
backwards -- LOWER protects more, because it is the brightness where the fade
STARTS. 0.85 is the default; 1.00 disables it.

## LIGHT PRIORITY: THE READ IS WRONG ON WINDOWS -- ORDERING IS DISABLED

`kPartLightPriorityOff` (`0x8c + kPartLightDelta`) is correct on Linux and
**wrong on Windows**: the log reads `2 0 0` / `0 0 0` and never the `3` a torch
carries. Ordering is gated on `g_localPriorityOffset != 0` and is therefore
inert until the offset is verified. `[prio-scan]` dumps the plausible int32s in
`0x70..0xA8` for that purpose.

**THE TRAP THAT HID THIS:** a STABLE sort on all-equal keys is a NO-OP, so
"reading garbage" and "working correctly" are indistinguishable from the outside.
It was reported working, and the log later showed the values had been zero all
along. Verify a derived value against the log before believing a visual result.

## LIGHT PRIORITY AND THE AREA-POLICY AMBIGUITY (2026-08-15, both confirmed)

### lightpriority lives ON THE PARTLIGHT, at +0x8c

`Gob::SetLightPriority(int)` does NOT store the value on the Gob. Disassembled,
it collects that Gob's PartLights and writes the int into each one:

    mov rdi,[rdi+0x90]              ; the Gob's part container
    call <collect lights>
    loop:  mov DWORD PTR [rdx+0x8c], ebx

So it is a plain read from a pointer the injector already holds -- no hook, no
Gob-to-light mapping, no `List<PartLight*>` ABI to reverse. `kPartLightPriorityOff`
is `0x8c + kPartLightDelta`, bounds-checked to 0..64 so a wrong offset on a
future build degrades to a no-op instead of nonsense.

**Used to ORDER the shadow roster (Windows).** NWN reorders its
`GetShadowLights()` list every few frames -- measured while walking, the same
three lights came back in a different order repeatedly, evicting and re-admitting
each other and restarting their fades. That churn is the flicker. Priority is
stable, so sorting by it (STABLE sort; equal priorities keep NWN's order) fixes
it while keeping BOTH halves the engine's own: eligibility from
`GetShadowLights()`, ranking from `lightpriority`.

TWO THINGS TRIED FIRST AND REVERTED, do not re-attempt:
* **Pinning the player's carried light.** Cannot work: the light from
  `AurSetDynamicProjectionLight` is a `CAurObject*`, the roster keys are
  `PartLight*` -- different objects, never equal. The trace showed `in-list=NO`
  on every frame.
* **Hysteresis** (keep what we held while NWN still offers it). Stopped the
  reordering and regressed everything else: a newly lit torch had to wait for a
  slot, and stale lights survived an area change into interiors. Stability
  bought by ignoring the engine's current answer is staleness.

### "Disable + a light" is AMBIGUOUS, and the area object is the key

`UpdateShadowingLights` makes the IDENTICAL pair of calls in two unrelated cases:

    night + carried light                 -> Disable(area) + SetLight(light)
    day, SunShadows OFF + carried light   -> Disable(area) + SetLight(light)

The second is an INTERIOR. Treating a non-null light as "always the torch swap"
left sun shadows switched on indoors whenever the player walked in holding one.

Linux reads the flag FIELDS and never sees this. Windows has only the observed
path, so it must disambiguate from behaviour: remember what an area said when NO
light was carried (Enable, or Disable with a null light), and use that remembered
answer for the ambiguous case. An area never seen to allow shadows applies the
disable -- far more likely an interior than an outdoor area entered torch-first.

**KEY THE MEMORY ON THE AREA'S OWN CAurObject, NOT THE SCENE.** The Scene pointer
is REUSED across areas: walking outside and back in produced the same pointer, so
the reset never fired and the outdoor "allowed" leaked into the interior. The
toggle's argument is `[area+0x250]`, which changes when the area does. This cost
a full debugging round -- it worked the first time into an interior and broke on
the second, which is exactly what a stale-identity bug looks like.

## LOCAL LIGHTS ARE FILLED THE WAY THE SUN IS (2026-08-15) -- read before touching them

`replay_bucket_into_local_light_layers()` runs from INSIDE the engine's own
`Scene::RenderDrawBucket` hook, right after `CALL_ORIGINAL`, the same site the
cascades use. `capture_local_light_shadow()` computes and publishes slots and
**draws nothing**.

That timing is not a detail, it is the whole design. Riding the engine's visible
camera traversal means the casters ARE the visible geometry -- so the stencil
proxies that contaminated the old standalone replay cannot enter, and the
"buckets 2/3 draw nothing cold" warm-up it needed is unnecessary. Three fill
methods (Legacy / Emitter / High, ~700 lines) were deleted once this landed.

**A FACE IS JUST ANOTHER SLOT.** Same position, radius and fade as its light,
its own view/projection. The receiver's per-slot loop already skips slots the
fragment projects outside, so faces needed ZERO shader changes. Layers =
lights x faces, capped by `kLocalSlotsMax`.

**Faces fix RESOLUTION, the FOV buys REACH.** A perspective cone spreads texels
by angle, so ground footprint per texel grows ~1/cos^3 off-axis: at 170 degrees
the rim is ~1000x coarser than straight down. 1 face/170, 3 faces/140,
4 faces/110 -- each FOV is the azimuth step plus ~20 degrees of overlap.
**2 faces can never work**: 180 apart needs a FOV over 180, the limit is 175.

**Where NWN already exposes a setting, READ IT rather than shipping a second
one.** "Light supported" was deleted in favour of `LightManager::m_nMaxLights`
(exported on both platforms); the light count is dev-only because NWN's "Shadow
Casting Lights" already gates it. Same principle as the engine-authority rule.

**A control whose cost model changed may become meaningless.** "Local shadow
update" was a cadence gate; once the fill happened every frame from the bucket
pass it could only throttle slot re-computation, so it was deleted rather than
left to look meaningful.

## AREA SHADOW POLICY ON WINDOWS: OBSERVE THE DECISION, NOT THE FLAGS (2026-08-14)

**The Windows gap is closed, and maintainer-confirmed in game on 2026-08-15.** It was open since the first Windows build and was
never a missing mapping line -- `nwmain.exe` exports NOTHING of `CNWCArea` (the
only `CNWCArea` strings in it are RTTI type descriptors), and
`UpdateShadowingLights` is not virtual either, so RTTI cannot reach it. No
symbol, no vtable slot, no hook.

**What the function actually is.** Disassembled on Linux at `0x705830`, it is a
decision funnelled into three calls and nothing else:

```
if (!area->aurObject) return;                    // +0x250
allowed = IsNight ? MoonShadows : SunShadows     // +0xdc, +0xa8, +0xc8
AurEnableShadowing(aurObject) | AurDisableShadowing(aurObject)
tail -> AurSetDynamicProjectionLight(creatureLight or 0)
```

All three callees ARE exported on Windows. So the injector observes the decision
the engine already made instead of recovering the fields it made it from -- which
also satisfies the engine authority rule rather than bending it.

**Why the signal is unambiguous, and this is the load-bearing part.** The two
toggles have a second caller,
`CNWCVisualEffectOnObject::EnableHardCodedEffectShadow`, so on their own they are
ambiguous. But EVERY reference to `AurSetDynamicProjectionLight` in the whole
binary is one of three tail jumps from inside `UpdateShadowingLights`
(`0x70589f`, `0x7058ce`, `0x70591f`), and every path reaching it has exactly one
preceding Enable/Disable. It therefore marks the end of an area update and
identifies the toggle as the AREA's. The visual-effect caller never reaches it.
Re-verify that claim against any new game build before trusting this.

**Ownership is decided by whether the FIELD HOOK INSTALLED, not by whether it has
fired.** The Aur hooks run inside that hook's `CALL_ORIGINAL`, so on the first
update `g_haveAreaShadowFlags` is still false and a test on it hands the state to
the observed path for one update on Linux too.

**All three hooks install or none do.** Two of three leaves the pending-toggle
protocol reading a truncated sequence, which is worse than not observing at all.

**Linux runs both and cross-checks.** The field path stays authoritative there
(it also carries opacity); the observed path still runs and warns ONCE if the two
ever disagree. That is the validation for the Windows path, which has no ground
truth of its own. Watch for
`[shadowmap][area] observed Aur* shadow-policy hooks active (cross-checked...)`.

**Known gap: `ShadowOpacity` (+0x104) is NOT recovered.** Windows applies the
on/off policy at unmodulated strength. `shadowalpha` is bound there and the
2026-08-12 calibration matched it to that field at 0/50/92/100%, but it is
creature-driven and orthogonal to the flags, so it was NOT wired in on a
resemblance.


**THE OBSERVED PATH SEES MORE THAN AREA POLICY, and that cut once.**
`UpdateShadowingLights` also has a branch for the player CARRYING A CREATURE
LIGHT: at night it calls `AurDisableShadowing(area)` and hands that light to
`AurSetDynamicProjectionLight`, swapping area shadowing for the torch's own
projection. Read literally, that looks like "this area has no sun shadow", and
on Windows -- which has only the observed path -- a torch deleted the sun's
shadow area-wide from 2026-08-14 until it was fixed on 08-15. Linux never saw it
because the flag fields describe the AREA and know nothing about what the player
holds. A disable followed by a NON-NULL projection light is the carried-torch
swap; leave the policy alone. Ask WHY the engine decided, not just what.

## PROFILING: THE ENGINE SHIPS TRACY, ON BOTH PLATFORMS (2026-08-14)

Not an injector feature -- a capability that was there all along and went unused.
`nwmain-linux` and `nwmain.exe` both have the Tracy 0.10 client compiled in, and
Beamdog ships the server per platform in `bin/<platform>/tracy/`.

Enable in `settings.tml` (`[instrumentation.tracy] enabled = true`, port 8086) and
RESTART -- it is a startup setting. Windows reads its own copy inside the Proton
prefix, NOT the Linux one. The shipped patch runs the client in ON-DEMAND mode, so
it costs nothing until a profiler attaches; leaving it enabled is free.

The engine instruments its own render path -- 192 of its source files appear in
the zone table, including `aurscene.cpp`, `aurscenemanager.cpp`,
`aurlightmanager.cpp`. This is a better instrument than the F3 HUD for anything
about ENGINE cost, because it shows what the injector does to the engine's own
zones rather than only timing our passes. `capture` + `csvexport` beside it are
the right tools for comparing two builds numerically.

DO NOT test for Tracy by grepping for `___tracy` exports: that is the GCC-mangled
C API and MSVC never emits it. It was the reason Windows was wrongly written off
as lacking Tracy. Grep the config key strings instead
(`instrumentation.tracy.enabled`).

## PLATFORM DIVERGENCE IS EXPLICIT, NEVER SHARED (2026-08-13)

**If a change helps one platform and hurts the other, it gets a switch. It does
NOT get shared and then tuned until both are tolerable.** Maintainer's rule,
and it was paid for: every local-light regression on 2026-08-13 came from
breaking it.

The two renderers are not the same machine. The engine reaches the driver by
different routes -- Windows pushes ~4700 `glUniformMatrix4fv` a frame through
our wrappers, Linux effectively none -- so a cost cut that is decisive on one
side can be pure risk with no payoff on the other.

`NWN_WIN_LOCAL_FASTPATH` (nwn_platform.h) is that switch for the local-light
capture. It answers a DIFFERENT question from `NWN_SHIP`: not "is this shipping"
but "is this the platform that needs the Windows-only cost cuts". Behind it,
and only behind it:

| | Windows | Linux |
| --- | --- | --- |
| per-light caster cull | on | off |
| current program | tracked `g_curProgram` | `glGetIntegerv` |
| publish a staged generation | ANY layer drew | EVERY layer drew |

Those three are ONE decision, not three, which is why they share a switch. The
cull makes empty layers ordinary (culling every caster of a light is
indistinguishable from that light having none), so Windows cannot demand that
every layer drew -- one empty layer would discard the whole frame's capture for
every other light, dropping to the bucket-replay fallback and back on
alternating frames. The two fills do not share casters, so that alternation is
VISIBLE FLICKER. Linux has no cull, so an empty layer means the stage really
did not run, and the stricter rule is both correct and what the 2026-08-13
savepoint shipped and the maintainer confirmed in game.

Enabling the cull on Linux is what caused the flicker report; the fix was to
turn the cull off there, not to loosen the publish rule everywhere.

Verify the split survives an edit by preprocessing rather than reading:
`g++ -E ... nwn_shadowmap.cpp | grep -A6 local_current_program` must show
`glGetIntegerv` on Linux and the tracked read under `-D_WIN32`.

## LOCAL SHADOW SOURCES: THREE METHODS IN ONE CONTROL (2026-08-13)

The panel's "Local shadow sources" is a (method, light count) selector, NOT a
light count. The stored `local_cube_sources` value is the COMBO INDEX; helpers
in `shadow_targets.inc` decode it (`local_source_method`, `local_source_lights`,
`local_source_uses_visible_fill`). The index has been renumbered twice as tiers
were added and removed; a stored value from an earlier build still parses but
means something else, and no migration was written.

| index | tier | fill | count |
| --- | --- | --- | --- |
| 0 | Legacy | bucket replay, honours "moving casters only" | 3 |
| 1-3 | Emitter | bucket replay, **all four buckets, nothing cleared** | 1-3 |
| 4-6 | High | per-draw duplication of the visible meshes | 1-3 |

Default is index 4: **High with one light**. High is the only tier whose casters
are what the player sees; one source is where it is affordable.

**THE TIERS DIFFER ONLY IN FILL.** All three take their lights from
`GetShadowLights()` in NWN's priority order, so all three stop at the three
shadow-casting lights the game exposes. Emitter briefly went to 4 while it
selected from the census; when that was withdrawn a 4th slot could never fill,
and the entry was REMOVED rather than left as a silent no-op.

**Emitter is a literal reproduction of the 2026-08-10 savepoint's capture**, and
must stay one. That build replayed buckets `{0,1,2,3}` in order with NOTHING
cleared between them, one downward face, one light. If you change it, diff
against `savepoints/2026-08-10-2221-copy-path-known-good` first.

**Why Emitter cast the engine's invisible proxies, and what actually fixed it.**
`g_localLightDynamicOnly` ("moving casters only") does not exist in the 08-10
source at all -- `grep -c` returns 0 -- and today it defaults ON. It clears the
depth after the static buckets and keeps 2/3, and the stencil proxy submissions
live in those dynamic buckets; with the real static world erased, the proxies are
most of what remains. The tier now forces it off (and forces bucket 3 on, since
08-10 had no alpha-caster switch). **The fill was never the problem** -- buckets
0..3 ARE the visible world geometry. Two rounds were lost to "re-point Emitter at
a different fill"; the flag layered on afterwards was the whole cause.

Cost follows the FILL, which is why the ladder looks lopsided: a bucket replay is
a handful of calls regardless of caster count, so Emitter can afford four lights,
while High pays one draw PER CASTER PER LIGHT (measured 1096 draws at one source,
2600 at three) and caps at three.

The census's `emitter` flag does NOT discriminate -- measured 63 of 63 lights
flagged, because it is derived from fade speed. The tier name is historical; it
does not describe a subset of lights.

Nothing in production captures a CUBE. Every tier is one downward face per light,
and the receiver indexes `layer = slot = light`. The six-face cube
(`kLocalCubeFaces`, `g_localCubeDepthTex`) is a diagnostic probe behind
`g_localCubeProbe`/`g_localCubeReceiver` with its own target and no receiver
binding in normal play.

## LOCAL LIGHT RULES (learned the hard way, 2026-08-13)

**Key local-light state by light IDENTITY, never by slot index.** NWN reorders
its `GetShadowLights()` priority list constantly as the player moves, so an
index tells you nothing about whether this is the same light as last frame.
Every bug in the fading work came from ignoring this: a light published twice at
once, a fade that restarted every capture, a light that could only ever retire
once.

**Do not let a light leave the slot list while it still needs to be drawn.**
Fading a light IN is trivial because it stays in NWN's set and its layer is
captured every frame. Fading one OUT was hard only because it was allowed to
leave -- its slot vanished and its layer was reused. Copying the depth into a
spare layer to work around that broke three times in a row (staging,
`g_haveLocalLightVP`, the light rejoining). Keeping the dropped light in our own
slot list with a decaying level removes the entire class of problem.

**Three fades exist and are not interchangeable:** the day/night one follows the
engine's `shadowalpha`; the per-light one follows `PartLight +0x90`; the
selection one is ours. See `CURRENT_TASK.md`.

**A value that looks static may simply be unobserved.** `shadowalpha` was
dismissed as a startup constant on a run that never crossed a transition, then
turned out to be the entire day/night curve. Before concluding a value does not
move, confirm the measurement window contained the event.

**A frame-count freshness test cannot tell "throttled" from "stopped".**
`local_map_still_fresh()` measures age in SECONDS against the configured cadence
(`max(0.25, interval*3)`). The `serial - g_localLightVPFrame <= 1` rule it
replaced was correct only while the capture ran every frame; with a cadence it
dropped the local term on every frame between captures and the shadow flickered
at exactly the cadence rate. This is a DIFFERENT failure from the publish rule
above: that one is the capture never publishing a map, this one is the receiver
discarding a good one.

**Suppressing an engine pass stops the engine updating its state.** With "Hide
the game's own shadows" on, `Scene::RenderShadows` never runs and `shadowalpha`
cannot be read meaningfully. Any probe of engine state has to account for what
the injector itself has switched off.

## HOT-PATH RULES (learned the hard way, 2026-08-11)

Anything reachable **per draw or per uniform upload** must not call:

- `shadow_getenv()` or `getenv()`
- any `glGet*` (`glGetIntegerv`, `glGetError`, ...)
- anything that allocates, formats a string, or writes to `stderr`

This cost the project its longest debugging session. `trace_matrix_upload()` had
`shadow_getenv("NWN_SHADOWMAP_UNIFORM_TRACE")` as its FIRST condition, and it is
called from `my_uniform_matrix4fv` -- every matrix upload the engine makes,
thousands per frame. On Linux a miss is one `getenv()`; on Windows it is a
`getenv()` miss plus a 27-entry `strcmp` scan of `kWinDefaultEnv` (the table that
makes the `.bat` optional). Same source, an order of magnitude apart by platform,
and **invisible to every toggle in the panel because it is not a rendering
pass** -- switching off casters, cascades, lights and the receiver entirely
changed nothing, which is what finally located it.

`shadow_getenv` is memoised now, so the general case is safe, but do not rely on
that: keep hot paths free of lookups.

Corollaries, all of which cost a test round each in that session:

- **A CPU timer around GL calls measures submission, not work.** `recv` read
  0.10 ms while being the most expensive thing in the frame; `copy`
  under-reported by 100x. Either `glFinish` before reading the clock, or do not
  report the number. The `[shadowmap][cost]` line finishes on the frames it
  prints (one stall per 30 main scenes) for exactly this reason.
- **Report at the point of return.** A gate that lists all its conditions
  elsewhere, or records a reason on one call and prints it on another, is worse
  than no diagnostic.
- **Measure frequency, not blame.** "Which subsystem is slow" is the wrong
  question when the answer is "a cheap function called 5000 times".
- **Do not generalise a census taken in one scene.** A bucket census said
  bucket 6 was the emitter bucket; in another area bucket 6 had 2 draws and the
  emitters were elsewhere. Two rounds were spent on that.
- **Read every field of a comparison before calling two runs identical.** A
  Windows/Linux comparison was drawn with `world_map=8192` against `2048` -- a
  16x difference, printed in both logs, unread.

## Editing guidance

- Prefer small, reversible changes that add observable diagnostics.
- Preserve the known-good red/green CPU/GPU bridge as a fallback while
  experimenting with faster paths.
- Avoid a new transform-reconstruction path without first logging the exact
  matrices passed by the engine in that draw phase.
- Runtime OpenGL IDs are not stable.  Never hard-code program/shader IDs from a
  log (e.g. program 200).
- Restore every GL state that an injector-owned pass changes: framebuffer,
  viewport, program, textures/active unit, depth/stencil/blend/cull state, and
  masks.
- Keep injector modifications opt-in through `NWN_SHADOWMAP_*` flags until the
  path is independently confirmed.
- Update `README.md` when the current test command, verified status, or blocker
  materially changes.

## Immediate next steps

1. **Phase 4a is in-game validated (2026-08-09):**
   `run-shadowmap-four-cascade-capture.sh` replays each accepted Phase 3r draw
   into all four layers of its own static/dynamic array. Clearing and freshness
   counters are per-target **and per-layer**; no receiver is drawn, so the game
   should look unchanged. It writes eight `shadowmap_cascade_{static,dynamic}_cN.pgm`
   files and `[shadowmap][csm] multi capture ...` lines. The first run wrote
   all eight layers at frame 2: static draws=6/layer, dynamic draws=9/layer,
   each with non-uniform depth. This validates layer ownership only; it does
   **not** solve normal-camera frustum culling.
2. **Phase 4b is the read-only culling census:**
   `run-shadowmap-caster-cull-trace.sh` hooks `ManageSceneBSP(Scene*)` only
   long enough to log, after the engine's own `ProcessTriMeshParts()` work,
   the counts of original `meshshadowbucket` entries, stencil-volume
   `staticshadowbucket` entries, and the engine's three cull counters. It has
   no target, receiver, replay, shader, matrix, or GL-state mutation. Orbit a
   visible tree toward and away from a screen edge; changing `meshshadow`
   counts proves exactly how the normal camera limits the duplicated Phase 3r
   caster set. The disassembly already establishes the mechanism:
   `ProcessTriMeshParts(..., List<Plane>&)` calls `PartOutside` using the
   normal camera planes before `AddPartToMeshBuckets()` adds that same mesh to
   `meshshadowbucket`. `staticshadowbucket` contains projection volumes, not
   reusable original meshes. The ordinary trace itself stops at its 90-frame
   cap, but this compact census stays active afterward and spends its log budget
   on one baseline plus count changes only.
   **In-game result (2026-08-09):** after the ordinary trace stopped,
   `meshshadowbucket` changed repeatedly from 4 through 15 entries and back
   down while the camera orbited; `staticvolume` remained 0 at this particular
   post-BSP observation point and the three exposed counters stayed 0. The
   changing original-mesh count is direct evidence that Phase 3r cannot achieve
   full light-space caster coverage by duplicating this list.
3. **Phase 4c is the read-only full-BSP candidate census:**
   `run-shadowmap-full-bsp-census.sh` calls the executable's plain
   `BSPTraverse(BSPNode*, callback, user)` only after `ManageSceneBSP` has
   completed normally. The walker has no camera or plane arguments. The exact
   engine callback first resolves `nodedata*` from `BSPNode +0x70`; its regular
   `PartTriMesh*` array/count live at `nodedata +0x20/+0x28` (and a separate
   primary mesh pointer is at `+0x90`). The probe reports both before logging
   `[shadowmap][fullbsp]` beside the ordinary
   camera-culled `meshshadow` count. It never retains a mesh pointer, calls a
   mutator, submits a draw, or touches GL state. Orbit/pan after the ordinary
   trace reaches 90 frames. A stable `tri-candidates` count while `meshshadow`
   varies proves that an engine-owned non-camera candidate set is enumerable;
   it does **not** authorize a replay yet.
   For the visual comparison, `run-shadowmap-full-bsp-red-diagnostic.sh` keeps
   the known Phase 3r red receiver active while it records both counts. Red is
   still produced only by the camera-culled Phase 3r caster path; a red shadow
   disappearing while the full candidate total stays stable is expected proof,
   not a regression in the census.
   **Confirmed result (2026-08-09):** in the small test area, 403 BSP nodes all
   resolve to nodedata, containing 15 regular array parts and 16 primary parts
   (31 stable full candidates, zero invalid payloads). The ordinary
   `meshshadowbucket` varied from 2 to 15 while that full total stayed 31. This
   is the proof to preserve when implementing the non-camera static source.
4. **Phase 4d native full-static submission is in-game validated (2026-08-09):**
   `run-shadowmap-full-bsp-native-static.sh` runs immediately after native
   `ManageSceneBSP()`, when NWN has filled its normal camera-culled pending
   mesh lists. It walks the verified full source and calls only native
   `AddPartToMeshBuckets(PartTriMesh*)` for the missing static candidates.
   NWN's later `Scene::AddPartsToDrawBuckets()` then resets/builds the real
   DrawBucketManager once and its ordinary `RenderDrawBucket()` calls give the
   GL wrapper adjacent, exact per-object transforms for the light duplicate.
   The first version manually invoked `AddPartsToDrawBuckets()` at Scene entry;
   it completed frame 2 but crashed in that engine helper on frame 3, proving
   the manual lifecycle was invalid. The corrected path ran through all 90
   bounded area frames, appending a stable 31-part source every frame without
   disappearing red shadows or a crash. Never manually build or replay draw
   buckets from this hook. It is static-only/red-only and must log
   `[shadowmap][fullsubmit] submitted=... pending-meshshadow=...`.
   Dynamic creatures/hair remain on their separately proven route.
5. **Phase 4e full-static fan-out is in-game validated (2026-08-09):**
   `run-shadowmap-full-bsp-four-cascade-capture.sh` combines the now-proven
   Phase 4d source with existing Phase 4a fan-out. It is depth-only: receiver
   suppression is deliberate, and success is four non-uniform
   `shadowmap_cascade_static_c{0..3}.pgm` images plus per-layer fresh draw
   records. The test produced 15 native static draws per cascade layer, all
   non-uniform; dynamic layers were correctly empty. That unblocks the
   camera-depth cascade selection/compositing proof.
6. **Phase 4f frozen-depth static selection is in-game validated (2026-08-09):**
   `run-shadowmap-full-bsp-csm-static.sh` keeps the Phase 4d native source and
   Phase 4e fan-out, but enables the first visible static CSM receiver. The
   shader reconstructs world position with `viewProjectionInverse`, computes
   `-view.z` with the same frozen view used by cascade fitting, and selects the
   first `clipFar` that contains it. It samples that layer's matching `lightVP`.
   It is red-only/static-only with hard boundaries, and remained anchored under
   orbit/pan/zoom in game. The stable full source reported 31 candidates and
   fresh static draws across all four layers.
7. **Phase 4g dynamic-body CSM is in-game validated (2026-08-09):**
   `run-shadowmap-full-bsp-csm-static-dynamic.sh` adds only the proven character
   body bucket (2) to the Phase 4f depth-selected receiver. It fans bucket 2
   into all four *dynamic* layers using the native per-draw transform, then
   samples the same frozen camera-depth-selected layer as the static array. The
   receiver requires a fresh draw in every static and dynamic layer; missing
   dynamic work suppresses the red diagnostic rather than using stale body depth.
   The body shadow was stable while orbiting/panning/zooming. Bucket 3/hair,
   split blending, PCF/bias tuning, and final lighting remained out of scope for
   this narrow validation.
8. **Phase 4h all currently-mapped casters is in-game validated (2026-08-09):**
   `run-shadowmap-full-bsp-csm-all-casters.sh` enables all currently mapped
   caster classes: static opaque bucket 0, static alpha-discard bucket 1,
   dynamic body bucket 2, and dynamic alpha/card bucket 3. Bucket 2 clears the
   dynamic array for the current scene serial before bucket 3 adds its original
   alpha-discard depth. This is still red-only and hard-split CSM; do not add
   new caster buckets, filtering, split blending, or lighting modulation before
   the all-caster stability test was confirmed. The test passed: all mapped
   static/dynamic alpha and opaque casters rendered correctly with acceptable
   cascade resolution/range and stable camera motion.
9. **Phase 5a dark presentation composite is in-game validated (2026-08-09):**
   `run-shadowmap-full-bsp-csm-shadows.sh` uses the Phase 4h caster set but
   outputs translucent black with normal alpha blending instead of opaque red.
   It is opt-in (`NWN_SHADOWMAP_CSM_COMPOSITE=1`) and preserves the red launcher
   as an A/B reference. Strength is `NWN_SHADOWMAP_CSM_STRENGTH=0..1`, default
   `0.42`. The receiver saves/restores full blend state as well as the existing
   FBO/program/depth/texture state. This is a presentation proof only: no
   cascade blend bands, normal-offset bias, or material-aware shadow strength.
   The depth arrays already provide hardware 2x2 PCF through linear comparison
   filtering; do not add a second manual PCF loop without a measured need.
10. **Phase 5b injected settings overlay is deferred:**
    `Ctrl+Shift+F11` toggles a compact injector-owned panel after the receiver
    pass, so it cannot enter the captured scene depth. It renders an internal
    5x7 bitmap font—no external UI/runtime dependency—and uses SDL keyboard
    state only, never mutating NWN's event queue. `Ctrl+Shift+Up/Down` selects;
    `Ctrl+Shift+Left/Right` edits output/strength/bias/lambda; `Ctrl+Shift+Enter`
    toggles output; Escape closes. Mouse capture is explicitly later work once
    SDL event dispatch can be intercepted safely. The hotkey state is observed
    in-game but the panel is currently invisible, so leave this isolated from
    shadow work until a dedicated GL-state diagnosis is performed.
11. **Phase 5c opt-in cascade overlap blend is implemented, awaiting test:**
    `run-shadowmap-full-bsp-csm-soft-shadows.sh` sets
    `NWN_SHADOWMAP_CSM_BLEND=0.75`. The receiver cross-fades the already-valid
    current and next cascade within that narrow world-space band. Default zero
    retains the known-good hard split path exactly. It is receiver-only and
    does not alter caster submission, target ownership, or camera context.
12. **Phase 5d visible PCF filter is implemented, awaiting test:**
    `run-shadowmap-full-bsp-csm-filtered-shadows.sh` adds a 3x3 manual PCF
    receiver footprint with a restrained default `NWN_SHADOWMAP_CSM_PCF_RADIUS=0.75`.
    This is distinct from Phase 5c: 5c only blends cascade boundaries, while
    5d should visibly soften the silhouette edge itself. Radius zero falls back
    to the existing hardware 2x2 linear comparison sample. No new depth target
    or caster route is introduced.
13. **Phase 6a local-light candidate census is implemented, awaiting test:**
    `run-shadowmap-local-light-trace.sh` (parent `linux-x86/` only, not copied
    into this fork) is read-only and runs with no CSM targets or receiver. It
    records the bounded `LightManager::GetShadowLights` CExoArrayList
    identity/count and up to eight opaque candidate addresses in an area with
    torches/local lights.
13b. **Phase 6b local-light PartLight field decode + single-light depth PROBE
    is IN-GAME VALIDATED (2026-08-09).** The offsets below are confirmed
    against a live session: every candidate decoded, values self-evidently
    real (plausible positions/radii, a warm torch-like light beside cool ones
    on round placement coordinates), selection tracked lights as the camera
    moved, capture reported `buckets-ok=4/4` with ~318k non-uniform texels,
    and the maintainer confirmed the PGM matches that light's viewpoint.
    Still depth-only -- no receiver, no on-screen effect.
    Two gotchas worth keeping: (a) do NOT gate this code on `g_usable`, which
    belongs to `create_target()`'s legacy target and is permanently false in
    trace mode -- doing so silently disabled the whole capture with no error;
    (b) `g_localLightPassActive` must be set around its bucket replays (see
    the re-entrancy rule in 13c). Original derivation follows.
    Superseded the caution in item 13 above ("do not read PartLight fields")
    once four independent engine accessor functions were disassembled and
    cross-referenced -- see the offset comment above `struct PartLightInfo` in
    `nwn_shadowmap.cpp` for the exact evidence (`PartOutside`,
    `GetLightAdjustedRadius`, `GetLightAdjustedColor`/`PartLight::Mat`,
    `LightManager::GetNearestLights`, all four reading the SAME position
    `+0xac`/radius `+0x70`/colour `+0x64` layout). `read_part_light()` decodes
    position/radius/RGB/ambient-only for every census candidate; the nearest
    non-ambient-only one is kept as `g_localLightSelected`.
    `capture_local_light_shadow()`, gated on
    `NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE`, builds one wide-FOV (100 degree)
    perspective from that light toward the frozen camera eye and replays
    buckets 0-3 (the same accepted static/alpha/dynamic-body/dynamic-alpha set
    as the sun CSM) into its OWN private depth-only FBO/texture -- never
    `g_fbo` or the cascade arrays. **Depth-only: there is still no receiver, so
    this has zero effect on what renders.** Run
    `./run-shadowmap-local-light-probe.sh` (this fork) and check
    `shadowmap-phase1.log` for `[shadowmap][local-light] capture frame=...`
    lines and `shadowmap_local_light.pgm` for a recognisable silhouette --
    same PGM acceptance test as every earlier phase. Type (spot vs. point),
    transform beyond position, and attenuation curve remain unmapped; this
    probe deliberately sidesteps that by using a single fixed wide FOV instead
    of a true 6-face point-light projection, and by aiming at the camera
    rather than needing a light "forward" direction. **Not yet confirmed
    against a live session -- do not treat the offsets or the probe's output
    as validated until a real run's log/PGM has been inspected.**
13c. **Phase 6c cascade bucket-level replay (performance) is IN-GAME
    VALIDATED (2026-08-09): correct shadows, "buttery smooth" per the
    maintainer. `NWN_SHADOWMAP_CSM_BUCKET_REPLAY=1` /
    `run-shadowmap-full-bsp-csm-filtered-shadows-fast.sh` is now the
    recommended path.** Triggered by a
    maintainer-reported "really low performance" report on a busy small area
    (100s of meshes). Diagnosed by elimination, not guessing: dropping
    `NWN_SHADOWMAP_SIZE` 2048->1024 and `NWN_SHADOWMAP_CSM_PCF_RADIUS=0` on
    the default path changed nothing, ruling out GPU/fill-rate. Code read
    then found the actual cost: `DUPLICATE_CASCADE_LIGHT` (the accepted
    fan-out) rebinds the cascade FBO, calls `glFramebufferTextureLayer`, and
    re-queries 6 pieces of GL state via `glGet*`/`glIsEnabled` on **every
    (draw call, cascade layer) pair** -- thousands of driver calls/frame on a
    busy area. `NWN_SHADOWMAP_CSM_BUCKET_REPLAY=1`
    (`run-shadowmap-full-bsp-csm-filtered-shadows-fast.sh`) adds a parallel,
    opt-in path: `replay_bucket_into_cascade_layers()` binds each cascade
    layer's FBO ONCE per bucket, then replays the WHOLE bucket via
    `guarded_render_bucket()` (the same re-entrant call already proven by
    `render_from_light()` and the Phase 6b local-light probe), applying the
    per-layer light transform through the ENGINE'S MATRIX STACK.
    **CRITICAL ENGINE FACT, learned the hard way here: NWN dirty-checks its
    uniform uploads, so a per-object light transform CANNOT be applied by
    intercepting `glUniformMatrix4fv` during a replay.** The first version of
    this path did exactly that -- reusing the old per-draw path's "recover
    model via the frozen view inverse, recombine with the light's view/proj"
    maths, just inline at upload time -- and it silently missed 100% of
    static geometry, because re-drawing an object whose model matrix has not
    changed uploads nothing at all. Measured with per-(bucket,layer)
    interception counters: static buckets `sub-mv=0` on EVERY layer including
    layer 0, dynamic buckets a full count (skinned geometry recomputes every
    draw, so it always uploads). Symptom: four byte-identical static cascade
    layers (same range, same 3,070,238 texels), correct-looking dynamic
    layers, and no visible shadows at all. The fix was to drop uniform
    interception entirely and use the Phase 3 mechanism instead -- write the
    light view/projection directly into matrix-stack entries `MTX_VIEW` /
    `MTX_PROJ` per layer (byte-exact save/restore, flag both dirty, and put
    the current matrix mode BACK before drawing), letting NWN derive and
    upload each object's matrices itself; changing the view makes those
    uploads genuinely dirty, so static and dynamic both come out right. Do
    NOT reintroduce uniform substitution alongside it: that double-transforms.
    Post-fix the four static layers report distinct ranges/texel counts
    (`4194298 / 4194304 / 4194304 / 3902792`).
    Both mechanisms remain mutually exclusive per bucket
    (`SceneRenderDrawBucket_trace_detour` turns the old per-draw flags OFF
    when the new path handles that bucket) and populate the exact same
    `g_cascadeStatic*/g_cascadeDynamic*CaptureFrameLayer/DrawsLayer` state the
    receiver (`draw_static_receiver`) and PGM dumps already read, so no other
    code needed to change. **The default (per-draw) path is untouched and
    unreachable unless the flag is unset; keep it as the A/B reference.**
    **RE-ENTRANCY RULE — this trap bit twice, once as a crash and once as a
    silent 16x slowdown. ANY injector pass that drives geometry through
    `guarded_render_bucket()` MUST set a flag that short-circuits both
    `SceneRenderDrawBucket_trace_detour` and `shadow_before_draw()`**, because
    that helper calls the hooked `Scene::RenderDrawBucket` and therefore
    re-enters our own detour. Existing flags: `g_cascadeReplayActive` (cascade
    replay) and `g_localLightPassActive` (Phase 6b local-light probe). Without
    the guard the detour re-decides "replay this bucket" and either recurses
    `kCascadeCount`-deeper per level until the stack overflows (the crash), or
    launches a full 4-layer cascade replay per replayed bucket (the local-light
    probe's 16 extra whole-bucket renders per frame, which made it feel exactly
    like the pre-optimisation build). Skipping `shadow_before_draw()` is
    correctness-neutral but removes real per-draw overhead: these private
    targets are depth-only with no colour attachment.
13d. **Phase 6d local-light RECEIVER is implemented and NOT yet working.**
    `run-shadowmap-local-light-shadows.sh`. The receiver resolves the sun and
    local terms separately and combines them with `max` ("darkest wins", so
    overlapping shadows do not stack to black); this restructuring was
    necessary because the old code discarded on the sun test alone, which would
    have made every local-light shadow invisible regardless of correctness.
    **Measured state: receiver debug 2 shows blue coverage on the floor and NO
    green** -- the cone reaches the receiving geometry but the depth comparison
    reports "lit" everywhere. Upstream is all confirmed (decode, selection,
    `buckets-ok=4/4`, recognisable PGM), so the bug is in the comparison
    itself: check the direction/bias (`GL_LESS`, `s.z - bias`), whether the map
    was rendered with exactly `g_localLightVP`, and the depth range mapping.
    Two aiming facts already established by measurement, do not redo them:
    aiming the cone at the CAMERA gives zero coverage everywhere when the light
    is on the player (the axis points backwards into empty air), and the single
    face now points DOWN at 140 degrees, which is what put the floor inside the
    map at all (`NWN_SHADOWMAP_LOCAL_LIGHT_DIR` / `_FOV`).
13e. **Performance work (2026-08-10), see SHADOWMAP_STATUS.md for the full
    table.** Replay cost is `casters x cascade layers`; a dense area measured
    61,092 static draws per frame. Added: `NWN_SHADOWMAP_CSM_CASCADES` (linear
    saving), `NWN_SHADOWMAP_CSM_DISTANCE` (fit cap; improves quality, does not
    itself cut draws because the replay is not per-cascade culled), a static
    cascade cache with fit hysteresis, and -- the real fix -- a **world-anchored
    static map** (`NWN_SHADOWMAP_STATIC_WORLD`, size 512..16384) rendered once
    per area, since static geometry and the area sun never change.
    **VALIDATED in game 2026-08-10 on a 32x32 area dense with static and
    dynamic meshes ("improved performance a lot"); now on by default in
    `run-shadowmap-local-light-shadows.sh`.** Note it is render-once for EVERY
    legal area by construction: NWN's maximum area is 32x32 tiles = 320 m, and
    the default extent of 256 covers a 512 m box, so movement can never force a
    re-render.
13f. **The settings overlay is now Dear ImGui** and works (Phase 5b's
    hand-rolled bitmap overlay is superseded but kept behind
    `NWN_SHADOWMAP_OVERLAY_LEGACY=1`). ImGui is vendored in `imgui/`, confined
    to `nwn_overlay_imgui.cpp`, built `-fvisibility=hidden` (verified 0 exported
    ImGui symbols -- this is an LD_PRELOAD library, anything exported
    interposes process-wide). No ImGui platform backend: input is polled from
    statically-linked SDL, never taken from NWN's event queue. Linux also
    captures the input the panel uses by patching the `SDL_PollEvent` jump
    thunk's function pointer; on Windows that hook is refused (no trampoline)
    so clicks reach both panel and game there.
14. The stencil pass remains a useful read-only reference for its caster
   selection/timing, but it emits shadow volumes rather than original meshes
   and cannot simply be reused as a depth caster pass.
6. Keep the red diagnostic but select the correct cascade from the
   reconstructed camera depth and its matching `lightVP`. First prove hard
   transitions are anchored while orbiting/panning/zooming; only then add a
   small split overlap/blend.
7. Expose range, resolution, split lambda, bias, and filtering only after the
   four-layer diagnostic is stable. Resolution/range controls cannot repair a
   receiver that still samples one layer.
7. Replace red diagnostic output with filtered lighting darkening only after
   cascade selection is verified. Keep Phase 3r as the safe regression
   launcher and do not disable stencil shadows yet.
