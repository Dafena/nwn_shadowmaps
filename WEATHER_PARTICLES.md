# Private shader rain — validation checkpoint

This is an **enabled-by-default preview in shipping builds** (opt-in in developer builds), not a fully accepted replacement for NWN weather.
The user confirmed visible private rain on Windows and requested native rain
hiding on 2026-10-11. This separate comparison checkpoint is reversible and
retains engine weather authority, native lightning, snow and unrelated VFX.

## Implemented

- Shared OpenGL 3.3 vertex/fragment shaders; no geometry/compute extension.
- World-cell-anchored trajectories and stable staggered phases. The camera
  chooses nearby cells and billboard orientation, not weather or collision.
- Bounded height-field sweeps against the existing completed static precipitation
  map, then impact splashes at that collision point. Separate non-comparison
  samplers read height without changing the map's shadow comparison parameters.
- Each impact is a droplet-impact sequence rather than a single puff (2026-11-10,
  runtime verification pending): a surface-aligned contact flash and expanding
  ring, plus an upright crown sheet, rebound jet and five ballistic secondary
  droplets. Both quads are animated per fragment from the same simulated hit.
  This adds one more vertex pass over the drop grid while splashes are enabled.
- Sloped-surface splash orientation, discontinuity rejection, and no impacts
  over empty depth. Highest exposed static surfaces receive impacts, rather
  than the floor under a roof.
- Completed scene depth testing (including characters), native fog-distance
  fading, decoded area lighting, and enabled lamp data refreshed this frame.
- No new large targets, BSP submissions, models, or module edits. Rain draws
  after completed area/OIT work and before overlay/local staging.
- No rain in interiors, clear/snow weather, zero density, or with incomplete or
  wrong-area shelter data. Existing wet-ground accumulation/ripples are unchanged.
- Scoped GL state restoration. Blend/color-mask changes target buffer zero;
  native framebuffer alpha is preserved.

## Enable

Build/install the normal artifact as described in BUILD.md. Restart the client
after installing a DLL; never overwrite a running client's loaded proxy.

In shipping builds, open **Performance → 3D Custom Rain** (enabled by default).
In developer builds, expand **Shader rain (preview)** and enable
**Private rain and splashes**; its status reports why the pass is waiting.
**Hide native rain (preview only)** defaults off. With both boxes checked,
Windows suppresses matching native rain draws after the private pass is ready.
The first frame remains native; missing prerequisites restore native rain.
Uncheck either box to restore it immediately. The pass status reports
`drawing (native rain hidden)` only when matching native draws were intercepted;
`drawing (native retained)` means no native rain was suppressed in that frame.
Controls are live, saved by the existing settings system, and reset with the
panel defaults. Linux shipping and Windows both expose them.

Environment defaults (saved panel settings take precedence):

| Variable | Default | Range |
| --- | --- | --- |
| NWN_SHADER_RAIN | 1 shipping / 0 developer | 0 / 1 |
| NWN_SHADER_RAIN_HIDE_NATIVE | 0 | 0 / 1; only active with preview |
| NWN_SHADER_RAIN_BUDGET | 6596 | 256–262144 |
| NWN_SHADER_RAIN_RADIUS | 40 | 8–64 world metres |
| NWN_SHADER_RAIN_WIND_X | 0 | -8–8 metres/second |
| NWN_SHADER_RAIN_WIND_Y | 0 | -8–8 metres/second |
| NWN_SHADER_RAIN_SPLASHES | 0.250 | 0–2 |

Wind is manual, **not a decoded NWN wind vector**. Density remains
NWN-authoritative. Budget is rounded down to a square grid, and density
selects a stable subset of its cells.
The logarithmic slider and Exact drop budget input allow extreme storms without
making low-budget tuning awkward. Try 32768 first and increase while checking
frame times. Above 32768 the panel displays a GPU-cost warning. The ordinary
default is 6596; the cap is not a promise of acceptable FPS at maximum.

Open-air cells now retain falling streaks even when the static map contains no
ground. This matters for Storm Crown's empty cc tileset and non-static
st_floor_tile placeables. Such cells use a camera-local vertical rain volume,
not a guessed floor or collision plane. Splashes still require an actual
static-map intersection. Non-static floor placeables do not yet contribute
splash collision; this change does not expand the existing static BSP capture.

For Linux use run-shadowmaps-clean.sh, not the diagnostics-heavy launcher.
The pass needs the selected-area trace/frame path and completed precipitation
map. Missing prerequisites leave native rain intact.

## Offline verification

    python3 check_shaders.py
    make test-weather
    make -B -j4
    make -B -j4 deploy
    make -C win -B -j4 CXX=x86_64-w64-mingw32-g++-posix CC=x86_64-w64-mingw32-gcc-posix

test-weather requires Mesa/EGL and GL development headers. It compiles the
actual src/weather_particles.inc against a small authoritative-state fixture,
links its shaders in a real software GL context, draws/readbacks pixels,
checks depth/alpha preservation, compares touched GL state, and exercises
weather/area/viewport/fog/void/wind/roof gates. It runs normally and with
a 3.3 version override.

tests/Dockerfile.weather provides a disposable Debian 11 compiler/test image
with a pinned base and compatible libc for portable Linux output. Frozen main
packages avoid unavailable security-archive candidates. This is not a
production service image.

## Required in-game acceptance (pending)

Record launcher, OS, game build, GPU/driver, actual MSAA samples, settings and
environment overrides. Compare preview off/on in the same scenes:

1. Rotate/pan/zoom: world-space streaks, no camera-attached sheet, coverage holes
   or abrupt cell seams.
2. Roofs, bridges, alpha-tested holes and solid static transparent blockers:
   no impacts below shelter; exposed upper surfaces still receive them.
3. Characters, foliage, water and framebuffer-sampling/volumetric materials:
   preserve depth and accepted A2C/native routing without flicker.
4. Clear/rain/snow, interiors, area changes and save/load: no inherited rain or
   stale map; existing surface wetness still fades correctly.
5. Torches, night, fog, UI and later engine passes remain correct.
6. Frame times at 2048/32768/262144, stationary/moving camera and many lights.
   GPU height/light sampling has a real cost.
7. Settings persistence/reset after client restart.

## Native rain comparison checkpoint (runtime verification pending)

Only the exact case-insensitive texture resource `fxpa_rain01` is eligible.
Both stock fx_rain and the provided Aurica override use it. The current live
GL texture must still match the material binding; unrelated textures and UI,
replay/owned/shadow passes are excluded. Depth testing and blending must be on,
and depth writes off. All nine draw entry points skip both native output and
private duplicates for the eligible rain draw, with no GL-state changes.

Windows PartEmitter::RenderEmitter (RVA 0x62900) calls
SharedMaterial::BindAllStandardTextures (RVA 0x82630), not Material's variant.
The shared binding loads its base CAurTexture from +0x50. These calls and the
field read were inspected in the installed 89.8193.37-17 nwmain.exe
(SHA256 3b7cb1252e0edb2ce22d7971f333aade027039ae30a45b4bc64732c3e6bec73a).
The Windows adapter requires a real trampoline and has no remove/call/reinstall
fallback. No guessed Linux offset or unsafe CAurTexture::BindInUnit hook was
introduced. Linux native emitter hiding is not runtime-validated.

Rain models using different textures remain native. A custom gameplay effect
reusing this exact rain texture in the same eligible rainy scene also qualifies;
distinct authored VFX textures remain unaffected.

Headless GL tests exercise matching/nonmatching resources, preview/hide toggles,
dirty maps, changed live textures, stale frame completion, interiors, depthless
UI, depth-writing geometry and suppression-state preservation. They cannot
prove the new engine hook executes correctly in NWN. Check the status and
compare both toggles in game, with camera motion, transitions and spell VFX.

## Limits / next checkpoints

- Collision includes the existing static opaque/card map only: not full 3D
  side-wall, creature, moving-placeable or native-water collision.
  Strong sideways wind/narrow obstacles need further validation.
- Splash particles match simulated drop impacts. Surface ripple normals still
  use an independent field; sharing those events is a later checkpoint.
- No per-frame unculled BSP replay was added.
- Lightning remains native. Shader bolts/flashes need a separately verified
  authoritative lightning-event adapter; no random timer replaces it.
- Native rain is available as the comparison toggle and automatic fallback.

## Integration audit — 2026-11-08

PR #4 is incorporated on the release integration branch. Private rain and native
rain hiding both default off. Linux shipping tests exercised real engine
clear → rain → snow → interior → clear transitions: the private rain pass became
ready, native rain remained enabled, and entering the interior cleared surface
accumulation immediately. The Windows DLL layer/torch path passed under Proton,
but separate rain/snow startup fixtures stayed in native clear weather; those
runs do not establish Windows precipitation or native-rain suppression parity.
Earlier user-confirmed preview results do not replace this combined-version
acceptance. High-budget, shelter/VFX, native Windows and crowded multiplayer
checks above remain open; see docs/RELEASE_AUDIT.md.

## Puddle reflection trace quality (2026-11-16)

**Puddle reflections → Reflection trace quality** selects Full (10 steps),
Reduced (5), or Minimal (3). `NWN_PUDDLE_REFLECTION_QUALITY=0/1/2` supplies the
environment default; `puddle_reflection_quality` persists the panel choice.
Full remains the default and Reset value. This changes the ray-walk budget,
not image resolution, puddle masks, refraction, material alpha or geometry.
Fewer steps may miss reflected details or lower hit confidence. A missing or
invalid sample-budget uniform keeps the original ten steps. Uniform updates
are cached per program and are refreshed when the live quality changes.

A real half/quarter reflection-buffer experiment was tested on Linux Balamb
with the RTX 3060 Ti / driver 620.70.06 at 1920×1006 and full-resolution volumetric
fog. Its 480×252 Quarter target was confirmed active in the game log. The
maintainer observed lower FPS at reduced reflection resolution, both with a
four-tap depth-aware resolve/full-trace fallback and with a cheaper one-tap
resolve that faded mismatches. That experiment was removed from active code;
smaller targets do not establish lower total frame cost. There is no additional
reflection target, pass, or upsampling cost in the trace-quality implementation.
The old `puddle_reflection_resolution` key is ignored, rather than silently
reinterpreted as trace quality.

The preceding wet-film optimization replaces repeated offset noise-height
samples with one height evaluation and a screen-derivative-to-world-gradient
conversion, outside the varying ground/puddle branch. The maintainer observed
about 4 FPS improvement in rainy Balamb with full-resolution volumetric fog.
This was a wet-film optimization, not a reflection-resolution improvement.

Validation: `tests/run-weather-surface-regression.sh` renders the injected
material branch, tests known gradients and degenerate footprints, and counts
the actual trace loop at 10/5/3 steps and at the default uniform value.
`tests/run-puddle-trace-quality-regression.sh` checks the real per-material
uniform upload and persistence registration. Balamb performance of the new
trace budgets remains to be measured. Native Windows validation is not claimed.

Runtime evidence for the rejected buffer experiment used `launch-linux.sh`
→ `run-dev.sh` and the development library, including the launcher's cost,
trace and depth-dump diagnostics. Native executable SHA256:
`6d19c39bc646af5ddbc333ff31797a4acd9019506bcb45ef1b93b1bd5925e700`;
the client shader build macro reports8193. Fog was Full resolution with32
samples; private rain used4352 drops,40m radius and0.250 splashes. Adaptive
VSync failed and the client retried full VSync. MSAA was not independently
confirmed at the material target; this is not an uncapped throughput result.
The final trace-budget shader fixture on NVIDIA measured representative Full
at0.522ms versus Minimal at0.498ms in an adjacent pair. This excludes game
geometry, the rest of the frame and all-map visual acceptance.

The **Enable rain puddles and reflection/refraction** checkbox under Puddle
reflections disables the entire standing-water branch, including its procedural
mask, absorption, reflection ray walk and framebuffer refraction. Rain particles,
splashes, wet film and snow remain independently available. The default is on;
`NWN_RAIN_PUDDLES=0` supplies an off environment default and `rain_puddles`
persists the live checkbox. No settings-version retune is needed for this new key.

## Wet-film quality and Linux measurement (2026-11-16)

**Wet surface film → Enable wet film** is ON/OFF. ON retains the full Detailed
look. The default and Reset value are ON. `NWN_WET_FILM=0/1` supplies the
environment default; `wet_film_enabled` persists the checkbox. Legacy Detailed
(`wet_film_quality=0`) migrates to ON; legacy Simple/Off migrate to OFF. An
explicit new boolean takes precedence regardless of file ordering. The legacy
environment quality value is accepted only when the new environment variable
is absent. Simple sheen was removed after the maintainer found no visible
advantage over Off. With wet film and puddles OFF, the complete rain-surface
shader branch is bypassed. Particles, splashes, precipitation authority, snow,
alpha and depth remain independent.

The Detailed ripple kernel ignores Gaussian contributions below exp(-29).
GPU comparisons against the unbounded kernel found maximum field differences
4.66e-10 on Mesa and2.87e-6 on NVIDIA; the latter includes compiler precision
differences. Tests exercise the actual field functions, material branches,
uniform uploads, wet/dry/interior eligibility, and disabled optical work.

Maintainer's same-view rainy Balamb comparison, puddles disabled: Detailed50FPS,
Simple56FPS, Off60FPS. Simple's visual difference was not apparent to the
maintainer. The live log confirms an8-sample native material target; the game
remains presentation capped. Disabling private rain/splashes had no observed
FPS effect. These observations do not establish uncapped throughput.

Further full-detail experiments were not incorporated: redundant-normal and
integer-power rewrites, shared ripple distance, compact tangent basis, static
ripple-seed caching, alternative integer seeds, conservative cell pruning,
visibility gates and shader group voting. They did not show reliable wins on
the RTX3060Ti/620.70.06; some were slower. Twelve interleaved compact-basis
pairs split6/6, with median baseline0.306ms versus variant0.3105ms. Matching
8×MSAA/alpha-coverage fixtures also showed no win for those candidates.
Depth-only draws were0.004ms with Detailed or Off in the fixture, so no
manual weather suppression was added to replay paths.

These negative results are not a proof that Detailed cannot be improved.
A real-frame GPU profile is needed before a larger Full-quality redesign;
the isolated material fixtures do not reproduce all Balamb geometry, lighting,
coverage and blend traffic. No new Full optimization is claimed from these
experiments, and no additional render target or pass was retained.


### Detailed wet-film compact impact candidate (2026-11-16)

Native RenderDoc frames now validate the actual rainy Balamb material path. The first candidate retains nine-cell puddle impacts but evaluates wet-film half-scale impacts over four cells with bounded Gaussian tails. Impact centres and motion are unchanged. The captured film-only candidate differed by no more than2/255 per colour channel. A representative ground draw showed about10–11% lower clock-normalized GPU time; this is not a whole-frame FPS result. GPU clock changes and replay stalls are explicitly excluded from claims of a60FPS result. See [profiling evidence and limitations](docs/WET_FILM_OPTIMIZATION.md).

Production shader/surface tests passed on Mesa and the RTX3060Ti, including four animation phases, shelter and alpha surfaces. Linux development and shipping builds are current. Normal gameplay performance and moving-camera visual validation remain pending.


### Shelter-map performance follow-up (2026-11-16)

The runtime conservative-bounds experiment was rejected after a52→48FPS gameplay regression and removed from active source. The normal launcher used an8192² precipitation shelter map, whereas earlier clean captures used4096²; component timing is therefore qualified by that difference. The captured engine noise texture is1024² BC4.

Reducing only the shelter map to2048² produced55FPS in the maintainer’s rainy Balamb view, with detailed wet filmON, puddlesOFF and full-resolution fog. The60FPS target remains unmet. The new live **Wet surface film → Rain/snow shelter map** control offers Original,4096,2048,1024, and shows the actual active size. It preserves detailed film calculations and the sun-map size; lower shelter precision can widen transitions near roofs. The shared map affects rain and snow shelter. Environment `NWN_WEATHER_SHELTER_SIZE` and saved key `weather_shelter_size` use those pixel sizes or0 for Original. Reset/default is Original; the maintainer approved a1024 trial next.

### Linux Balamb performance follow-up (2026-11-17)

The rainy-view investigation isolated a major cost outside precipitation itself: receiver-tag render-target attachment changes during native geometry drawing. Linux now batches that reserved attachment across one native scene pass while restoring per-draw draw buffers and removing the attachment before postprocessing; tag contents, actual MSAA coverage and engine-derived classification are preserved. Windows retains its existing lifetime. State/GPU regression checks passed and the maintainer reports correct visuals. With full-resolution fog and both caster options restored, the heavy spot improved from the40s to72FPS. With detailed wet filmON and puddlesOFF, the former55FPS view reports80FPS and the heavy rainy spot69FPS. These are reported native gameplay results, not a universal performance guarantee. See `docs/WET_FILM_OPTIMIZATION.md` for matched GPU timing and rejected shelter experiments.
