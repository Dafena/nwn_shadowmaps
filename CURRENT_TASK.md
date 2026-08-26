# Current task

Updated 2026-08-26. Linux remains the behavioural reference, and the
maintainer has now explicitly requested planning and implementation work for
Windows.

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
