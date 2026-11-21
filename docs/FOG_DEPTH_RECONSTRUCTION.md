# Reduced-resolution fog detail loss — 2026-11-20

Maintainer supplied Quarter/Half/Full rainy test2 images: distant railings and
thin/transparent details were obscured disproportionately at Quarter.

The old low-resolution depth pass chose the farthest sampled depth, deliberately
favoring background fog. Upsampling assigned nonzero weights to every neighbor
and normalized them even if all four belonged to the background. Thus a thin
foreground feature absent from the low-resolution map received background fog.
Blur also mixed depths with an 8%-distance tolerance, and Full unnecessarily
went through neighboring-pixel reconstruction. Its noise filter remains useful.

Correction:

- Blur and upsample reject incompatible depths using the same 2%/5cm threshold.
- Composite discards pixels without a compatible low-resolution sample. A
  selective full-resolution draw uses the same scatter program, source uniforms,
  lighting/shadow textures, and exact full-resolution scene depth to fill only
  those pixels. Pixels already composited are discarded before the costly march.
  This adds one draw at Quarter/Half and extra ray marching on rejected pixels;
  it is not a free performance change.
- Full retains depth-restricted noise filtering, then directly reads its own volume texel.
- Additional scene-depth binding uses unit27, saved/restored by GodrayGlState;
  unit29/30 array-shadow bindings remain available to the selective draw. Alpha
  ownership and the existing bounded LDR blend are unchanged.

`tests/fog_depth_reconstruction_regression.py` extracts both actual GLSL depth
predicates and verifies the unmatched-foreground fallback contract. It fails on
old source and passes after the correction. Shader compilation, fog energy
regression, Linux development/shipping builds and diff checks pass.

Normal NVIDIA RTX3060Ti/620.70.06 runtime, NWN89.8193.37-17, private test2 default
camera, VSync OFF, fog strength1.5/density.32, glow.2, LightingMax128/22 candidates:
Quarter480x252, Half960x503 and Full1920x1006 shader compile/link/render checks passed.
Final Full gate retains the same depth-restricted blur already checked at Quarter/Half. Captures
and logs are under /tmp/nwn-headless-fog/depth-*. Exact supplied rainy low-angle
view and matched performance acceptance are pending. Original saves/settings
were not changed. Automated rapid menu loading failed; staged save loading
worked. Do not count those failed menu runs as renderer failures.

This operates on NWN's resolved scene depth; it does not introduce per-MSAA-
sample or multiple transparent-layer depth. Therefore it is not evidence that
all remaining transparency/coverage artifacts have been solved.

## Authored fog opacity boundary (2026-11-21)

Maintainer then showed geometry silhouettes appearing within the region that
native fog renders fully opaque. This is separate from the low-resolution
foreground mismatch. The shared scatter program previously integrated until
min(scene radial distance, marchFar), with marchFar135 in test2 despite native
fog end45. Hidden surface distance could therefore change fog/transmittance.

`nwnFogMarchEnd` caps integration at authored fog end divided by view-depth gain
per radial metre. Both low-resolution and selective exact-depth paths use the
same function; lamp chords also use that capped march length. Areas without
valid authored fog retain the optional added atmosphere. Native fog and its
already-opaque surface colour remain in the base scene; no duplicate authored
extinction is applied. This removes hidden surface-distance dependence, not
all possible light/shadow variation within the visible fog volume.

`tests/fog_opaque_distance_regression.py` extracts and evaluates the real shader
function for frontal/oblique rays, foreground, hidden geometry, and no-authored-
fog cases. It fails before the change and passes afterwards. Exact screenshot
camera acceptance is not implied by that numeric test.

Cutoff build normal Linux runtime smoke check passed at Quarter480x252 with
native fog30..45, same NVIDIA/game/settings baseline above. Shader compiled and
scene rendered; capture opaque-quarter.png and log opaque-runtime.log are in
/tmp/nwn-headless-fog. The logged march135 is the general uniform ceiling, not
the new per-ray authored45/viewPerMetre cutoff. This default camera does not
establish acceptance of the supplied distant-building view.

Maintainer subsequently reported the current result "looks quite good" and
requested a checkpoint and Windows DLL (2026-11-21). This accepts the observed
Linux fog result; it does not establish Windows runtime or all-scene parity.
