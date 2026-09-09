# Windows deployment

The Windows target is `version.dll`, a proxy DLL built from the same renderer
source as the Linux injector. It is always a shipping build.

## Platform rule

Linux remains the behavioural reference. On 2026-08-24 the maintainer
explicitly requested staged Windows implementation work. Windows-specific
repairs must remain isolated and must not redefine working Linux behaviour.

Material Mode 2 A2C is accepted and validated on both Linux and Windows. Native
Windows enables strict material routing by default so the proxy DLL needs no
launcher flags; set `NWN_ALPHA_MODE_ROUTING=0` only for an explicit native-path
A/B test. Mode 3 OIT is parked globally and is not a Windows target. See
`../TRANSPARENCY_MODES.md` for the evidence and
`../WINDOWS_IMPLEMENTATION_PLAN.md` for the completed Windows checkpoints.

The automatic clear/rain/snow weather system is also enabled by default on
Windows. Its rendering implementation is shared with Linux; Windows-specific
code only discovers client weather state and interposes OpenGL calls.

If a behaviour works on Linux and fails on Windows, repair it in a Windows-only
path. Do not change shared C++ or GLSL to solve a Windows-only symptom. The
platform mechanics live in `nwn_platform.h` and the Windows local-light
mechanics are isolated behind `NWN_WIN_LOCAL_FASTPATH`.

## Build

From the repository root:

```bash
cd win
make
```

This requires a POSIX-thread mingw-w64 toolchain:

```bash
x86_64-w64-mingw32-g++ -v 2>&1 | grep 'Thread model'
```

The output must contain `posix`. The result is `win/version.dll`; the build
links libgcc/libstdc++ statically and uses the system OpenGL, GDI, user32, and
version libraries.

Verify the proxy exports and the mapped engine symbols against the exact game
executable before deployment:

```bash
make verify NWN_WIN_EXE="/path/to/Neverwinter Nights/bin/win32/nwmain.exe"
```

`check_artifact.sh` fails if the DLL is not x86-64 PE, any of the 17 proxy
exports is absent, or any mapped MSVC symbol is absent from that executable.

## Install

Copy `version.dll` beside `nwmain.exe`, normally into:

```text
<NWN install>\bin\win32\
```

Launch NWN normally. To remove the injector, delete the copied DLL. Under
Proton/Wine, set:

```text
WINEDLLOVERRIDES=version=n,b
```

The DLL carries shipping defaults, so no batch launcher is required. Environment
variables still override those defaults.

Strict material routing is one of those Windows shipping defaults. Materials
declaring `parameter int NWN_ALPHA_MODE 2` use A2C whenever the live framebuffer
is multisampled; unmarked materials and every excluded route remain native. Set
`NWN_ALPHA_MODE_ROUTING=0` to disable routing for troubleshooting.
The validated four-stage material hook pipeline also defaults to complete;
`NWN_WIN_MATERIAL_HOOK_STAGE` is retained only for diagnostic downgrades.

## Logging and settings

The shipping build is quiet by default. Set `NWN_SHADOWMAP_LOG=1` before
launching to write `nwn_shadowmap_win.log` beside `nwmain.exe`. The settings
panel can save `nwn_shadowmap_settings.ini` beside the executable. Diagnostic
PGM dumps and frame-cost instrumentation remain off unless explicitly enabled.

The panel is intentionally smaller than the development panel. Controls that
can remove or bypass the effect are not exposed or persisted in a shipping
build; their compiled defaults define shipping behaviour.

## Known Windows-specific status

- Private client `StartWeather`/`StopWeather` calls and the active client-area
  lookup are resolved from the v89.8193.37-17 network decoder. At scene start,
  the injector reads the loaded client area's stored weather so a weather
  change made while indoors is applied immediately upon entering an exterior.
  This is a client-side path and works for local and multiplayer games; the
  injector does not depend on local server memory.
- Rain optics, snow accumulation/deformation, precipitation occlusion,
  static-transparent blockers, lighting, fog, interior reset, and area weather
  transitions have been confirmed in native Windows. The exact crowded
  multiplayer case for all 15 NPC trail slots remains a final regression item.
- Area sun/moon shadow policy is recovered by observing the exported `Aur*`
  decision path. `ShadowOpacity` is not available through that Windows path, so
  Windows applies the policy without that opacity field.
- The Windows local-light fast path has separate caster culling, current-
  program tracking, and relaxed generation publication rules. Linux does not
  use those mechanics.
- `Bright surfaces keep light` is a start threshold, not an intensity: lower
  values protect a wider brightness range from the later directional-shadow
  composite. Windows defaults it to `0` so ordinary torch-lit surfaces retain
  their local lighting; Linux retains its separately validated `0.85` default.
- `Local shadow update` is a persisted three-tier performance control: Low
  rebuilds no more often than every 25 ms, Medium every 16 ms, and Ultra on
  every rendered frame to match the directional-shadow cadence. Low remains
  the default; the local shadow-source budget is still independently capped at
  three lights.
- Material identity transport maps the exact v89.8193.37-17 exports for
  `Material`/`SharedMaterial` creation, binding, field parsing, texture access,
  and shared initialization. These hooks support default-on strict material
  routing and fail closed if a safe trampoline is unavailable. Although both
  destructor exports exist, their MSVC Subhook trampolines crash on return and
  are permanently refused;
  construction/init boundaries reset reused identities instead. The unsafe
  texture-unit hook also remains disabled.
- The v89.8193.37-17 `Material` layout stores its live `SharedMaterial*` at
  offset `+0xD0`, independently confirmed by the exported custom-shader bind
  routine. The read-only transition census used that association to distinguish
  one texture's native Mode 0, authored Mode 2, and parked Mode 3 materials.
- Visible Mode 2 A2C routing was confirmed on Windows with a four-sample live
  framebuffer. The same run left Mode 0 and parked Mode 3 native and bound both
  directional CSM and local-light shadow receivers to the Mode 2 shader.
- The Windows `lightpriority` field offset is currently unverified, so priority
  sorting is disabled. `GetShadowLights()` remains the authority for source
  eligibility and order.
- Windows and Proton can have different GL dispatch overhead. Compare matched
  scenes and settings before attributing a performance difference to the
  injector.

## Troubleshooting

If the DLL does not load:

1. confirm it is beside the exact `nwmain.exe` being launched;
2. remove stale copies from other NWN directories;
3. enable `NWN_SHADOWMAP_LOG=1` and inspect the first symbol-resolution lines;
4. check Windows Smart App Control or other code-integrity policy if Windows
   reports a Bad Image or `0xc0e90002` for an unsigned local DLL.

For renderer bugs, report the game build, native/Proton mode, GPU/driver,
MSAA/video settings, log, and the exact artifact path used by the game.
