# Windows deployment

The Windows target is `version.dll`, a proxy DLL built from the same renderer
source as the Linux injector. It is always a shipping build.

## Platform rule

Linux is the default development target until the Linux injector is working.
Windows changes are made only when explicitly requested by the maintainer.

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

## Logging and settings

The shipping build is quiet by default. Set `NWN_SHADOWMAP_LOG=1` before
launching to write `nwn_shadowmap_win.log` beside `nwmain.exe`. The settings
panel can save `nwn_shadowmap_settings.ini` beside the executable. Diagnostic
PGM dumps and frame-cost instrumentation remain off unless explicitly enabled.

The panel is intentionally smaller than the development panel. Controls that
can remove or bypass the effect are not exposed or persisted in a shipping
build; their compiled defaults define shipping behaviour.

## Known Windows-specific status

- Area sun/moon shadow policy is recovered by observing the exported `Aur*`
  decision path. `ShadowOpacity` is not available through that Windows path, so
  Windows applies the policy without that opacity field.
- The Windows local-light fast path has separate caster culling, current-
  program tracking, and relaxed generation publication rules. Linux does not
  use those mechanics.
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
