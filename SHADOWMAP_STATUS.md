# Implementation status

Updated 2026-08-21. This file describes the current tree only. It is not a
phase history and it does not describe deleted launchers or experiments.

## Development priority

Until the injector is working on Linux, Linux is the priority target for every
feature and fix. Windows work requires an explicit request from the maintainer.
Keep Windows compiling when practical, but do not let unrequested Windows
runtime differences steer shared implementation decisions.

## Renderer pipeline

The injector observes a selected gameplay `Scene::Render` and captures one
immutable camera context for that render. It uses NWN's renderer state and
visible draw buckets to populate private depth targets, then composites the
result after the scene is complete.

### Directional sun

- Cascaded orthographic light-space matrices are fitted from the captured area
  camera and sun direction, with texel snapping and cache state.
- Static and dynamic cascade arrays are separate. Static near layers can be
  refreshed less often; moving layers are refreshed from the current visible
  buckets.
- The world-anchored static map is an injector-owned single texture for static
  area geometry. It is refreshed when its area, anchor, extent, or sun context
  changes.
- Static alpha-discard geometry and dynamic alpha/card geometry have dedicated
  capture controls.
- The receiver copies scene depth, reconstructs world position, selects the
  appropriate cascade, compares the static/dynamic maps, and applies optional
  overlap and 3x3 PCF before the composite.

### Local lights

- `LightManager::GetShadowLights()` is the only source-selection authority.
  Sources are consumed in NWN's order and capped by the native three-source
  list plus the configured budget.
- Each active source owns one downward perspective depth layer. The path is a
  contact cone, not a cube map and not six faces.
- Capture setup computes the source matrices after the scene. The visible
  `Scene::RenderDrawBucket` hook then replays each original bucket immediately
  after NWN's draw, using the engine matrix stack and a local re-entry guard.
- A spare layer allows a dropped source to fade out. Receiver metadata and
  generation state must remain coherent with the depth contents.
- The local receiver applies NWN attenuation with a configurable falloff,
  cone/far-edge fading, PCF, normal bias, minimum separation, slope-scaled fill
  offset, and optional lamp lift.
- The receiver runs before post-scene local capture. The one-frame staging
  lifetime is intentional and must not be changed casually.

### Area policy

The injector observes NWN's active-area shadow state. Directional CSM follows
the area's sun/moon enable state and shadow opacity, with a composite-only
day/night fade. Local-light shadows are independent of that directional policy.
The Windows path observes the engine's exported `Aur*` decisions because the
area class method is not exported there; Linux also cross-checks the observed
decision against its readable area fields.

### Native shadow pass

The injector can suppress NWN's stencil shadow pass in the development path and
does so in shipping defaults so the scene does not receive two shadow systems.
The game's **Creature Shadow Detail** should remain **Best**; NWN's Off setting
uses a blob fallback rather than meaning “no shadows”.

## Experimental transparency path

The local branch contains an opt-in Linux A2C foliage experiment in
`nwn_oit.cpp`. It is not part of the accepted shipping shadow path. Runtime
evidence established that A2C is camera-stable and preserves useful opaque
depth intersections, with 8x MSAA giving the preferred visual result. The
remaining blockers are per-material selection, complete directional/local
shadow interaction, and emitter visibility through multiple covered layers.

An earlier weighted-OIT replay is not the current baseline. Although it could
blend smoothly, it disappeared at some camera angles and interfered with
textures, opaque geometry, UI, fog, and water. Weighted OIT will return only as
an explicitly selected material mode after a read-only MTR parameter census.

The native Linux MTR census mapped regular transparency to bucket 1,
`sample_framebuffer 1` to bucket 5, `sample_framebuffer 2` to bucket 6, and
`volumetric 1` to two passes in buckets 1 and 5. Framebuffer-2 visibly erases
emitters behind the surface and cannot be classified as ordinary transparency.
See [TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md) for exact state and the
implementation checkpoints.

## Current limitations

1. Static caster submission follows NWN's normal visible draw list. A caster
   outside the camera frustum can therefore be absent even if its shadow would
   fall on visible geometry.
2. NWN's automatic second-story tile hiding can change that list at an
   overhang boundary, causing static-shadow flicker. The next design is a
   private two-generation static cross-fade; no engine draw-list mutation is
   acceptable.
3. Local shadows cover a downward contact cone, not all directions around a
   point light.
4. Windows' `lightpriority` field offset is not verified, so the Windows-only
   priority sort is disabled.
5. The result is an experimental diagnostic/composite system, not a complete
   replacement for NWN's lighting, stencil, or material shadow semantics.
6. The A2C foliage branch is experimental. Multiple layers can consume all
   covered MSAA samples and hide later emitters; an explicit transmittance path
   is planned rather than making emitters unconditionally depthless.

## Development controls

The development ImGui panel is the supported tuning interface. Important
controls include:

| Section | Controls |
| --- | --- |
| Sun | strength, bias, cascade overlap, PCF, composite |
| Area | day/night fade |
| Local light | enable, strength, falloff, cone angle, lamp lift, edge fade, slope bias, normal bias, minimum separation, map size |
| Performance | cascade count, moving/static caster policy, world-map size/extent, receiver and capture A/B controls |
| Diagnostics | receiver debug modes, frame-cost and target status |

Shipping builds hide controls that can remove the effect, omit diagnostic PGM
and frame-cost work, and carry their own defaults. Environment variables remain
useful for startup A/B tests; the source and panel are the authority for exact
names and supported ranges.

## Build and test

```bash
make
make deploy
make portable
cd win && make
python3 check_shaders.py
```

Use `./run-dev.sh` for the development path and
`./run-shadowmap-trace.sh` for a non-rendering trace. Set
`NWN_SHADOWMAP_GAME_DIR` if automatic game-directory discovery is insufficient.
The deploy path uses `nwn-shadows.sh` beside `nwmain-linux`.

## Source navigation

The root flow is in `nwn_shadowmap.cpp`. The main implementation modules are:

- `shadow_trace_cascade.inc` — scene and bucket hooks;
- `shadow_replay.inc` — cascade, world-map, and local bucket replay;
- `shadow_local_lights.inc` — local source preparation and capture state;
- `shadow_shader_interposition.inc` — shader/draw interception;
- `shadow_overlay_runtime.inc` and `shadow_fullscreen_receiver.inc` — receiver,
  overlay, and frame ordering;
- `shadow_diagnostics_settings.inc` — settings and diagnostics.

See [CURRENT_TASK.md](CURRENT_TASK.md) for the active blocker,
[TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md) for transparency evidence, and
[AGENTS.md](AGENTS.md) for invariants.
