# Windows build (version.dll proxy)

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


Cross-built from Linux with mingw-w64. Same engine logic as the `.so` — the
shared `nwn_shadowmap.cpp` compiles for both, with `nwn_platform.h` shimming
`dlsym`/`mprotect`/`sysconf` and four `#ifdef`'d sites.

## Build

```bash
cd win && make
```

Produces `version.dll` (PE32+). Verified: exports **all 17** `version.dll`
entry points, and depends only on system DLLs (statically linked
libstdc++/libgcc, so the target machine needs no mingw runtime).

### Why all 17, not just the game's three

The first build exported only the three functions `nwmain.exe` itself imports
and failed at launch with:

> entry point `GetFileVersionInfoW` not found in `nvoglv64.dll`

A proxy replaces `version.dll` for the **whole process**, not just for the
game. NVIDIA's OpenGL driver imports it too, and once our module is loaded
under the name `version.dll` the loader hands it to everyone who asks — so any
export we omit is a hard load failure for them. The forwarders use one generic
six-argument passthrough, which is ABI-correct here because every
`version.dll` entry point takes only integer/pointer arguments and returns an
integer; extra arguments are harmless under the single x64 calling convention.
They are defined under internal names and mapped to the real ones by
`version.def`, since `windows.h` already declares the real signatures.

## Install

Copy **`version.dll`** into the game's `bin\win32\` folder, next to
`nwmain.exe`. That is all — launch the game however you normally do.

**The `.bat` is optional now (changed 2026-08-10).** It used to be mandatory,
and that was a bad design for a drop-in DLL: a run where none of the variables
reached the process produced a 71-line log with no phase banners at all and a
completely inert renderer -- indistinguishable from "the mod is broken".

The DLL now carries a read-only table of defaults (`kWinDefaultEnv`), consulted
by `shadow_getenv()` only when the real environment variable is absent. Every
`NWN_SHADOWMAP_*` read in `nwn_shadowmap.cpp` goes through that accessor, so the
implication rules downstream (light capture pulling in the four trace flags,
receivers pulling in multi-layer capture) behave exactly as under the Linux
launcher. The environment always wins, so the `.bat` and the Linux launchers are
unaffected.

**Do NOT seed these with `_putenv_s()` from the constructor.** That was the
first attempt and it was worse than the problem: the log stopped immediately
after the seeding line -- before the IAT hooks, before symbol resolution --
so mutating the CRT environment that early is not survivable in this process.
The read-only table touches no global state and does no work until something
asks for a setting.

There is no injector to run: nwmain.exe imports `VERSION.dll`, so Windows
loads ours from the exe's own directory at process start, before GL init.

To uninstall, delete `version.dll`.

## Where the log goes

**Nothing is written by default.** Windows is the shipping build, so the log is
OPT-IN: set `NWN_SHADOWMAP_LOG=1` and it appears as `nwn_shadowmap_win.log`
beside `nwmain.exe`, with the same `[shadowmap]` lines as the Linux log,
starting with the symbol-resolution report -- still the first thing to read when
something misbehaves.

The check uses `GetEnvironmentVariableA`, not `getenv`: it runs from an early
constructor where the CRT environment is not reliably usable (seeding it with
`_putenv_s` there once killed startup outright -- see the bugs list).

Everything else diagnostic is off there too: `.pgm` dumps
(`NWN_SHADOWMAP_DUMP_PGM=1`) and the light census / local-light dumps
(`NWN_SHADOWMAP_LOCAL_LIGHT_TRACE=1`). The only file the shipping build writes
is `nwn_shadowmap_settings.ini`, which is the panel remembering its settings.

## Area shadow flags: SOLVED 2026-08-14, confirmed in game 2026-08-15

Previously the one feature Windows could not have. `nwmain.exe` exports nothing of
`CNWCArea`, and `UpdateShadowingLights` is not virtual, so it can be reached
neither by name nor through RTTI. The injector now hooks the three exported
functions that call ends in -- `AurEnableShadowing`, `AurDisableShadowing`,
`AurSetDynamicProjectionLight` -- and takes the engine's own on/off decision from
them. See `AGENTS.md` for why that sequence is unambiguous.

Not recovered here: `ShadowOpacity`, so the policy applies at full strength.

## What is genuinely different from Linux

| Concern | Linux | Windows |
| --- | --- | --- |
| Load mechanism | `LD_PRELOAD` | `version.dll` proxy (3 forwarded exports) |
| Symbol resolution | walk unstripped ELF `.symtab` | `GetProcAddress` on the exe; nwmain.exe is stripped but **exports 18,245 named symbols**, including the static data ones (`?m_aurMtxStack@@3PAVaurMatrixStack@@A` etc.) |
| Modern GL interception | patch `__glew*` pointers | **nwmain.exe exports no `__glew*` at all** — instead `wglGetProcAddress` is IAT-hooked and hands the engine our wrappers as it resolves them |
| `glDrawElements`/`glDrawArrays` | ELF symbol interposition | IAT hook on `OPENGL32.dll` |
| Fault guard around bucket replay | `sigaction` + `sigsetjmp` | none (runs unguarded; a fault will be honest rather than silently disabled) |
| `SDL_PollEvent` | a jump thunk — patch its pointer | REFUSED (no trampoline; see bug 5) |
| Overlay input capture | swallow events in the `SDL_PollEvent` hook | **subclass the game's HWND** and drop mouse/keyboard messages while the panel wants them |

The Itanium→MSVC name map lives in `nwn_win_symbols.h`; all 33 engine symbols
matched unambiguously against the export table of the shipped `bin/win32/nwmain.exe`.

## RESOLVED 2026-08-11: Windows now matches Linux performance

For a long stretch the Windows build was several times slower than the Linux one
on identical scenes -- native Windows and Proton alike, reproduced on a second
machine. **It was not the port, the hooks, the symbols, the GPU or any rendering
pass.** It was `shadow_getenv()` being called from `trace_matrix_upload()`, which
runs on every `glUniformMatrix4fv` the engine issues:

| | Linux | Windows |
| --- | --- | --- |
| a `shadow_getenv` MISS | one `getenv()` | `getenv()` **+ 27 `strcmp`** over `kWinDefaultEnv` |

`kWinDefaultEnv` is the table that makes the `.bat` optional (see Install). It
only runs on a miss, which is why it looked free -- but `UNIFORM_TRACE` is never
set, so every matrix upload took the miss path, thousands of times a frame. The
Windows-only cost came from the Windows-only table.

**`shadow_getenv` is now memoised** (pointer-compare first, `strcmp` fallback
aliased in). Fixing the individual call site was tried first and was the wrong
shape: 154 sites exist, 32 reachable per frame or per draw, so the idle case got
better and the frame rate collapsed again the moment Cascades and light casting
were switched back on. Caching is safe because nothing ever writes the
environment -- `_putenv_s` was tried and did not survive startup.

Also removed from that path: a `glGetIntegerv(GL_CURRENT_PROGRAM)` per matrix
upload, replaced by tracking `glUseProgram` (which is already hooked).

**RULE: nothing reachable per draw or per uniform upload may call
`shadow_getenv`, `getenv`, or any `glGet*`.** This is sharper on Windows, where
GL 1.1 entry points dispatch through `opengl32.dll`'s per-thread context lookup
instead of landing in the driver, but it is a bad idea on both.

## Status: WORKING, rebuilt 2026-08-10 (freeze lifted)

The maintainer lifted the earlier freeze and asked for a fresh DLL. This build
adds everything the Linux side gained since: automatic fog occlusion (reads
NWN's own `fogParams` uniform -- see `SHADOWMAP_STATUS.md`) and the single
Off..Ultra "Cascades" control replacing the two sliders. The fog setting is no
longer in the panel because it needs no tuning.

### The Windows panel is Performance-only (2026-08-11)

Windows is a SHIPPING build; the development Linux build keeps every option.
The guard is **`#if !NWN_SHIP`** (nwn_platform.h), not `_WIN32` -- since
2026-08-12 `make deploy` produces a shipping Linux `.so` with exactly the same
trim, so this is no longer a platform question and `_WIN32` is back to meaning
platform mechanics only.

`#if !NWN_SHIP` in
`nwn_overlay_imgui.cpp` guards out the **Sun shadows SECTION** (strength, bias,
blend, PCF), the whole **Local light** section, **Diagnostics**, the
**frame-cost** block and **Refit after**. What remains is the FPS line, the
hotkey hint, **Performance**, **Restore defaults**, and every `(?)` tooltip.

Three A/B switches were added to Performance during the 2026-08-11 performance
hunt -- **Sun shadows**, **Moving casters**, **Fixed casters**. With all three
off the injector draws nothing, which is how it was finally established that the
cost was not in any render pass.

**They are DEVELOPMENT-BUILD ONLY** (`#if !NWN_SHIP`, both the widgets and their settings
keys). Every one of them REMOVES shadows: they are a debugging instrument, not a
quality setting, and a shipping build has no business offering a user a way to
switch the effect off by degrees. They shipped visible on Windows in one build
by mistake and were pulled before distribution.

Both halves matter. Hiding the widget while still PERSISTING the key would
recreate the `local_enabled` failure exactly -- a saved `receiver_enabled=0`
would disable the whole effect on Windows with no control able to undo it. Guard
the widget and the `settings_table()` entry together, always.

Verify after a build; the panel strings are in the binary:

```bash
for s in "Moving casters" "Fixed casters" "Sun shadows"; do
  echo "$s: $(strings win/version.dll | grep -c "^$s$")"   # must be 0
done
```

Everything hidden ships at its default, so two of those defaults had to change
for Windows or the visible controls would govern nothing:

- `g_localLightReceiver` defaults **on** there, and `kWinDefaultEnv` enables
  `LOCAL_LIGHT_TRACE/CAPTURE/RECEIVER` -- otherwise "Light casted shadow" would
  be a resolution control over a disabled feature.
- `MAX_LAMPS=8` and `LOCAL_LIGHT_SIZE=256` (Low), i.e. ~2 MB of shadow depth and
  eight per-light captures a frame.

"Lights for lift" is renamed **"Light supported"** on BOTH platforms and moved to
the top of Performance: it sets how many lights lift the sun shadow AND how many
cast their own, so it is the single light budget rather than two settings that
happened to share a number.

### Light budget: lift count vs caster count (2026-08-11)

"Light supported" moves TWO numbers, and only one of them is expensive:

| Light supported | lift (per-pixel test) | **casters** (a depth capture each) |
| --- | --- | --- |
| 8 / 16 / 32 | 8 / 16 / 32 | **2** |
| 64 | 64 | **3** |
| 128 | 128 | **4** |

Four casters is the ceiling at any setting. That cap is what made an
emitter-heavy area playable: every light in the caster set costs a full depth
capture per frame, and such areas produce dozens of lights.

**"Lights casting shadows"** (Performance, on by default) decides whether EMITTER
lights compete for those slots. Excluding emitters wholesale was tried first and
was wrong -- the player's carried torch IS an emitter, and it is the shadow
players notice. With the cap in place they are safe to include.

### A hidden control must not be persisted

`local_enabled` is NOT saved on Windows. The panel hides the Local light
section there, so a value in `nwn_shadowmap_settings.ini` can only ever fight
the shipped default -- and it did: an .ini carrying `local_enabled=0` (the Linux
default, written by an earlier build) silently disabled light-cast shadows on
Windows, with no visible control able to undo it. The log said
`receiver gate: recv=0 haveVP=0`, while the capture was demonstrably writing
good maps.

Rule: if a setting is not reachable from the platform's panel, do not persist it
on that platform -- the compiled default and `kWinDefaultEnv` own it.

### NWN's own shadows are suppressed, silently and always

Windows ships with `g_hideEngineShadows` **on and with no control for it**:
`Scene::RenderShadows` is never called, so the engine's stencil shadow never
draws under this module's. It is not a preference there -- without it every
object carries two shadows -- so there is nothing to expose. Linux keeps the
checkbox, because that is where the suppression gets debugged.

Hidden therefore means NOT PERSISTED on Windows (the rule below): a saved
`hide_engine_shadows=0` would put NWN's shadows back under everything with no
control able to undo it.

**Tell users to set Creature Shadow Detail to BEST in the game's video
options.** This is genuinely counter-intuitive and it will be asked about:

| `Creature Shadow Detail` | engine draws | suppressed here |
| --- | --- | --- |
| Off | blob fallback, drawn by another path | **no** |
| Fast | stencil, player only | only the player's |
| **Best** | stencil, creatures AND placeables | **yes -> nothing** |

"Off" is the one setting that guarantees blobs. Best is not wasted work despite
the result being discarded: the setting decides which objects the pass COVERS,
and a narrower setting simply leaves more engine shadows on screen.

### Shipping build: what is OFF (2026-08-11, before distribution)

Everything diagnostic is compiled off for Windows. Linux keeps it all -- that is
the development build.

| | Windows | Linux | override |
| --- | --- | --- | --- |
| log file | off | stderr | `NWN_SHADOWMAP_LOG=1` |
| `[cost]`/`[buckets]` instrumentation | **off** | on | `NWN_SHADOWMAP_COST=1/0` |
| `.pgm` depth dumps | off | on | `NWN_SHADOWMAP_DUMP_PGM=1/0` |

`NWN_SHADOWMAP_COST` matters beyond noise: reporting a truthful GPU time needs a
`glFinish`, which stalls the pipeline on every frame it prints, and it also
carried a per-draw counter. A user's install must not pay for the maintainer's
measurements. The counters and the `glFinish` are now gated on the same flag, so
switching it off removes the work, not just the printing.

Two periodic reports are deliberately LEFT ON: the receiver's "why did I not
draw" gate, which only fires when the receiver produced nothing, and the
symbol-resolution report. Those are what makes a "no shadows" report from
someone else diagnosable, and both are silent unless something is wrong (and
write nowhere unless `NWN_SHADOWMAP_LOG=1`).

The two `*_TRACE` entries in `kWinDefaultEnv` are NOT diagnostics despite their
names -- `NWN_SHADOWMAP_TRACE` selects the accepted render path and
`LOCAL_LIGHT_TRACE` supplies the light census the capture depends on. Removing
either removes the shadows. Their text output is bounded by `TRACE_FRAMES` (90).

### Diagnostic artefacts

`.pgm` capture dumps default **off on Windows** and **on for Linux**
(`NWN_SHADOWMAP_DUMP_PGM=1/0` overrides). Linux is the development build where
they are the cheapest evidence available; Windows is the shipping build and has
no business writing files next to someone's `nwmain.exe`.

The local-light dumps used to key off `NWN_SHADOWMAP_LOCAL_LIGHT_TRACE` alone,
which was fine while that flag was off everywhere -- but enabling light-cast
shadows on Windows REQUIRES it (the capture is gated on it), so the shipping
build started writing `.pgm` files again. They now require `g_dumpCapturePgm`
too, which also skips a full depth-array readback per capture.

### Resolved: the native-Windows failure

That section described a receiver that drew nothing on native Windows while
Proton was fine. Root cause was NOT the port: `glCopyTexSubImage2D` into a depth
texture is invalid on a MULTISAMPLED framebuffer, so the driver raised
GL_INVALID_OPERATION and the draw was gated off -- NVIDIA's Linux driver
tolerated the same call. Fixed by `glBlitFramebuffer`; antialiasing can stay on.
See `SHADOWMAP_STATUS.md`.

