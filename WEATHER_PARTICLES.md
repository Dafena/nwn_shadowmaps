# Private shader rain — validation checkpoint

This is an **opt-in preview**, not a fully accepted replacement for NWN weather.
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

Open the renderer ImGui panel, expand **Shader rain (preview)** and enable
**Private rain and splashes**. Status reports why the pass is waiting.
**Hide native rain (preview only)** defaults on. With both boxes checked,
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
| NWN_SHADER_RAIN | 0 | 0 / 1 |
| NWN_SHADER_RAIN_HIDE_NATIVE | 1 | 0 / 1; only active with preview |
| NWN_SHADER_RAIN_BUDGET | 2048 | 256–262144 |
| NWN_SHADER_RAIN_RADIUS | 28 | 8–64 world metres |
| NWN_SHADER_RAIN_WIND_X | 0 | -8–8 metres/second |
| NWN_SHADER_RAIN_WIND_Y | 0 | -8–8 metres/second |
| NWN_SHADER_RAIN_SPLASHES | 1 | 0–2 |

Wind is manual, **not a decoded NWN wind vector**. Density remains
NWN-authoritative. Budget is rounded down to a square grid, and density
selects a stable subset of its cells.
The logarithmic slider and Exact drop budget input allow extreme storms without
making low-budget tuning awkward. Try 32768 first and increase while checking
frame times. Above 32768 the panel displays a GPU-cost warning. The ordinary
default remains 2048; the cap is not a promise of acceptable FPS at maximum.

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
actual weather_particles.inc against a small authoritative-state fixture,
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
