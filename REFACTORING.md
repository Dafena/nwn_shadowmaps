# Shadow injector refactoring checkpoint

Updated: 2026-08-13

## Frozen working baseline

The user-confirmed single-local-light implementation is preserved at:

`../savepoints/2026-08-13-refactored-single-light-runtime-confirmed`

That snapshot is the recovery source of truth. It is the modular refactor after
an in-game confirmation and has working sun CSM, NWN's engine-selected local
light, visible dynamic casters (`render 1` regardless of the MDL `shadow`
flag), exclusion of invisible stencil proxy geometry, alpha materials, and the
settings overlay. Supporting additional local shadow lights begins only after
this checkpoint.

## Refactoring rules

1. Preserve behavior before improving architecture.
2. NWN engine decisions remain authoritative, especially
   `LightManager::GetShadowLights()` order.
3. Do not alter renderer timing, capture publication, GL restoration, caster
   classification, settings keys, launcher defaults, or exported symbols in a
   mechanical extraction.
4. Build development and shipping variants after every extraction.
5. Compare the shared library's exported dynamic-symbol set with the frozen
   savepoint and run `ldd -r` before asking for an in-game test.
6. Start coupled renderer sections as `.inc` implementation modules in the same
   translation unit. This reduces the monolith without splitting global state
   or changing initialization order. Promote a module to a separate `.cpp`
   only when its interface is small and explicit.
7. Pure helpers with no renderer state may be separate object files immediately.

## Current module map

| Module | Form | Ownership |
| --- | --- | --- |
| `nwn_shadowmap.cpp` | translation-unit root | Hook lifecycle, shared renderer state, capture scheduling, receiver orchestration |
| `shadow_config.{h,cpp}` | separate object | Shipping defaults and memoized environment lookup |
| `shadow_math.{h,cpp}` | separate object | Vector/quaternion helpers, matrix operations and projection builders |
| `shadow_gl_api.inc` | same-TU implementation | Headerless OpenGL types/constants/function-pointer surface and GL resolver helpers |
| `shadow_engine_bindings.inc` | same-TU implementation | Mangled engine symbols, binding table and resolution helpers |
| `shadow_targets.inc` | same-TU implementation | Shadow target allocation, texture/FBO lifecycle and target validation |
| `shadow_diagnostics_settings.inc` | same-TU implementation | Readback/dump diagnostics and persisted settings helpers |
| `shadow_replay.inc` | same-TU implementation | Engine bucket replay, replay guards and draw accounting |
| `shadow_shader_interposition.inc` | same-TU implementation | Shader interception, visible-draw classification and duplication bridges |
| `shadow_fullscreen_receiver.inc` | same-TU implementation | Fullscreen receiver program, uniforms, depth reconstruction and composite draw |
| `shadow_overlay_runtime.inc` | same-TU implementation | Overlay runtime, input toggle and live settings application |
| `shadow_trace_cascade.inc` | same-TU implementation | Trace probes, cascade construction/capture and cascade diagnostics |
| `shadow_local_lights.inc` | same-TU implementation | Engine-selected local-light capture, visible dynamic casters and local targets |
| `nwn_overlay_imgui.cpp` | existing separate object | ImGui settings overlay |
| `nwn_oit.cpp` | existing separate object | OIT implementation |

The root source has fallen from approximately 12,843 to 3,024 lines. The eight
new subsystem modules total 9,136 lines; no large implementation was deleted or
rewritten. They are included at the exact positions occupied by their original
source ranges, preserving declaration order and internal linkage.

This is a deliberate intermediate architecture. Same-TU modules make each
renderer subsystem small enough for bounded inspection and Codex context while
retaining the monolith's shared private state. Promote them to independent
`.cpp` objects only after their interfaces have been made explicit and runtime
behavior has passed another gate.

## Verification completed

- Development build: pass.
- Shipping/deploy build: pass.
- `ldd -r`: no unresolved relocations.
- Exported dynamic-symbol set: identical to the frozen working `.so`.
- Configuration and math symbols use hidden visibility and do not expand ABI.
- Preprocessed root before and after the subsystem split is byte-for-byte
  identical (`g++ -E -P`), SHA-256
  `2341d35523c4671c4f04485adb76975a99f777370a6f3423e82497247a4c5aa7`.
- `run-shadowmap-trace.sh` now resolves both nested `refactored/` and older
  one-level layouts, or accepts `NWN_SHADOWMAP_GAME_DIR`; it no longer looks for
  `nwmain-linux` inside `csm_claude/`.
- In-game test of the refactored build: **passed**. The maintainer confirmed the
  split works correctly and preserves the accepted single-light behavior.

## HISTORICAL -- Active workspace and next feature

> **SUPERSEDED.** The refactor is complete and `refactored/` IS the tree you are reading. The source-budget description predates the sun-path rewrite.


`refactored/` is now the authoritative workplace. The parent tree and all
savepoints are read-only references. The source-budget activation is compiled
through `shadow_local_lights.inc`: persisted `local_cube_sources` owns the 1--3
expensive source budget (NWN's native maximum), while `g_lampUploadMax` remains
lift/census only. It preserves engine priority, visible-caster classification,
and exact one-source fallback behavior. Both builds pass and are clean under
`ldd -r`. Publication is atomic at source-set level: every requested slot must
receive a visible draw, otherwise the cleared partial generation is withheld.

**Multi-source is CONFIRMED IN GAME (2026-08-13).** The "next gate is a
two-source in-game confirmation" line this paragraph used to end on is done;
the maintainer runs three sources.

### How the budget relates to NWN's own slider (audited 2026-08-13)

Two separate limits, and the distinction matters because only one of them is
ours:

| | set by | caps |
| --- | --- | --- |
| NWN's `Shadow Casting Lights` (`max-casting-lights`, default 3) | the game's video options | how many lights `LightManager::GetShadowLights()` returns -- our CANDIDATES |
| `local_cube_sources` (panel: "Local shadow sources") | this injector | how many of those candidates we consume, clamped 1..3 |

No code reads `NumShadowCastingLights` directly. The game's slider reaches us
only by sizing the engine list we consume verbatim, which is exactly the
"NWN stays authoritative" rule.

**The compiled default is 1, not 3.** `shadow_targets.inc` initialises
`g_localCubeSourceBudget = 1` and `settings_reset_defaults()` sets 1. A
maintainer .ini carrying `local_cube_sources=3` is a SAVED value -- "Restore
defaults" drops it back to one source, and a fresh install or a tester starts at
one. Change both sites together if the shipped default should be three.
