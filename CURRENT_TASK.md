# Current task

Updated 2026-09-03. Linux remains the behavioural reference. The automatic
clear/rain/snow weather-effects system, including precipitation occlusion and
height-aware snow deformation, is accepted on Linux. A Windows weather port
remains a separate, explicitly requested task.

## Accepted Linux baseline

- Directional cascaded shadow maps and local-light shadow maps are working.
- Explicit material Mode 2 (`parameter int NWN_ALPHA_MODE 2`) selects the
  accepted alpha-to-coverage path when MSAA is available. Unmarked, unsupported,
  framebuffer-sampling, and volumetric materials remain native.
- A2C receives directional and local shadows and preserves opaque intersections.
  Its discrete sample mask and reduced visibility for emitters behind many
  overlapping layers are accepted limitations/TODOs.
- Material routing uses hashed concrete/shared identities, refreshes late MTR
  fields, and recycles entries through Linux destructor hooks. Native-area
  performance and native-to-authored area transitions passed the maintainer's
  runtime tests.
- Supported custom continuous-alpha shaders may publish final fragment alpha
  with `#define NWN_CONTINUOUS_ALPHA_SHADER 1` for the injector's Mode 2 path.

## OIT policy

Explicit Mode 3 weighted OIT is parked. `NWN_OIT_MODE3=1` and the
`NWN_OIT_MODE3_*_CENSUS` family are intentionally ignored; a marked Mode 3
material fails closed to native NWN rendering. The implementation and Linux
evidence remain in-tree for a deliberate future restart, but Mode 3 is not an
active development, production, shipping, or Windows-porting target.

The final Linux prototype did prove a native-pivot opaque core plus weighted
soft fringe, pre-water composition, stock fog/lighting retention, cross-mode
cutout occlusion, and same-bucket character occlusion. It is parked because the
remaining complexity and compromises are not currently worth productizing.
Historical experiments and commands remain in
[TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md), clearly marked as dormant.

## Resource-path invariant

Native Linux NWN intentionally follows aliases in
`/home/fede/.local/share/Neverwinter Nights/nwn.ini` into the Proton prefix, so
Linux and Proton share the same development resources. The `users/fede` and
`users/steamuser` development paths resolve to the same inode. Do not deploy
duplicate overrides into the unaliased home `development` directory.

## Accepted Linux weather-effects baseline

The automatic Linux implementation lives in `weather_runtime.inc`:

- `CNWCArea::SetWeather(unsigned char, float)` is the authoritative automatic
  weather source. Linux disassembly confirms weather values 0 clear, 1 rain,
  2 snow and density in the 0..100 range. The injector normalizes density and
  maintains gradual wetness accumulation/drying without module configuration.
  Wetness is scoped to the current `CNWCArea`: area and save-load transitions
  reset inherited wetness immediately, while weather changes within one area
  retain the accepted fade. The stock `areaFlags` uniform is the authoritative
  interior test (`NWAREA_FLAG_INTERIOR`): interior draws suppress weather on
  their first frame and clear both surface reservoirs immediately, including
  when NWN reuses the same `CNWCArea` allocation across a load screen.
- The selected `g_areaScene` gate is mandatory. Draw classification tracks
  NWN's `skinmesh` uniform from its own `glUniform1i` uploads, avoiding a
  synchronous driver query per draw. Static opaque bucket 0 receives the full
  effect. Static alpha/card bucket 1 receives orientation-aware wet noise and
  normal impacts without puddles. Dynamic body/hair buckets, skinned
  characters, native water, and unknown/out-of-bucket draws are excluded.
- The weather hook uses Linux's established remove/call/reinstall fallback
  because this executable's function prologue does not yield a Subhook
  trampoline. Native Linux runtime testing confirmed clear, snow, and rain
  transitions, area changes, and save loading.
- Eligible opaque surfaces gain gradual wet film and compact world-anchored
  procedural puddles. Standing water is limited by geometric and smoothed
  surface flatness; steep geometry only becomes wet. Puddles reuse NWN's
  framebuffer and water-noise inputs for bounded screen-space reflection,
  refraction, and animated normal ripples. Non-puddle wet surfaces use moving
  micro-normal noise and smaller normal-only rain impacts.
- Rain contributions fade through NWN fog. Mode 2 A2C remains enabled by
  default and was kept outside the rain shader's RGB-only static-alpha work.
- Rain and snow share a separate world-anchored top-down precipitation depth
  map. It follows the configured static-world extent and resolution (including
  the 16K maximum), requests NWN's complete static BSP only when stale, and is
  reused until the area changes or the camera crosses 60% of its extent. The
  stable `CNWCArea::SetWeather` pointer keys the cache; transient torch/shadow
  objects cannot trigger repeated captures. Static opaque bucket 0 and static
  alpha/card bucket 1 accumulate into one capture. Stock alpha-discard holes
  remain open, while solid transparent-mesh fragments block precipitation;
  characters never enter the blocker map. Receiver-height depth comparison
  leaves rooftops exposed while sheltering geometry below them. A stationary
  16-sample world-space disk with geometry-anchored dithering softens blocker
  borders without camera shimmer, discrete bands, or displaced roof edges.
  Every rain contribution and snow coverage is multiplied by this exposure.
- Clear, rain, and snow are one authoritative state machine. Falling
  precipitation remains mutually exclusive, while accumulated rain and snow
  are independent surface reservoirs: the old layer fades as the new layer
  builds, matching clear-weather drying/melting instead of popping off. Rain
  and snow use the same switch-away/clear fade rate.
- The snow receiver adds world-anchored high-frequency accumulation on upward
  static opaque geometry, NWN fog fading, normal variation, and a bounded
  view-dependent raised parallax height walk. Bucket-2 creature transforms
  feed 16 bounded world-space character histories: resolved ground-contact
  movement carves up to 64 continuous rounded trail segments per character
  into a 512x512 RGBA32F deformation texture. Its first two channels store
  deformation depth and contacted world height, preventing a creature below a
  roof or bridge from deforming snow above it. Each character's
  oldest segment enters a two-second retirement fade at capacity; the texture
  is refreshed at 10 Hz with gradual refill and reset on area transitions.
  Slot 0 and a separate retirement pool are reserved for the camera-target
  player, so NPC pressure cannot evict that trail. Snow shaders sample this
  texture instead of evaluating segment uniform arrays per fragment. The
  occlusion fringe suppresses parallax and relief normals until the surface is
  nearly fully exposed, so a soft shelter transition cannot become a raised
  snow bank. Snow is a neutral material relit from NWN's decoded area light
  and complete enabled local-light census (up to the engine's 128-light
  setting), rather than inheriting the covered tile's albedo. Snow
  settlement trusts geometric slope and only mildly modulates with the
  material normal, while an albedo-independent illumination floor keeps the
  layer visible on dark tilesets that do not already use snow textures. Near
  full accumulation a continuous minimum blanket and 97% snow composite stop
  bright local lights from revealing the base texture as a lifted-snow halo.
- Windows currently compiles no-op weather stubs only. Do not add Windows weather
  bindings without an explicit port request. If the future Windows port fails
  while Linux works, ask the maintainer before changing shared behaviour.

The feature requires no environment variable, module edit, hak, shader
override, or user-authored material marker.

The maintainer confirmed the final Linux regression set on 2026-09-03:
clear/rain/snow transitions, save and area changes, immediate interior reset,
fog, opaque and static-transparent shelter, torch lighting, shadows, A2C,
native-water exclusion, crowded creature trails, player-trail priority, and
the 8192x8192 occlusion target all behaved correctly with no noticed
performance regression. A 16K depth target is supported by the configured
path but consumes about 1 GiB of VRAM and was not part of that runtime pass.

## Next work

The Windows proxy/artifact, directional-shadow, and local-light parity
checkpoints are now confirmed in game. Directional sun/moon shadows render,
local lights cast visible shadows, and torch-lit surfaces retain their local
lighting through the directional composite without camera-distance changes.
Windows uses a `0` default for the `Bright surfaces keep light` start threshold;
Linux retains its validated `0.85` default.

Windows product settings/logging are also confirmed. The read-only Windows
material transport now has runtime proof for creation, shared-field parsing,
per-draw binding, texture-name lookup, and initialization-time pointer-reuse
reset. The exported MSVC destructors are deliberately refused because their
nominal Subhook trampolines crash when returning to the original function;
Windows resets reused shared identities at `SharedMaterial::Init` instead.
The authored-material census passed a native-area-first transition on
2026-08-26: the same base texture produced distinct stable routes for native
Mode 0, authored Mode 2, and parked Mode 3 materials. Visible Windows Mode 2
A2C is also confirmed: Mode 2 alone selected A2C with live MSAA, while Mode 0
and parked Mode 3 remained native. Directional/local A2C reception, stable
camera motion, engine-budget sun lifting across camera zoom, and selectable
local-shadow refresh are confirmed. Checkpoint 7 is complete; checkpoint 8
regression/performance is next. Keep Windows-only mechanics behind Windows
paths and do not alter working Linux behaviour to solve a Windows symptom.
Mode 3 OIT is explicitly outside the Windows scope. The staged plan and exit
criteria are in
[WINDOWS_IMPLEMENTATION_PLAN.md](WINDOWS_IMPLEMENTATION_PLAN.md).

The shipping panel now exposes local-shadow refresh as Low (25 ms), Medium
(16 ms), or Ultra (every rendered frame). Low preserves the established cost;
Ultra deliberately matches the sun-shadow update cadence. This cadence does
not change the separate three-light local shadow-map budget or the ordinary
light list used for sun-shadow lifting.

Windows planning must account for:

- proxy-DLL installation and OpenGL/IAT hook parity;
- engine symbol/field evidence rather than Linux offsets;
- directional and local shadow-map parity;
- material identity lifecycle and MTR mode routing;
- MSAA detection and Mode 2 A2C state restoration;
- settings/logging parity and matched Linux/Windows regression scenes;
- the still-unverified Windows `lightpriority` field, which remains disabled.
