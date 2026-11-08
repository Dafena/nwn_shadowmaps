# Current task — release integration audit (2026-11-08)

Integration branch: `integration/plugin-release-audit-20261108`.
Baseline: `b47f980`. Scope, decisions and evidence status live in
[docs/RELEASE_AUDIT.md](docs/RELEASE_AUDIT.md).

## Accepted behavior to preserve

- Native directional/local shadow policies, material Mode2 A2C and automatic
  clear/rain/snow surfaces. Mode3 OIT remains parked; it is not reactivated.
- Authored continuous-alpha geometry and UV2.x alpha remain unchanged. Explicit
  material priorities0–9 select complete opaque-core/fade groups with solid
  depth testing. Unknown/framebuffer-sampling/water/replay draws remain native.
- WorldMaker v6 per-fragment lighting fixes missed local lights on broad floors.
  Older HAKs require matching metadata/shaders; the plugin cannot infer missing
  mesh priorities from a shared material. See docs/LAYER_STACK.md.
- Shared resources follow nwn.ini aliases. Never overwrite installed HAKs,
  modules, saves or proxy DLLs as part of an isolated test.

## Incorporated work

Rain PR4 is merged into this branch. Both private rain and native-rain hiding
are optional and default off. The new pass retains engine weather authority,
completed shelter/depth data and existing surface-weather behavior.

The rejected A2C depth-copy trial is removed from active source. Its rationale
and accepted alternative are recorded in docs/LAYER_STACK.md; the original
experiment is recoverable from the pre-audit checkpoint commit.

Layer scheduling now has a Windows-specific first-item adapter and exported
Part draw hook, gated on complete safe hook installation. Linux scheduling is
the behavioral reference. Scene-local visit generations and bounded scene
recycling prevent GUI renders or repeated area changes from losing discovery.

## Validation limits and next work

- Linux development, shipping and Windows builds must all stay current.
- DLL exports/symbol checks are not runtime validation.
- Native Windows verification remains explicitly pending by maintainer choice.
  Proton checks are valuable Windows-path smoke/visual checks, not native-driver
  certification. Do not present earlier Windows weather acceptance as proof of
  the new layer behavior.
- Actual Linux/Proton matched views and frame-time checks are in progress.
  Timing-overlay-injected startup failures are excluded from comparisons.
- Final crowded multiplayer15actor snow-trail acceptance and high-budget rain
  acceptance remain separate limitations; see WEATHER_PARTICLES.md and
  WINDOWS_IMPLEMENTATION_PLAN.md.
- Second-story tile shadow strobes remain deferred. Do not reintroduce full
  unculled BSP every frame or draw-count-based generation guesses.

Useful findings from the previous session are preserved in source history and
local audit evidence, rather than carried as hundreds of stale task updates.
