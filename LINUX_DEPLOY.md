# Linux deployment

This document is for the tester-facing Linux build. The injector is a native
`LD_PRELOAD` library for NWN:EE's `nwmain-linux`; it is not a Proton/Wine
deployment guide.

## Build

For a distributable library, build on Debian 11 with the portable target:

```bash
make portable
```

If Docker is unavailable, `make deploy` builds the shipping library on the
current machine. A library built on a newer rolling distribution may require a
newer glibc than the tester has; the portable target is preferred for sharing.

The project vendors Dear ImGui and subhook. No dependency download is needed.

## Install

Copy these two files into the directory containing `nwmain-linux`:

```text
libnwn_shadowmap_deploy.so
nwn-shadows.sh
```

Usually that is:

```text
<NWN install>/bin/linux-x86/
```

Make the launcher executable:

```bash
chmod +x nwn-shadows.sh
```

## Run

```bash
cd "<NWN install>/bin/linux-x86"
./nwn-shadows.sh
```

For Steam launch options, keep the quotes:

```text
"/full/path/to/nwn-shadows.sh" %command%
```

Use the launcher rather than hand-writing `LD_PRELOAD`: common Steam paths
contain spaces, and the launcher uses a space-free symlink when necessary. It
also starts the game from the directory NWN expects.

## In-game setting

Set **Options → Video → Creature Shadow Detail** to **Best** and leave
Environment Shadows enabled. NWN's **Off** setting selects a blob fallback; it
does not mean “draw no shadows”. The injector suppresses the native stencil
shadow path in its configured shipping mode.

## Panel and files

The shipping panel opens with `Ctrl+Shift+F11` in a loaded area. Settings are
saved in `nwn_shadowmap_settings.ini` beside the game executable. Shipping
builds do not emit PGM dumps or frame-cost instrumentation by default.

The shipping defaults are compiled into the library. Environment variables can
still override them for a controlled A/B test. The most useful first checks are
`NWN_SHADOWMAP_LOG=1` and `NWN_SHADOWMAP_OFF=1`.

The material-selectable A2C/OIT work described in
[TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md) is experimental development
work, not a supported shipping feature. Do not enable its diagnostic
environment switches in tester packages unless the test explicitly targets
that work. Mode 3 OIT activation is currently hard-disabled; authored Mode 3
materials render through NWN's native path.

## Requirements

- native Linux NWN:EE (`nwmain-linux`);
- glibc compatible with the supplied library (use `make portable` to minimize
  the required version);
- OpenGL 3.3-capable driver.

## Reporting a problem

Run from a terminal and save the log:

```bash
NWN_SHADOWMAP_LOG=1 ./nwn-shadows.sh 2>&1 | tee /tmp/nwn-shadow.log
```

Report the same scene with and without the injector. Include distribution,
GPU, driver, game build, MSAA/video settings, whether the issue is doubled,
missing, detached, or flickering, and any environment overrides.
