# PS4 cascade-shadow renderer reference

> **What this is.** Notes taken from the console build's shipped shader and
> technique files, used as an architectural reference for how BioWare/Beamdog
> actually implemented cascaded shadows in this engine. No platform code is
> reproduced or executed here, and the dump itself is not part of this
> repository. Sections below the "Linux injector mapping" heading describe what
> this project built; everything above them is reference material.

This document records what can be recovered from the local NWN:EE PS4 package
without copying platform code or modifying the game. It is an implementation
specification for the Linux `LD_PRELOAD` injector, not a claim that the PS4
binary can run on Linux.

Source package:

```text
<PS4-reference-dump>/PS4/CUSA15938/CUSA15670/
```

## Evidence inventory

Only three readable renderer technique files contain directional-shadow
implementation data:

| File | What it establishes |
| --- | --- |
| `ovr/GNM.technique` | PS4's active pass ordering, target sizes, caster filters, update cadence, and raster state. |
| `ovr/OpenGL33.technique` | GLSL-style shader contracts, array sampling, static/dynamic combination, alpha cutoff, and geometry-shader fan-out. |
| `ovr/D3D.technique` | Cross-check of the four-layer D32 target, depth comparison direction, receiver bias, and layered geometry pass. |

No other readable package asset references cascade matrices, splits, or fitting
parameters. `eboot.bin` contains useful renderer names (`CascadeShadowSetup`,
`UpdateUniforms`, `cascadeShadowsStaticFramebuff`, `clearcascades`) but is not
a readable ELF with recoverable function bodies in this dump. The exact CPU
matrix-fitting routine therefore is **not present in recoverable source**.

### Switch cross-check (2026-08-09)

The supplied Switch `romfs` package at
`/home/<user>/.local/share/eden/dump/010013700DA4A000/romfs/` contains the same
three renderer technique sources and the same `D3D.technique.debug_blob` as the
PS4 package. After normalizing line endings, `OpenGL33.technique` and
`D3D.technique` are byte-identical to the PS4 versions. `GNM.technique` differs
only in unrelated post-process/UI/highlight sections (notably SSAO is enabled
and highlight passes are reduced); its cascade-shadow targets, pass ordering,
caster filters, raster state, and shader source are unchanged.

That is strong evidence that this is a shared NWN:EE renderer design rather
than a PS4-only implementation.

The later supplied Switch `exefs` at `/home/<user>/cursor/nwnee executable/`
adds executable-level corroboration. Its `main` is a Nintendo NSO; decoding its
compressed text/rodata/data segments for read-only string/RTTI inspection found
`graphics.techniques.default.features.cascade-shadows`,
`#define CASCADE_SHADOWS_ENABLED %d`, `LightManager::ShadowMapUpdate`,
`UpdateUniforms(Scene*, Camera*)`, and
`UpdatePointLightUniforms(... LightManager::ShadowMapUpdate ...)`. This proves
the shared runtime has a CPU-side cascade-shadow update and camera/scene uniform
path, rather than merely shipping dormant shader source.

The NSO is stripped, so it still does **not** recover exact split distances,
fit/stabilisation constants, or private object layouts. Further broad static
analysis has diminishing returns; the Linux renderer's verified entry-frame
trace is the direct source for those values and is now used for the isolated
cascade-math implementation.

## Recovered production design

### Targets

The production PS4/GNM path owns two depth texture arrays:

| Property | Static | Dynamic |
| --- | --- | --- |
| Target | `GL_TEXTURE_2D_ARRAY` | `GL_TEXTURE_2D_ARRAY` |
| Layers | 4 | 4 |
| Resolution | 2048 x 2048 | 2048 x 2048 |
| Depth format | `GL_DEPTH_COMPONENT` (D3D cross-check: D32 float) | same |
| Comparison | `GL_COMPARE_REF_TO_TEXTURE`, `GL_LESS` | same |
| Filtering | linear | linear |
| Wrap | clamp to edge | clamp to edge |

The OpenGL33 fallback declares 1024 x 1024, while the PS4/GNM and D3D paths
declare 2048 x 2048. The Linux injector's default 2048 target is therefore the
right PS4-class baseline.

Each array is attached as a depth-only framebuffer. There is no color output.
The engine writes all four layers in one geometry-shader draw using `gl_Layer`;
the equivalent D3D stage writes `SV_RenderTargetArrayIndex`.

### Caster partition and cadence

The passes are exact and deliberate:

| Map | Update rate | Casters |
| --- | --- | --- |
| Static | once per 10 scene draws on PS4/GNM | `StaticShadows`, non-skinned opaque; then `StaticShadows`, non-skinned transparent |
| Dynamic | every scene draw | `Dynamic`, non-skinned opaque; `Dynamic`, non-skinned transparent; `Dynamic`, skinned opaque; `Dynamic`, skinned transparent |

Every cascade caster excludes `Water`. The static pass admits `Fade` and
`Caps` in addition to `StaticShadows`; both dynamic and static passes explicitly
distinguish transparent and skinned variants. Static and dynamic maps are
cleared independently; static content is not regenerated during the dynamic
pass.

This is why a single all-caster map is not PS4-equivalent: it throws away the
cache boundary and cannot match the source's update cost or composition.

### Caster shader contract

All variants first produce a **world-space** position:

- rigid: model transform of the incoming position;
- skinned: skin transform, then model transform.

The geometry stage emits each input triangle once for every cascade layer. It
applies the selected static or dynamic light VP matrix and adds a small positive
clip-space depth offset (`0.001`) before emitting the triangle to that layer.

Transparent casters are alpha-tested, not blended into the depth map:

- material alpha must be at least `0.95`;
- diffuse texture alpha must be at least `0.5`.

This is the PS4 answer for foliage: cards cast cutout silhouettes, while their
normal color pass can retain its ordinary transparency behavior.

### Raster state

The PS4/GNM cascade depth passes use:

```text
depth test/write: enabled, LESS
color writes: disabled
culling: back
polygon offset: units 4.0, factor 2.0
depth clear: 1.0
```

The portable GL33 technique has the same structure, with polygon-offset factor
3.0 and static draw period 6. The PS4/GNM numbers are the preferred Linux
starting reference. The existing injector target creation now matches the
confirmed sampler details (`GL_LESS`, linear, clamp-to-edge); it does not yet
redirect any live draw.

## Receiver contract

The normal lighting path receives:

```text
shadowsVPStatic[4]
shadowsVPDynamic[4]
shadowsClipfar[4]
cascadeShadowMapsStatic
cascadeShadowMapsDynamic
```

For each shaded world-space point it:

1. selects the first cascade whose positive view depth is below its
   `shadowsClipfar` threshold;
2. transforms the world position with that cascade's static and dynamic VP;
3. maps XY/Z from clip space to texture/comparison coordinates;
4. comparison-samples the matching array layer;
5. uses the minimum of static and dynamic visibility;
6. multiplies the global directional-light attenuation by that visibility.

The D3D implementation makes the comparison bias explicit: it tests receiver
depth minus `0.001`. The GL/GNM path instead adds `0.001` to caster clip-space
Z. They encode the same intent: move the caster depth conservatively away from
self-shadowing.

The default OpenGL33 shader has shadow smoothing disabled. When enabled, it
uses a rotated 20-point Vogel pattern, but **PCF is not required to reproduce
the default PS4 pipeline**. Hardware linear comparison is the correct first
Linux receiver test. The optional smoothing radius starts at `0.0025` texture
coordinates and drops by `0.0006` per farther cascade.

## What this settles, and what it does not

### Settled by direct source evidence

- four cascades, not one;
- separate static and dynamic maps;
- 2048² PS4 target resolution;
- depth arrays plus hardware comparison sampling;
- `LESS`, linear filtering, clamp-to-edge;
- one layered geometry-shader fan-out (or four equivalent per-layer draws);
- static cadence of ten draws on PS4;
- explicit opaque/transparent/skinned caster families;
- alpha-cutout foliage caster rules;
- exact receiver composition: `min(static, dynamic)` multiplied into the
  global directional light;
- receiver cascade selection in **current camera view depth**, not world
  distance or a screen-space heuristic.

### Not recoverable from this package alone

- actual `shadowsClipfar[4]` values;
- the frustum fitting / stabilization algorithm that produces each VP matrix;
- exact static invalidation conditions beyond the ten-draw cadence;
- PS4 engine object flags that map content to `StaticShadows`, `Dynamic`,
  `Transparent`, and `Skinned`.

Those are generated by the CPU side (`CascadeShadowSetup` / `UpdateUniforms`),
whose code is inside the opaque PS4 executable. The technique sources tell us
the exact *matrix contract*, but not the fitting constants. Claiming otherwise
would be guessing.

## Linux injector mapping -- WHAT WAS ACTUALLY BUILT

Updated 2026-08-15. The columns below are outcomes, not a work order: every row
here shipped and is confirmed in game on Linux and Windows.

| PS4 responsibility | What this project does |
| --- | --- |
| Select gameplay scene/camera | `g_areaScene` plus area-scoped camera VP capture, taken in the same render phase as the receiver's scene depth. The phase identity was the fix for the motion bug below. |
| Build four stable cascade VPs | **Done.** Four cascade layers with texel snapping, fitted per frame from the area camera frustum, with a movement threshold and generation counter so a layer is only refitted when it must be. |
| Static caster collection | Uses the engine's **visible** draw buckets, NOT `Scene::RenderStaticShadows()`. The PS4 warning held: the stencil pass submits invisible `render 0 shadow 1` proxies and omits visible `render 1 shadow 0` pieces. |
| Dynamic caster collection | Same route, same reason. Static/dynamic separation is exposed as "Fixed casters" / "Moving casters". |
| Array population | Four-layer static and dynamic depth arrays, populated by replaying each bucket into the layers from inside `Scene::RenderDrawBucket`. A static layer holding a valid generation is re-stamped rather than re-rendered -- that cache is what makes it affordable. |
| Transparent casters | Alpha-cutout casters work: foliage casts foliage rather than a solid quad. Bucket 3 (dithered cards) is a separate toggle because screen-space dithering reads as near-solid depth from a light's view. |
| Receiver | **Deliberate divergence.** The PS4 design multiplies shadow inside the material shaders; this stayed a fullscreen composite pass, because the injector does not own the engine's shaders and cannot add a uniform to them. It reconstructs world position from scene depth and the area camera VP inverse. |

### Where this project went beyond the reference

The PS4 package describes DIRECTIONAL cascades only. Local point-light shadows
here are not from this reference: each shadowed light gets one downward face at
170 degrees, filled from inside the engine's own bucket pass at the same point
the cascades are. See `AGENTS.md`.

## Outcome of the motion bug

**Fixed.** The PS4 source settled the question: cascade selection and receiver
reconstruction must use the SAME current camera view as the normal material
pass, and reusing an arbitrary last-seen matrix or a camera-independent area box
is not supported. The Linux symptom -- stable when still, drifting during pan or
zoom -- was exactly that phase mismatch, and capturing scene depth and the area
VP in one render phase resolved it.

The staged work order that used to sit here is complete, with one item
deliberately abandoned: moving receiver multiplication into the engine's own
directional-light shader paths. The fullscreen composite is the shipped design
and the red/green diagnostic is retained behind a debug mode rather than retired,
because it remains the fastest way to prove the pass reaches the screen at all.
