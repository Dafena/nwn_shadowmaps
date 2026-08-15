# NWN:EE cascaded shadows -- Linux test build

Real sun shadows for Neverwinter Nights: Enhanced Edition. Drop-in: it does not
modify the game, patch any file, or need a mod/hak. Remove one file and the game
is exactly as it was.

## Build it first (source package)

If you were sent the source rather than a prebuilt `.so`, that is deliberate: a
binary is tied to the glibc of the machine that built it, and building on your
own machine avoids that entirely.

You need `g++` (C++17), `make`, and OpenGL headers. On most systems that is:

```bash
# Debian / Ubuntu / Mint / Pop!_OS
sudo apt install build-essential libgl-dev

# Fedora
sudo dnf install gcc-c++ make mesa-libGL-devel

# Arch / Manjaro / CachyOS / Steam Deck (desktop mode)
sudo pacman -S base-devel
```

Then, in the folder you were sent:

```bash
make deploy
```

That produces **`libnwn_shadowmap_deploy.so`**. It takes under a minute. There
is nothing to configure and no dependency to fetch -- ImGui and subhook are
included in the package.

(`make` on its own builds the development version instead: more panel controls,
writes diagnostic files, and expects launcher scripts. You want `make deploy`.)

## Install

Copy **`libnwn_shadowmap_deploy.so`** and **`nwn-shadows.sh`** into the folder
that contains `nwmain-linux`:

    <NWN install>/bin/linux-x86/

    chmod +x nwn-shadows.sh

No configuration file is needed -- the library carries its own settings.

## Run

Put **`nwn-shadows.sh`** in the same folder and run it:

```bash
cd "<NWN install>/bin/linux-x86"
./nwn-shadows.sh
```

**From Steam** -- Properties -> Launch Options, keeping the quotes:

```
"/full/path/to/nwn-shadows.sh" %command%
```

### Use the script, not a bare LD_PRELOAD

`ld.so` splits `LD_PRELOAD` on **spaces** as well as colons, and the normal
install path contains one:

    .../steamapps/common/Neverwinter Nights/bin/linux-x86/
                         ^^^^^^^^^^^^^^^^^^

So `LD_PRELOAD=/that/path/libnwn_shadowmap_deploy.so ./nwmain-linux` loads
nothing at all, with no error and no shadows -- it just looks like the mod does
not work. The script preloads via a symlink in a space-free directory, which is
the only reliable fix, and also sets the working directory the game expects.

## Set this in the game, or shadows will be doubled

**Options -> Video -> Creature Shadow Detail = Best**, Environment Shadows on.

This is backwards from what you would expect, so it is worth stating plainly:
NWN's "Off" does not mean "no shadow" -- it falls back to drawing a dark blob
under every creature. This mod suppresses NWN's own *stencil* shadow pass, which
is what Fast and Best use, and it replaces it with a proper cascaded shadow.

- **Best** -- creatures AND placeables. Correct choice.
- Fast -- player only; everything else keeps the game's own shadow.
- Off -- blobs, which this cannot remove.

Nothing is wasted by choosing Best: the pass is suppressed either way, and the
setting only decides how much it covers.

## The panel

**Ctrl+Shift+F11** opens the settings panel. Settings save automatically to
`nwn_shadowmap_settings.ini` beside the game binary -- that is the only file
this writes.

Everything is under **Performance**, and the two that matter most for framerate:

- **Cascades** -- the big one. Each level sets how many distance slices the
  shadow uses and how many of those also redraw MOVING casters. The moving half
  is nearly all of the per-frame cost, so if the framerate is low, lower this
  first. **Medium** is 3 slices with 1 moving layer: near-shadow detail at a
  low cost, and the best starting point.
- **Static world map** -- memory, not speed. It is a single texture and the
  highest setting allocates about **1 GB of video memory**. On a card with 6 GB
  or less, keep this at Low or Medium.

**Restore defaults** puts everything back.

## Requirements

- The **native Linux** build of NWN:EE (`nwmain-linux`). Not Proton.
- **glibc 2.29 or newer** for the prebuilt library (Ubuntu 20.04, Debian 10,
  Steam Deck and anything newer are fine). Check with `ldd --version`. If yours
  is older, build from source instead -- see above.
- OpenGL 3.3. Any GPU that runs NWN:EE acceptably is fine.

## If something is wrong

Run from a terminal and read the output -- it reports what it resolved and, if
it draws nothing, why:

```bash
cd "<NWN install>/bin/linux-x86"
LD_PRELOAD="$PWD/libnwn_shadowmap_deploy.so" ./nwmain-linux 2>&1 | tee /tmp/nwn-shadow.log
```

Send `/tmp/nwn-shadow.log`. The first lines list which engine symbols were found;
that alone identifies most problems, including a game patch having moved them.

To turn the mod off without deleting anything:

```bash
NWN_SHADOWMAP_OFF=1 LD_PRELOAD=... ./nwmain-linux
```

## What to report

- Framerate with and without the library, in the same spot (the difference is
  the only number that means anything -- absolute fps depends on the area).
- Shadows that look wrong: doubled, missing on some objects, flickering, or
  detached from what casts them.
- Anything that appears at a distance boundary. Shadows are drawn in slices and
  the seams between them are the place defects show first.

Please include your GPU, driver version and distribution.
