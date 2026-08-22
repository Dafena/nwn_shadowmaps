# Building

Both targets come from the same source tree. No dependencies to fetch — Dear
ImGui and subhook are vendored in `imgui/` and `subhook/`.

## Linux

```bash
make            # libnwn_shadowmap.so        -- development, every control visible
make deploy     # libnwn_shadowmap_deploy.so -- what you hand to a tester
make portable   # same as deploy, built in debian:11-slim via docker
```

Needs g++ with C++17. `make portable` needs docker.

**Use `make portable` for anything you send to another machine.** A binary's
glibc floor comes from the build host: built on a rolling distro the `.so` can
demand a very recent glibc and silently fail to load nearly everywhere. The
docker build lands on GLIBC_2.29 / GLIBCXX_3.4.21.

## Windows

```bash
cd win && make  # win/version.dll -- always a shipping build
```

Cross-compiled with mingw-w64. **The POSIX-thread variant is required** — the
Win32-thread compiler cannot satisfy this project's `clock_gettime` use.

```bash
x86_64-w64-mingw32-g++ -v 2>&1 | grep 'Thread model'   # must say: posix
```

## Checking the shaders without launching the game

```bash
python3 check_shaders.py
```

The receiver shader is assembled from C++ string literals and is only compiled
inside a live GL context, so a syntax error in it is invisible until the game
runs — and it is the pass that draws every shadow. This extracts each shader
exactly as the C++ concatenates it and runs `glslangValidator` (package
`glslang`) over it. Run it after ANY edit to those strings.

Transparency shader injection in `nwn_oit.cpp` is runtime-built GLSL too. Run
the same checker after changing those strings, then perform a native Linux game
run because material parameters, MSAA coverage, and engine bucket state cannot
be validated offline. See [TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md).

## Installing

**Linux:** put `libnwn_shadowmap_deploy.so` next to `nwmain-linux` and
`LD_PRELOAD` it, or use `nwn-shadows.sh`.

**Windows:** put `version.dll` in `bin/win32/` beside `nwmain.exe`. Under
Proton/Wine also set `WINEDLLOVERRIDES="version=n,b"`.

Logging is off by default in shipping builds. Enable with
`NWN_SHADOWMAP_LOG=1`; on Windows the log is written beside the executable as
`nwn_shadowmap_win.log`.

The development trace launcher truncates `shadowmap-phase1.log` at every
startup. Copy or filter evidence into a separate file before the next run.

### Windows: Smart App Control

A locally built, unsigned DLL is blocked by Smart App Control with a "Bad Image"
dialog and error `0xc0e90002` (Code Integrity events 3033/3077). This is a
signing requirement, not a build fault — rebuilding cannot fix it. Either sign
the DLL with a trusted certificate, or disable Smart App Control, which cannot
be re-enabled without reinstalling Windows. SAC is on by default on new
Windows 11 installs, so expect this with testers.
