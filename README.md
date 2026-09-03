# NWN:EE shadow-map injector

This repository contains an experimental renderer injector for Neverwinter
Nights: Enhanced Edition. It intercepts the game's OpenGL renderer and builds
injector-owned shadow targets from NWN's own rendering decisions. It does not
modify `nwmain`, `.mtr` files, `.shd` files, modules, hak content, or game
assets on disk. It does intercept and extend selected shader sources at runtime.

The Linux target is `libnwn_shadowmap.so`, loaded with `LD_PRELOAD`. The
Windows target is `win/version.dll`, a proxy DLL loaded beside `nwmain.exe`.
Both targets are built from the same renderer source.

## Current state

As of 2026-09-03:

- Directional sun shadows use cascaded, injector-owned depth arrays and a
  fullscreen composite. Static world geometry can use a world-anchored map;
  moving and alpha geometry use the normal visible draw buckets.
- Local-light shadows use one downward perspective depth layer per selected
  light. The layers are filled immediately after NWN renders each visible
  bucket, using the engine's matrix stack and the engine's selected shadow-light
  list. This is a contact-shadow path, not a cube-map implementation.
- The local-light receiver supports falloff, cone edge fading, PCF, normal
  offset, minimum separation, slope-scaled fill bias, and lamp lift.
- NWN's area shadow policy is observed and applied to the directional composite.
  Local-light shadows remain independent of the area's sun/moon policy.
- The Dear ImGui settings panel is live. Development builds expose diagnostics;
  shipping builds keep only user-facing controls.
- Linux and Windows automatically observe NWN clear/rain/snow area weather
  through one shared weather-effects renderer. Rain applies gradual wetness,
  compact fog-aware reflective/refractive puddles, and normal-based impacts.
  Snow precipitation
  is mutually exclusive with rain, while their accumulated surface layers
  crossfade with matching rain/snow fade-out timing. Snow uses slope-aware
  high-frequency coverage, raised parallax depth, and persistent refilling
  trail segments carved by player/creature
  movement. A 512x512 RGBA32F world-space deformation texture combines up to
  16 nearby characters with independent 64-segment CPU histories; each history
  recycles its own oldest segment through a short retirement fade while the GPU
  cost stays texture-based. The camera-target player owns a reserved history
  and retirement pool that NPC churn cannot evict. Deformation stores contact
  height as well as depth, so movement below a roof cannot alter snow above it.
  Added snow is a neutral material relit by NWN's area lighting and complete
  enabled local-light census, allowing torches and other colored lights to tint
  it without copying the covered tile's albedo. Coverage uses the real triangle
  plane rather than rough material normals, and a snow illumination floor keeps
  it visible over dark, non-snow terrain textures.
  Near full accumulation, a continuous minimum blanket prevents bright lamps
  from exposing procedural albedo gaps and making the layer appear lifted.
  Native water is excluded. A separate cached top-down depth map blocks every
  rain and snow surface contribution beneath opaque and static-transparent
  scenery while preserving alpha-discard holes. Its 16-sample world-anchored
  edge filter avoids hard cuts, visible bands, and camera shimmer. It reuses the
  static-world extent/resolution and one-shot full-BSP lifecycle; 8192x8192 was
  runtime-tested, while the supported 16K maximum requires about 1 GiB for its
  depth texture. Linux obtains weather directly from `CNWCArea::SetWeather`.
  Windows resolves the corresponding private client weather calls and, on
  every area load, synchronizes from the newly active client area's stored
  weather. The area-load check closes interior-to-exterior timing gaps and is
  client-side, so it applies to local games and multiplayer clients alike.
- The repository has a Windows build. Windows-specific local-light fast paths
  are isolated behind `NWN_WIN_LOCAL_FASTPATH`; shared Linux behaviour must not
  be changed to solve a Windows-only problem.
- The renderer contains explicit material transparency modes. Mode 2 A2C is
  the preferred foliage baseline because it remains stable under camera
  rotation and receives directional/local shadows; multiple covered layers
  still limit emitter fidelity. Mode 3 weighted OIT is parked: its runtime and
  census switches are ignored and authored Mode 3 materials render natively.
  The proven Linux prototype remains in-tree only as dormant research and is
  not part of the Windows plan.
- A native MTR census proved that ordinary transparency, framebuffer sampling,
  and volumetric rendering use materially different routes. Future alpha modes
  are planned around an explicit `NWN_ALPHA_MODE` material parameter, but the
  initial custom-shader transport changed foliage rendering and was rejected;
  the accepted selector now follows the stock material path. Framebuffer and
  volumetric flags retain their native meanings.

This remains a prototype rather than a complete replacement for NWN's shadow
system. The most important open visual issue is flicker when NWN's
“Hide second story tiles = Auto” setting changes the visible draw list while a
player walks under an overhang. The agreed next design is a cross-fade between
two injector-owned static layer generations, driven by the actual static fill.
It must not inject hidden geometry back into NWN's live draw stream.

Other known limitations include the contact-shaped local-light projection,
camera-frustum-limited caster submission, and an unverified Windows
`lightpriority` field offset. The Windows priority sort is therefore disabled
until that field is identified.

The active transparency design, native bucket evidence, rejected global OIT
path, and regression matrix are in
[TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md).

## Repository map

`nwn_shadowmap.cpp` is the root translation unit. It includes the renderer
modules below so they can share private state without exporting an internal
ABI:

| File | Responsibility |
| --- | --- |
| `shadow_config.{h,cpp}` | Memoized environment lookup and shipping defaults |
| `shadow_engine_bindings.inc` | NWN symbol declarations and resolution |
| `shadow_gl_api.inc` | OpenGL ABI declarations and entry points |
| `shadow_targets.inc` | Texture/FBO allocation and validation |
| `shadow_replay.inc` | Sun, static-world, and local bucket replay |
| `shadow_local_lights.inc` | Engine-selected local-light state and capture setup |
| `weather_runtime.inc` | Shared rain/snow rendering, platform weather authority, precipitation occlusion, and snow trails |
| `shadow_shader_interposition.inc` | Shader interception and draw wrappers |
| `shadow_fullscreen_receiver.inc` | Receiver shader construction and scene copies |
| `shadow_overlay_runtime.inc` | Overlay runtime, input, and frame ordering |
| `shadow_diagnostics_settings.inc` | Diagnostics and persisted settings |
| `shadow_trace_cascade.inc` | Scene tracing, cascade setup, and hooks |
| `shadow_math.{h,cpp}` | Pure matrix, vector, and projection helpers |
| `nwn_overlay.{h,cpp}` | Overlay contract and Dear ImGui implementation |
| `nwn_platform.{h,cpp}` | Linux/Windows platform mechanics |
| `nwn_oit.cpp` | Opt-in transparency census and experimental A2C/OIT mechanics |

The source is the authority for supported diagnostics and exact environment
keys. Use `rg -n 'shadow_getenv|NWN_SHADOWMAP_' nwn_shadowmap.cpp shadow_*.inc`
before documenting a new switch.

## Build

```bash
make              # development Linux library
make deploy       # shipping Linux library
make portable     # shipping Linux library built in Debian 11
cd win && make    # shipping Windows proxy DLL
```

The project vendors Dear ImGui and subhook, so no dependency download is
required. See [BUILD.md](BUILD.md) for compiler requirements and deployment.
Run the shader extraction check after changing receiver shader strings:

```bash
python3 check_shaders.py
```

## Development launchers

Run these from the repository directory:

```bash
./run-dev.sh                         # diagnostics-heavy trace/development path
./run-shadowmaps-clean.sh            # full-effects visual/performance baseline
./run-shadowmap-trace.sh             # non-rendering trace path
./run-nwn.sh /path/to/nwmain-linux   # generic LD_PRELOAD launcher
```

`run-dev.sh` enables the diagnostics-heavy development capture and delegates
game-directory discovery to `run-shadowmap-trace.sh`. Set
`NWN_SHADOWMAP_GAME_DIR` when the game is not found by the launcher. Diagnostic
logs and PGM output go to the repository by default; override the locations
with `NWN_SHADOWMAP_LOG` and `NWN_SHADOWMAP_OUT_DIR`.

Use `run-shadowmaps-clean.sh` for normal visual and performance comparisons,
including weather, A2C, shadows, and the ImGui panel. It keeps the current
shipping/deploy library and normal renderer settings but disables all
census/readback/dump/timing diagnostics. Using the shipping build is important:
the development library defaults GPU cost reporting and capture dumps on. The
base scene trace remains enabled
with a one-frame/one-event output cap because its hooks supply the camera,
scene, and light state required by the shadow renderer. It ignores saved panel
settings by default so repeated A/B runs are comparable. Accepted Mode 2 A2C
routing is enabled by default on Linux and Windows; set
`NWN_ALPHA_MODE_ROUTING=0` only for a native-rendering A/B comparison.
`NWN_OIT_MODE3` is intentionally unavailable.

For a normal full-effects run, press `Ctrl+Shift+F11` in a loaded area to open
the panel. Settings are saved as `nwn_shadowmap_settings.ini` in
`NWN_SHADOWMAP_OUT_DIR` when that variable is set; otherwise they use the
process working directory. The development launcher sets the output directory
to the repository, while a plain shipping launch writes beside the game. Delete
that file only when you intentionally want to discard saved tuning.

## Linux deployment

For a tester build, prefer `make portable`, then copy
`libnwn_shadowmap_deploy.so` and `nwn-shadows.sh` beside `nwmain-linux`. The
launcher avoids `LD_PRELOAD` path splitting when the game installation path
contains spaces. Full steps are in [LINUX_DEPLOY.md](LINUX_DEPLOY.md).

## Windows deployment

```bash
cd win && make
```

Copy `win/version.dll` beside `nwmain.exe` in `bin/win32`. Under Proton/Wine,
use `WINEDLLOVERRIDES="version=n,b"` when required. The Windows build carries
shipping defaults and writes no log unless `NWN_SHADOWMAP_LOG=1` is set. See
[win/README.md](win/README.md).

## Rules that must survive future changes

1. Until Linux is working, Linux is the priority target for every feature.
   Windows work requires an explicit request.
2. If NWN exposes a decision, consume it. Missing engine data means a safe
   no-op, not an injector heuristic. `GetShadowLights()` owns local-shadow
   source selection; the `SetLightGL` census is lift-only.
3. A Windows-only fix gets a Windows-only path. Do not alter shared C++ or GLSL
   to repair a Windows symptom when Linux is already working.
4. Replays must be guarded against re-entry and must restore framebuffer,
   viewport, program, texture, depth, blend, cull, scissor, masks, polygon
   offset, and matrix-stack state.
5. The receiver runs before the post-scene local-light staging work. Do not
   reorder those passes without proving scene-depth capture and generation
   lifetime again.
6. Never publish a local-light generation until its required layers are
   complete. Preserve the last coherent generation when a replacement is
   incomplete.
7. Keep new settings in the ImGui panel, the settings table, and the defaults
   reset together. Use `kSettingsMax`; never reintroduce a hard-coded table
   capacity.

## Documentation map

- [CURRENT_TASK.md](CURRENT_TASK.md) — authoritative active checkpoint
- [WINDOWS_IMPLEMENTATION_PLAN.md](WINDOWS_IMPLEMENTATION_PLAN.md) — staged native Windows parity plan
- [TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md) — transparency evidence, mode contract, and roadmap
- [SHADOWMAP_STATUS.md](SHADOWMAP_STATUS.md) — current implementation status
- [AGENTS.md](AGENTS.md) — stable engineering rules for future work
- [LINUX_RENDERER_MAP.md](LINUX_RENDERER_MAP.md) — current Linux engine map
- [SYMBOL_INDEX.md](SYMBOL_INDEX.md) — source navigation map
- [REFACTORING.md](REFACTORING.md) — current module layout and refactor notes
- [PS4_CASCADE_REFERENCE.md](PS4_CASCADE_REFERENCE.md) — external reference only
