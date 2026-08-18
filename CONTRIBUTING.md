# Contributing

## The platform rule is absolute

Linux and Windows share one source tree, and that sharing is deliberately
one-directional:

> When something works on Linux and does not on Windows, the change is made
> **for Windows only**. If the code is shared, an alternate path is created for
> Windows. This is not a preference.

This exists because it was learned the hard way. A shared receiver-shader
expression was "fixed" for a Windows symptom and broke Linux, which had been
confirmed working the same day. `nwn_platform.h` carries the two macros that
express the split:

- `NWN_SHIP` — *is this a build handed to someone else?* Windows is always one;
  Linux becomes one via `make deploy`. It hides controls that remove shadows,
  drops diagnostics, and carries its own defaults.
- `NWN_WIN_LOCAL_FASTPATH` — platform **mechanics**, not shipping policy. Keep
  the two distinct; conflating them is how Windows-only behaviour leaks into
  Linux.

## Building

See `BUILD.md`. In short: `make` for the development build (panel, diagnostics,
launcher scripts), `make deploy` for the shipping build, and `win/` cross-builds
the Windows DLL with mingw-w64.

`nwn_shadowmap.cpp` is split into same-translation-unit `.inc` modules. Both
Makefiles list them as dependencies — if you add a new `.inc`, make sure it is
covered, or an `.inc`-only edit silently reuses a stale object.

Runtime-built GLSL can be validated without launching the game:

```bash
python3 check_shaders.py
```

## Verify in-engine

This is a renderer injector. Written-correct is not the bar — the graphics
driver, the engine's GL state and the game's own passes all get a vote. A change
is not done until it has been seen running. Several past regressions looked
perfect in review and were caught only by a screenshot.

Prefer diagnostics that produce evidence over reasoning about what "should"
happen. The panel's Performance and Diagnostics sections, the `[cost]` line, and
the receiver debug views exist because guessing has a poor track record here.

## Reporting

GPU, driver version and OS are not boilerplate for this project — a
documented class of bug reproduces only on specific drivers, and antialiasing
settings materially change the code paths involved. Please include them.
