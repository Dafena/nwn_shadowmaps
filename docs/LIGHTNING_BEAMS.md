# Procedural lightning beams

`src/lightning_beams.inc` replaces the native strips of EOM's lightning beam
(`vdu_beam000`, texture `eom_lbolt01`) with a procedural beam: a braid of fine,
flickering strands that meet at the source, wander and spread toward the
target, with an HDR white core (picked up by the engine's bloom), a blue glow
and a faint haze.

## How it works

1. Both texture-bind observers (`MaterialBindAllStandardTextures_detour` and,
   on Windows, `SharedMaterialBindRain_detour`) report the bound base texture.
   Only the exact resource `eom_lbolt01` arms the module; stock beams and every
   other emitter are untouched.
2. At the engine draw (`glDrawElements`, `glDrawArrays`, `glDrawRangeElements`
   and the base-vertex variants), an armed draw in a normal area pass is read
   back: the program's `vPos` attribute (float, size >= 3), its index and
   vertex buffers (`glGetBufferSubData`, never on a mapped buffer) and `m_mv`.
   The vertices go to world space through `viewInverse * m_mv`, and the strip
   is reduced to its two ends (farthest pair; each end is the mean of the
   vertices near it, so the strip half-width cancels). The end nearer the first
   submitted vertex is the source.
3. Only if every read succeeded and the private program is ready is the native
   draw skipped. Any failure logs a reason (first few only) and keeps the
   native strip.
4. Strips with the same ends in one frame (the beam model's three emitters)
   merge into one beam. After the scene, before the rain pass, each beam is
   drawn as a camera-facing ribbon with additive blending, depth test on and
   depth writes off, through the rain pass's `StateGuard`.
5. Before the strands, a fullscreen light pass per beam rebuilds each pixel's
   world position from this frame's scene depth (the sun-shadow receiver's
   copy) and brightens surfaces within a few metres of the beam by their own
   colour (`DST_COLOR, ONE`), with a wrapped Lambert term from depth-derived
   normals and the strands' flicker. Without scene depth this frame (sun
   shadows off) the light is skipped and the beams still draw.
6. The ribbon runs past both ends and measures distance including the
   overshoot, so glow and haze close in rounded caps at hand and target.
7. Beam ends are tracked across frames as impact points (target) and source
   points (hand). Each gets a camera-facing disc with an impact flash (a
   burst on first contact), crackling arcs and a swirling plasma blob; the
   target also throws 40 stateless sparks under gravity (depth-tested, so
   they vanish into the floor) and lights its surroundings. After the beam
   stops, the plasma, sparse arcs and the light fade over one second and
   already-emitted sparks keep falling: the residual plasma.

## Strikes (Call Lightning)

EOM's `vim_exp2light` (VFX_IMP_LIGHTNING_M) is the stock model with only its
strike strip texture renamed to `eom_lstrike01`. A draw with that texture is
captured like a beam strip, but reduced to its ground contact (the mean XY of
all referenced vertices of the vertical, symmetric strip, at the mean height
of its two lowest); strikes are tracked across frames because the native
particles fly down the strip. Each strike draws:

- a swirling storm cloud (10 m radius, 18 m up) the strike is summoned
  from: dark spiral, bright eye, lit from inside by each stroke;
- an 18 m bolt into the cloud (fading near the eye) on the beam ribbon: a jagged main channel (wander fixed per
  strike, jitter re-formed each return stroke), a braided column of thinner
  strands and down-and-out forking branches;
- return strokes: a flash at contact, then every 70-270 ms, each decaying
  over a dim continuing current, driving bolt, ground and light brightness;
- a ground decal from scene depth (5 m): radial forking arcs crawling over the
  real surfaces, a shock ring on first contact and a plasma pool;
- 120 sparks thrown up and out under gravity;
- column and ground light; the ground light, plasma and sparse arcs fade for
  1.6 s after the native effect ends.

The model's own sparks and flickering light remain native; its ground flare
is kept but dimmed (alpha 0.5 to 0.2).

## Settings

| Setting | Env | Default | Panel |
| --- | --- | --- | --- |
| `lightning_enabled` | `NWN_SHADER_LIGHTNING` | 1 | Lightning beams > Procedural lightning |
| `lightning_intensity` | `NWN_SHADER_LIGHTNING_INTENSITY` | 1.0 (0..4) | Lightning brightness |
| `lightning_light` | `NWN_SHADER_LIGHTNING_LIGHT` | 1.0 (0..3) | Light on surroundings |

Turning the setting off restores the native textured strips immediately.

## Status

Builds on Linux (development and shipping) and Windows; the GLSL validates.
Native runtime behaviour (draw path used by particles, readback, end
detection, look) is not yet verified in game. The log lines
`[shadowmap][lightning] strip ...` and `native strip kept: ...` are the first
evidence to collect.
