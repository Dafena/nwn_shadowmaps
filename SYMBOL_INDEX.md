# Renderer symbol and recovery index

Updated: 2026-08-13

This is the compact navigation map for `nwn_shadowmap.cpp`. Read
`CURRENT_TASK.md` first, then use this file to open only the relevant source
window. The source is about 12.7k lines (roughly 159k of text), so broad dumps
are both noisy and harmful to context retention.

Line numbers are approximate. Refresh a location with one narrow `rg -n` call
after edits; do not regenerate this index by printing the entire source.

## Fast recovery sequence

1. Read `CURRENT_TASK.md` for the current objective and last known result.
2. Find the symbol in the tables below.
3. Refresh it with `rg -n "exact_symbol" nwn_shadowmap.cpp`.
4. Inspect at most 40–120 lines around that result.
5. Update `CURRENT_TASK.md` after every source patch/build/test request.
6. Update this index only when ownership, flow, or relevant symbols change.

## Renderer invariants

- NWN's `LightManager::GetShadowLights()` order owns local-light selection.
  Never replace it with camera distance, actor distance, visibility, inferred
  ownership, or another injector ranking.
- A local-map caster is normal visible geometry: `render 1 shadow 0` and
  `render 1 shadow 1` cast. Invisible `render 0 shadow 1` stencil proxies do
  not cast into the injected map.
- Determine visibility from normal colour submissions, not guessed MDL memory
  offsets. Stencil/depth-only submissions and learned stencil programs are not
  normal visible geometry.
- Re-use the original draw's shader, material, textures, alpha test, skinning,
  and animation. The injector changes the target and light transform only.
- A published light matrix and its depth layer are one generation. Never expose
  a new matrix with an old map, or vice versa.
- The receiver intentionally runs before regular local capture and consumes the
  preceding completed generation. Moving capture before the receiver breaks the
  receiver's scene-depth copy.
- Restore framebuffer, viewport, draw/read buffers, colour/depth/stencil state,
  active texture, program, VAO/buffers, model-view, and projection after an
  injected draw.
- Restrict work to the selected area scene. Menus and auxiliary scenes must not
  enter the local caster path.

## Current frame flow (implemented)

```text
Scene::Render detour
  -> capture selected-area camera/frame context
  -> normal engine rendering
  -> injected receiver uses the previous completed local map
  -> capture_local_light_shadow()
       -> reads NWN's first selected shadow light
       -> creates/clears a 6-layer depth-array target
       -> builds six point-light face matrices
       -> currently replays engine shadow buckets 0..3
       -> publishes matrix/map metadata for the next receiver frame
```

The bucket replay is the known caster-selection defect. It inherits the stencil
shadow population, so it can omit visible `shadow 0` pieces and include invisible
stencil proxies.

## Intended visible-caster flow (next patch; not implemented yet)

```text
after receiver, frame N
  -> retain NWN's selected light and prepare/clear regular local layers

normal dynamic geometry, frame N+1
  -> enter Scene::RenderDynamicGeometry detour
  -> each intercepted normal visible draw is duplicated to all six light faces
  -> stencil/depth-only draws are rejected
  -> leave the normal stage and mark the generation complete
  -> publish its matrices and depth layers together

receiver, following frame
  -> consumes that coherent completed generation
```

The existing cube experiment already proves the interception, re-projection,
alpha/material preservation, and restoration mechanics. Generalize that bridge
for the regular local target; do not invent another caster classifier.

## Engine light selection and stencil reference

| Symbol / search text | Approx. line | Ownership / purpose |
| --- | ---: | --- |
| `LightGetShadowLights_t` | 1361 | Recovered engine function type |
| `eng::LightGetShadowLights` | 1395 | Resolved engine entry point |
| `struct EngineShadowLightEntry` | 1692 | Captured engine-selected light record |
| `g_engineShadowLights` | 1693 | Authoritative selected-light order retained by injector |
| `g_inStencilShadow` | 3942 | True while engine stencil shadow submission is active |
| `g_stencilPrograms` | 3944 | Learned stencil-only program IDs |
| `LightPrioritizeShadow` | 9234 | Diagnostic trace for engine prioritization |
| `LightGetShadowLights_trace_detour` | 9314 | Captures the engine list without replacing its policy |
| `g_engineShadowLights[...] =` | 9402 | Exact engine-list population site |
| `g_engineShadowSelectedKey` | 9415 | Stable identity diagnostic for selected light |
| `g_engineShadowSelectedFrame` | 9418 | Frame in which engine selection was observed |
| `LightGetShadowLights` hook install | 11314 | Normal hook installation region |
| trace hook installation | 12540 | Optional diagnostic installation region |

Stencil program learning is around lines 4086–4105 and 5558–5568. Engine
stencil-shadow draws are observed around line 5668. These are useful only as a
negative classifier/reference; they are not the desired caster source.

## Frame context, transforms, and GL restoration

| Symbol | Approx. line | Ownership / purpose |
| --- | ---: | --- |
| `struct NativeTransformSlot` | 1041 | Per-program native transform capture |
| native transform slots | 1056 | Fixed transform-slot storage |
| `struct ShadowFrameContext` | 1703 | Selected-area camera, viewport, frame matrices |
| global frame context | 1718 | Latest valid selected-area context |
| `struct SavedState` | 2865 | GL state snapshot used by injected passes |
| `bind_target` | 2874 | Common injected-target setup |
| `restore_state` | 2892 | Common GL state restoration |
| `native_transform_slot` | 5118 | Finds/updates transform state for current program |
| `capture_native_transform` | 5144 | Captures normal draw matrices/view inverse |
| `begin_cascade_light_transform` | 5180 | Sun/cascade transform model reference |
| `end_cascade_light_transform` | 5216 | Cascade matrix restoration reference |
| `capture_shadow_frame_context` | 8533 | Captures selected-area camera and viewport state |
| `SceneRender_detour` | 11280 | Top-level scene flow and receiver/capture ordering |

Important patch site: `capture_native_transform()` currently recognizes cascade
and cube-capture activity. A regular visible-caster bridge must add its activity
there without changing ordinary rendering.

## Regular local depth-array target

| Symbol | Approx. line | Ownership / purpose |
| --- | ---: | --- |
| `g_localLightCapture` | 327/504 | Enable flag and configured local map size (conditional defaults) |
| `g_localLightReceiver` | 333 | Receiver enable flag (conditional defaults) |
| `g_localLightSelectedPtr` | 1680 | Current selected engine light pointer |
| `g_localLightSelectedInfo` | 1681 | Current selected light metadata |
| `g_localLightSelectedFrame` | 1683 | Selection generation/frame |
| `g_localLightFbo` | 1914 | Regular local capture FBO |
| `g_localLightDepthArray` | 1915 | Six-layer regular local depth texture |
| `g_localLightTargetUsable` | 1916 | Target completeness/availability |
| `g_localLightFaceVP` | 2040 | Published VP matrix for each of six faces |
| `g_haveLocalLightVP` | 2042 | Published matrix validity |
| `g_localLightVPFrame` | 2043 | Published local-map generation |
| `g_localLightWorldPos` | 2044 | Published selected-light position |
| `g_localLightWorldRadius` | 2045 | Published selected-light radius |
| `create_local_light_target` | 10078 | Allocates depth array and FBO |
| depth-array allocation | 10096 | Texture storage setup |
| local FBO creation | 10118 | FBO allocation/setup |
| target completeness check | 10127 | Sets target usable state |
| `bind_local_light_target` | 10141 | Binds regular target/layer state |
| `capture_local_light_shadow` | 10529 | Current light-face setup and bucket replay |
| regular target resize/recreate | 10544 | Reallocates when configured size changes |
| engine list slot zero selection | 10574 | Uses NWN's first selected shadow light |
| radius stability/owner logic | 10674 | Maintains selected light radius metadata |
| face VP construction | 10695 | Builds six light views/projections |
| metadata publication | 10710 | Publishes VP/position/radius/frame today |
| regular target bind | 10728 | Starts capture target work |
| capture draw counter reset | 10767 | Per-capture diagnostics |
| depth-array layer attach | 10814 | Attaches one of six faces |
| bucket replay loop | 10899 | Current defective caster source |
| PGM readback | 11030 | Optional local map diagnostic dump |
| capture generation increment | 11227 | Completes current capture generation |
| regular capture summary | 11234 | Concise diagnostics |
| `capture_local_light_shadow(self)` | 11519 | Call site after receiver |

Generation hazard: current metadata is written around 10710 before bucket replay
finishes. A staged visible path must keep prepared metadata private, then publish
it only after the normal visible stage has completed every required face.

## Receiver path

| Symbol / search text | Approx. line | Ownership / purpose |
| --- | ---: | --- |
| `draw_static_receiver` | 7477 | Fullscreen/receiver injection path |
| local capture ordering comment | 7661 | Documents previous-generation design |
| local map age | 7668 | Computes receiver generation age |
| local receiver gate logging | 7679 | Explains why local path is enabled/disabled |
| `localReady` | 7685 | Requires enabled, target, texture, VP and age <= 1 |
| cube receiver age | 7688 | Cube experiment freshness gate |
| local/cube depth binding choice | 7771 | Chooses depth-array texture source |
| local depth-array bind | 7772 | Binds regular texture to receiver |
| local face VP upload | 7895 | Uploads six regular VP matrices |
| cube VP upload | 8019 | Cube experiment equivalent |
| local position/radius upload | 7965 | Receiver light metadata |
| cube position/radius upload | 8021 | Cube experiment equivalent |
| cube attenuation/range upload | 8028 | Cube experiment receiver settings |

The decisive lifecycle explanation is in the comments around 11501–11513:
receiver first, capture second. Preserve that ordering.

## Proven cube visible-draw bridge

| Symbol | Approx. line | Ownership / purpose |
| --- | ---: | --- |
| `g_localCubeFbo` | 1922 | Cube experiment FBO |
| `g_localCubeDepthArray` | 1923 | Cube experiment six-layer depth array |
| `g_localCubeTargetUsable` | 1924 | Cube target validity |
| cube `active` / `prepared` state | 1935 | Scope and preparation flags |
| cube VP generation | 1938 | Prepared matrix generation |
| cube active face / VP arrays | 1944 | Per-face transform state |
| cube dynamic draw counters | 1948 | Per-face successful duplicate counts |
| cube skip counters | 1953 | Why draws were rejected |
| cube selected key / anchor | 1984 | Stable selected-light identity/reference |
| `capture_native_transform` | 5144 | Shared native matrix capture; regular bridge must join gate |
| `begin_local_cube_dynamic_transform` | 5236 | Reprojects a visible draw for one light face |
| cube matrix restore | 5262 | Restores engine projection/model-view |
| cube layer bind | 5270 | Attaches one depth-array face |
| `local_cube_visible_dynamic_draw` | 5863 | Proven normal-visible classifier |
| `DUPLICATE_LOCAL_CUBE_DYNAMIC` | 5900 | Proven duplicate/reproject/restore macro |
| `SceneRenderDynamicGeometry_detour` | 9024 | Limits duplication to normal dynamic stage |
| cube prepare/fresh gate | 9037 | Activates only a coherent prepared generation |
| cube active scope | 9050 | Active only around original dynamic draw call |
| cube completion counters | 9065 | Aggregates duplicated draws |
| `create_local_cube_target` | 10163 | Cube target allocation reference |
| cube target bind | 10199 | Cube target setup reference |
| cube preparation | 10408 | Selects light, builds matrices, clears layers |
| cube selected-light change | 10443 | Handles engine selection changes |
| cube view/proj/VP build | 10468 | Builds private prepared transforms |
| cube layer clear | 10472 | Clears all six layers before duplication |
| cube publish/prepared state | 10478 | Records info, generation and readiness |

Classifier details to preserve:

- line ~5864 rejects draws while `g_inStencilShadow`;
- lines ~5869/5882 reject missing/invalid programs;
- line ~5876 requires normal RGB colour writes;
- line ~5888 rejects learned stencil-only programs.

## Intercepted draw coverage

Every supported draw wrapper invokes `DUPLICATE_LOCAL_CUBE_DYNAMIC`. A regular
bridge must share the same wrapper coverage, not add a second incomplete list.

| Wrapper | Approx. line |
| --- | ---: |
| `glDrawElements` | 5944 |
| `glDrawArrays` | 5967 |
| `glDrawRangeElements` | 6021 |
| `glMultiDrawArrays` | 6042 |
| `glMultiDrawElements` | 6064 |
| `glDrawElementsBaseVertex` | 6085 |
| `glDrawRangeElementsBaseVertex` | 6111 |
| `glDrawElementsInstanced` | 6133 |
| `glDrawElementsInstancedBaseVertex` | 6158 |

Prefer a shared helper/macro that can duplicate into cube experiment and regular
local targets independently. Do not fork eight wrapper implementations.

## Proposed regular visible-bridge state (names reserved; not implemented)

Use one explicit state group rather than overloading published receiver state:

- `g_localVisibleCaptureActive`
- `g_localVisiblePrepared`
- `g_localVisiblePreparedFrame`
- `g_localVisibleCompletedFrame`
- `g_localVisibleActiveFace`
- private prepared face `view`, `proj`, and `VP` arrays
- per-face successful draw counters
- rejection counters matching the cube classifier
- concise slot/light identity and completion diagnostics

The visible bridge was runtime-confirmed with one regular local light. The live
workspace now supports a configured 1--3 slot budget (NWN's native maximum); preserve the existing
single-light behavior as fallback when only one valid NWN entry exists. Do not
advertise partially filled layers.

### Regular local-light source ownership (2026-08-13)

| Search text | Meaning |
| --- | --- |
| `g_localCubeSourceBudget` | Persisted `local_cube_sources` setting; authoritative expensive shadow-source budget, clamped 1--3 (NWN native maximum) |
| `g_engineShadowLights` | NWN-selected and NWN-ordered source list; consume verbatim |
| `slotBudget` in `shadow_local_lights.inc` | Applies the 1--3 source budget without reranking |
| `g_localLightSlotCount` | Actual valid live slots captured and consumed by the receiver |
| `nwnLocalSlots` | Receiver uniform limiting the depth-array loop to live slots |
| `g_lampUploadMax` | Cheap light/lift census only; must never cap local shadow sources |
| `g_localLightLastCaptureTime` | Timestamp retained for diagnostics/future ping-pong maps; it must not throttle the current single-buffered target |
| `g_localLightCapturePolicyEpoch` | Bumped when quality, source budget, or local range changes; makes a replacement visible-dynamic capture immediately due |
| `g_localLightCompletedPolicyEpoch` / `g_localVisiblePolicyEpoch` | Tags a completed map with the policy used to stage it, so an in-flight old-policy map finishes safely and a current-policy replacement follows immediately |

The depth target, six-face-per-slot matrices, visible dynamic draw duplication,
and receiver loop were already array-based. The activation patch only decouples
the source budget from the lift census and exposes an honest live count.

## Configuration and lifecycle

| Search text | Approx. line | Purpose |
| --- | ---: | --- |
| `NWN_SHADOWMAP_STENCIL_TRACE` | 11671 | Stencil diagnostics |
| all-buckets setting | 11673 | Current bucket replay diagnostic mode |
| local capture enable | 11679 | Regular local capture configuration |
| local capture size | 11724 | Regular depth-map size |
| local dump setting | 11729 | PGM diagnostic output |
| local struct/direction overrides | 11730 | Reverse-engineering controls |
| local lift | 11746 | Capture position adjustment |
| local FOV | 11750 | Face projection control |
| local receiver enable | 11773 | Receiver configuration |
| local strength | 11779 | Receiver shadow intensity |
| local bias | 11784 | Receiver depth bias |

Keep experimental visible-caster fallback opt-out explicit if added, for example
`NWN_SHADOWMAP_LOCAL_VISIBLE_CASTERS=0`. Default it on only after the acceptance
test passes; until then, failed/incomplete visible generations fall back to the
existing bucket replay.

## Build, deploy, and launch map

| File / symbol | Approx. line | Purpose |
| --- | ---: | --- |
| `Makefile:CXXFLAGS` | 20 | Shared-library compile flags |
| `Makefile:TARGET` | 28 | `libnwn_shadowmap.so` output |
| `Makefile:all` | 33 | Normal build target |
| `Makefile:deploy` | 48 | Deploy/copy target |
| `run-dev.sh` library check | 40 | Development launcher verifies built `.so` |
| `nwn-shadows.sh` | 39 | Safe `LD_PRELOAD` setup for path containing spaces |
| `dist/nwn-shadows.sh` | 39 | Distributed launcher equivalent |

Use the existing launcher rather than hand-assembling environment variables.
`csm_claude` is not a Git repository; do not spend recovery turns on Git status
or history commands.

## Refactored module map (2026-08-13)

| File | Ownership |
| --- | --- |
| `shadow_config.{h,cpp}` | Shipping defaults and memoized environment lookup |
| `shadow_math.{h,cpp}` | Pure vector, quaternion, matrix and projection helpers |
| `shadow_gl_api.inc` | Headerless OpenGL ABI surface, constants and resolver helpers; included by the root TU |
| `shadow_engine_bindings.inc` | Engine function/global symbols and mangled-name resolver table; included by the root TU |
| `shadow_targets.inc` | Target/FBO/texture lifecycle and validation |
| `shadow_diagnostics_settings.inc` | Diagnostics, readback/dumps and persisted settings helpers |
| `apply_local_cube_runtime_settings` | Reconciles persisted `local_cube_quality` with the live capture cadence and resets its deadline; called during startup load and panel edits |
| `apply_local_map_runtime_settings` | Promotes persisted staged `local_map` into `g_localLightCaptureSize` before the first GL target allocation; runtime changes remain Apply-gated |
| `sanitize_local_shadow_map_size` | Restricts loaded local map sizes to the five supported 256..4096 presets |
| `shadow_replay.inc` | Bucket replay implementation and replay accounting |
| `shadow_shader_interposition.inc` | Shader hooks, visible-draw filtering and duplicated draws |
| `shadow_fullscreen_receiver.inc` | Fullscreen receiver program and depth-to-shadow composite |
| `shadow_overlay_runtime.inc` | Overlay runtime, input and live option application |
| `shadow_trace_cascade.inc` | Bounded tracing, cascade math/capture and validation |
| `shadow_local_lights.inc` | Engine-selected local-light capture and visible dynamic casters |
| `REFACTORING.md` | Frozen baseline, extraction policy, verification status and next seam |

The `.inc` modules intentionally remain in the root translation unit. They can
see existing internal state without exporting it or changing static
initialization/hook order. Pure modules are separate objects with hidden symbols.

## Documentation map

| File | Use |
| --- | --- |
| `CURRENT_TASK.md` | Authoritative active objective, next patch, and last action |
| `SYMBOL_INDEX.md` | This navigation/recovery map |
| `README.md` | User-facing settings, launch and implemented features |
| `SHADOWMAP_STATUS.md` | Development history and proven renderer behavior |
| `LINUX_RENDERER_MAP.md` | Reverse-engineered Linux renderer structure |
| `PS4_CASCADE_REFERENCE.md` | Console implementation reference; not runtime truth |
| `AGENTS.md` | Stable collaboration/invariant guidance |

## Known traps and rejected paths

- Do not change engine light choice: that bug is solved.
- Do not treat stencil shadow buckets as visible caster truth.
- Do not infer `render`/`shadow` by speculative object offsets when the normal
  draw stage provides direct evidence.
- Do not move regular capture before the receiver.
- Do not publish prepared matrices before all matching depth layers finish.
- Do not keep injected capture active through UI, menu, stencil, or unrelated
  scene passes.
- Do not replace original caster shaders/materials; doing so loses alpha tests,
  skinning, wind, and custom visible geometry behavior.
- Do not omit any intercepted draw variant.
- Do not broadly print `nwn_shadowmap.cpp`; it wastes context and obscures flow.
- Do not mix planned state with implemented state in this index. Mark proposals.

## Narrow inspection recipes

```bash
# Refresh one symbol location.
rg -n "capture_local_light_shadow|SceneRenderDynamicGeometry_detour" \
  nwn_shadowmap.cpp shadow_*.inc

# Local-light implementation only.
rg -n "capture_local_light_shadow|localVisibleDynamic" shadow_local_lights.inc

# Inspect the proven classifier and draw bridge only.
rg -n "local_cube_visible_dynamic_draw|DUPLICATE_LOCAL" \
  shadow_shader_interposition.inc

# Inspect frame ordering only.
rg -n "SceneRender|frame" nwn_shadowmap.cpp shadow_local_lights.inc

# List declarations without dumping function bodies.
rg -n "^(static |extern |struct |class |#define DUPLICATE_)" \
  nwn_shadowmap.cpp shadow_*.inc
```

Keep each source window under about 120 lines. If two regions are needed, open
two bounded windows instead of the span between them.

## Regular local-light visible-geometry staging (implemented 2026-08-13)

- `2052` `g_localVisibleDynamicCaptureActive` and adjacent staging state:
  prepared/completed frame ids, selected slots, six face matrices, light data,
  and per-layer visible draw counters.
- `5167` `capture_native_transform`: visible local capture is admitted alongside
  trace/cube capture so original normal-draw model transforms remain available.
- `5302` `begin_local_visible_dynamic_transform`: reconstructs model space from
  the exact normal model-view and camera inverse, then substitutes one staged
  local-light face view/projection.
- `5328` `begin_local_visible_dynamic_capture`: binds the regular local depth
  array/FBO layer. It does not create a diagnostic-only target.
- `5959` `DUPLICATE_LOCAL_CUBE_DYNAMIC`: shared normal-draw bridge. The macro now
  independently duplicates accepted visible draws into diagnostic cube targets
  and/or the regular staged local-light array while restoring complete GL state.
- `9110` `SceneRenderDynamicGeometry_detour`: arms staged capture around the
  engine's real normal dynamic pass, publishes only non-empty results, and marks
  an empty result for one legacy fallback.
- `10670` `capture_local_light_shadow`: keeps engine `GetShadowLights()` order,
  computes the ordinary local matrices, clears/stages selected array layers, and
  defers legacy bucket replay unless the visible pass produced zero draws.

Current frame lifecycle:

1. Receiver consumes the last completed regular local depth array.
2. Post-scene local capture computes/stages the engine-selected light faces and
   clears their layers without publishing them.
3. The next real normal dynamic pass duplicates visible draws into those layers
   and publishes the completed map before that frame's receiver.
4. The next post-scene call preserves the completed result; the following cycle
   stages another update. This is intentionally every-other-frame until proven.

Required log signature:

```text
[shadowmap][local-light] visible dynamic capture slots=N draws=M fallback=no ...
```

`M == 0` is not success: it activates the old bucket replay fallback and must be
diagnosed before removing that fallback.
