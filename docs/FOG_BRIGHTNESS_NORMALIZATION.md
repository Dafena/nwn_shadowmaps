# Daytime volumetric fog normalization — 2026-11-20

Maintainer reported daytime volumetric fog blowing out the image. Local-shadow
performance work is paused while correcting that visual baseline.

The scatter pass individually bounded sun shafts and lamp glow, then added
ambient fog and finally added that sum to scene RGB. Separate bounds do not
bound the total, even when each source is individually plausible. The final
pass operates only on the default framebuffer (entry FBO zero is an existing
hard gate); NWN's authored fog is already present in that scene.

The correction applies a single exponential shoulder to combined ambient,
area-faded shaft and local-light radiance. It then fills remaining destination
channel headroom using ONE_MINUS_DST_COLOR, retaining SRC_ALPHA for scene
transmittance. Thus output is S*(1-D)+D*T, bounded in [0,1] for the normalized
framebuffer, rather than hard-clipping S+D*T. This is an intentional LDR
composition rule, not a new physical HDR model. Authored fog is not applied a
second time. Framebuffer alpha and all saved/restored GL blend state retain
the existing ownership. No extra pass, texture, readback or scene copy.

No user settings or defaults were changed. Bright-scene fog and lamp glow will
be less additive; this is the requested visual correction. Night appearance
still needs a visual check, as does the exact user-adjusted low-angle view.

## Verification and limits

- `python3 tests/fog_energy_regression.py` extracts the real scatter expression
  and actual blend choice. Old source fails with output >1; new source passes
  10,000 mixed-source exposures and the zero-medium identity. This is a numeric
  composition regression, not a GPU frame-time test.
- `python3 check_shaders.py`: pass.
- Linux diagnostic, normal and shipping builds: pass. Windows runtime untested.
- Actual diagnostic and normal Linux runtime: NVIDIA RTX3060Ti / driver620.70.06,
  NWN89.8193.37-17, GL3.3, 1920x1006, full-resolution fog, VSync OFF. Normal
  loaded root libnwn_shadowmap.so, with no profiling flags. Launchers and copies
  under `/tmp/nwn-headless-fog`, isolated user directory and saves.
- Private KWin virtual output + Xwayland :2 uses NVIDIA hardware, not software.
  SDL offscreen driver was unavailable. XTest pointer motion did not reach the
  virtual seat; direct X events successfully load saves. This does not provide
  a verified method for restoring the user's adjusted camera pitch.
- Default test2 camera daylight fog ON/OFF captures show improved stone contrast
  and retained haze. Fixed world crop mean RGB old ON .4600 / OFF .3063; new
  ON .3849 / OFF .3066. These images have animated NPC/water and are not pixel-
  identical captures. They do not reproduce the exact strongest low-angle case.
- Normal runtime compiled all fog programs and reported full 1920x1006 target.
  No performance gain is claimed from the visual change.
- Temporary live fog-isolation controls were removed from source after testing.
  Original launchers, authored resources and original saves/settings untouched.

Pending: low-angle/day-night appearance acceptance, then resume local-shadow
ablation using the corrected fog baseline. Earlier successful local ablation was
at the wrong camera; adjusted-view runs aborted and yield no accepted timings.

## Lamp arrival and carried torch follow-up

Maintainer requested glow 0.2, gradual fog influence as lights appear, and no
startup whiteout when equipping test2's torch. Previous fog selection uploaded
current nearest-eight lights at full contribution with no per-light transition.

- Lamp glow compiled/reset default is now 0.2; existing environment override,
  slider, settings table and bound remain. Settings v20 retunes only this key
  from v19; runtime verified exactly one skipped/retuned value, saved as 0.2.
- `shadow_fog_lamp_fade.h` owns eight fog-only slots keyed by the engine light
  identity. New entries start at zero and need another observation before
  ramping; fade-in takes 0.5s, retirement 0.35s. Retiring slots are not replaced
  while bright. Moving sources use current engine positions, avoiding position
  lag. No changes to light authority, native lighting or shadow capture.
- Reset on scene/area change, disabled fog/glow, nonfinite time or >1s gap;
  hitch catch-up capped at 50ms. No persistent cross-area world-light cache.
- Slot weight multiplies lamp radiance before the combined shoulder, so arrival
  cannot inject one frame of full-brightness fog before stabilizing.
- `tests/fog_lamp_fade_regression.cpp` checks arrival, fade-out, replacement,
  current moving position, area reset and pause reset. Numeric fog regression,
  shader compile checks and normal Linux build pass.

Actual normal-build runtime: private NVIDIA display, same test2/daylight/full-
resolution settings as above. Inventory radial Equip action visibly moved the
Torch to equipped off-hand, with a lit torch visible on the character. Recorded
100 snapshots over ~11s including equip: world crop mean RGB baseline .34737,
peak .36061, sampled bright-channel fraction zero. Rise progressed through
.34889, .35297, .36044, rather than a sampled screen-wide flash. This is sampled
visual evidence, not every GPU frame or an exhaustive camera-entry/exit test.
Night/other-camera acceptance is still not claimed. Original saves untouched.

## Follow Lighting Max Lights

Fog now uses the existing live `lamp_upload_max()` authority (Lighting Max Lights,
with the existing diagnostic MAX_LAMPS override) instead of an independent cap
of eight. CPU targets/history and GLSL arrays/loop support the engine's maximum
128 ordinary lights. Shadow-casting source selection and its three-light limit
are unchanged. Existing eligibility/ray-sphere rejection, glow0.2, radiance
normalization, and temporal fades remain. Excess sources retire smoothly after
reducing the game limit; their fading contribution can briefly remain during
that transition. No additional shadow maps or rendering passes.

Regression covers 32 simultaneous identities, reducing to four, and full 128-
slot replacement. Shader compilation/energy regression and both Linux builds
pass. Normal Linux RTX3060Ti runtime with the private test2 copy reported
Lighting Max Lights=128, selected22 eligible fog candidates; actual shader
compiled/linked and full-resolution scene rendered correctly. This proves the
old eight-source restriction is gone. Not a matched performance benchmark;
more contributing lamps can cost additional fog calculations. Windows runtime
and visual acceptance at every game lighting limit remain untested.
