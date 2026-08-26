# PS4 cascade reference

This file is a reference extracted from the PS4 renderer assets. It is not
runtime truth for NWN:EE and it is not a work order for this injector. Current
Linux/Windows behaviour is documented in [SHADOWMAP_STATUS.md](SHADOWMAP_STATUS.md)
and the source.

## Reference design

The PS4 renderer uses two four-layer depth arrays:

| Property | Static | Dynamic |
| --- | --- | --- |
| Target | depth texture array | depth texture array |
| Layers | 4 | 4 |
| Reference resolution | 2048² | 2048² |
| Comparison | `LESS` | `LESS` |
| Filtering | linear comparison | linear comparison |
| Wrap | clamp-to-edge | clamp-to-edge |

Static and dynamic maps are populated separately. Static geometry is refreshed
less often; dynamic opaque, transparent, and skinned families are refreshed per
scene. Transparent casters are alpha-tested rather than blended into the depth
map. The receiver selects a cascade from positive camera-view depth, compares
the matching static and dynamic layers, and combines their visibility into the
directional-light term.

The reference raster state is depth test/write enabled, `LESS`, colour writes
disabled, back-face culling, polygon offset, and a depth clear of `1.0`. The
exact platform values are useful starting points, not evidence that NWN exposes
the same pass or object flags.

## What it is useful for here

The reference supports these general design constraints:

- separate static and dynamic depth ownership;
- layered cascade targets rather than a single monolithic map;
- world-space caster transforms followed by light-space projection;
- alpha-cutout caster handling;
- receiver cascade selection in camera depth;
- comparison sampling with conservative self-shadow protection.

The injector implements analogous concepts through NWN's visible bucket path,
its own matrix-stack replay, and a fullscreen receiver. It does not claim binary
or shader-level equivalence to the PS4 renderer.

## Not recoverable from the reference

The PS4 assets do not establish NWN's cascade fit constants, invalidation rules,
private object layouts, area-light policy, local-light selection, or the
correct Windows/Linux platform split. Those must come from the current engine
bindings and runtime evidence. Do not copy an old phase claim from this file
into the implementation status.
