# Firelight

`src/fire_light.inc` adds warm, flickering per-pixel light around torches,
braziers and campfires.

## How it works

- Lights come from the engine's own census (`SetLightGL`, published as
  `g_lampLights` at the frame boundary): only lights the engine enabled this
  frame, at the engine's position and radius. Nothing is selected, suppressed
  or replaced in the engine.
- A light counts as fire when `lamp_is_fire` accepts its colour: orange
  through yellow flame (red leads, green 0.2-1.05 of red, blue at most 0.6 of
  red, clearly saturated). White, blue, green and warm-white electric lamps
  are excluded.
- Fires are lit in a true flame orange (1, 0.52, 0.18), nudged a quarter of
  the way toward the light's own hue, not the engine colour, which in some
  areas is yellow-green. With firelight on, the fog glow around fires uses the
  same colour at the engine light's brightness.
- The nearest 32 fires (by distance to the camera minus radius) feed one
  fullscreen pass after the scene. Each pixel's world position is rebuilt
  from this frame's scene depth (the sun-shadow receiver's copy) and the
  surface is modulated by its own colour (`DST_COLOR, ONE`).
- Each fire flickers on its own (slow sway, flutter and fast crackle, seeded
  by the engine light's address), mostly brightening and dipping slightly
  below the engine's steady light, and its light centre dances by a few
  centimetres so lit patches move. A wrapped Lambert term from depth-derived
  normals dims faces turned away; the reach is 85% of the engine radius.
- No occlusion, like the engine's own lamp lighting.

## Settings

| Setting | Env | Default | Panel |
| --- | --- | --- | --- |
| `fire_light_enabled` | `NWN_SHADER_FIRE_LIGHT` | 1 | Firelight > Flickering firelight |
| `fire_light_strength` | `NWN_SHADER_FIRE_LIGHT_STRENGTH` | 1.0 (0..3) | Firelight strength |

The panel shows how many fire lights are being drawn. Without scene depth
this frame (sun shadows off) the pass is skipped.

## Status

Builds on Linux and Windows; the GLSL validates. Not yet verified in game:
whether the colour test catches the module's torches and campfires (and only
them), and the flicker strength.
