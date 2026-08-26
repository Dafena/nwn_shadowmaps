# Source navigation index

This index intentionally names symbols and files rather than recording line
numbers. Line-number indexes became stale whenever the renderer was split into
modules. Use `rg -n` to locate the current definition.

## Scene and frame flow

| Symbol | File | Purpose |
| --- | --- | --- |
| `SceneRender_detour` | `nwn_shadowmap.cpp` | Top-level scene entry, receiver, overlay, and post-scene work |
| `trace_scene_enter` / `trace_scene_exit` | `shadow_trace_cascade.inc` | Selected-area frame context and lifecycle |
| `SceneRenderDrawBucket_trace_detour` | `shadow_trace_cascade.inc` | Original bucket draw followed by sun/local replay |
| `SceneRenderDynamicGeometry_detour` | `shadow_trace_cascade.inc` | Dynamic-stage observation and local completion bookkeeping |
| `SceneRenderShadows_detour` | `nwn_shadowmap.cpp` | Native stencil-shadow observation/suppression |
| `LightGetShadowLights_trace_detour` | `shadow_trace_cascade.inc` | Captures NWN's authoritative local source list |

## Targets and replay

| Symbol | File | Purpose |
| --- | --- | --- |
| `create_cascade_targets` | `shadow_targets.inc` | Static/dynamic cascade arrays and FBOs |
| `create_static_world_target` | `shadow_replay.inc` | World-anchored static depth target |
| `create_local_light_target` | `shadow_local_lights.inc` | Per-source local depth array and FBO |
| `replay_bucket_into_cascade_layers` | `shadow_replay.inc` | Sun cascade bucket replay |
| `render_static_world_map` | `shadow_replay.inc` | One-shot/static-cache world fill |
| `replay_bucket_into_local_light_layers` | `shadow_replay.inc` | Local bucket replay after NWN's original bucket |
| `guarded_render_bucket` | `shadow_replay.inc` | Re-entry/fault-guarded call to NWN's bucket renderer |
| `SavedState`, `bind_target`, `restore_state` | `shadow_replay.inc` | GL state preservation around private passes |

## Local-light state

| Symbol | File | Purpose |
| --- | --- | --- |
| `capture_local_light_shadow` | `shadow_local_lights.inc` | Prepares selected sources and schedules local capture state |
| `g_engineShadowLights` | `nwn_shadowmap.cpp` / trace module | NWN-selected source records in engine order |
| `g_localCubeSourceBudget` | `nwn_shadowmap.cpp` | Configured local source budget; “cube” is historical naming |
| `g_localLightFaceVP` | `nwn_shadowmap.cpp` | Per-slot downward perspective matrices |
| `g_localLightSlotCount` | `nwn_shadowmap.cpp` | Receiver-ready local slot count |
| `g_localLightPassActive` | `nwn_shadowmap.cpp` | Prevents local replay recursion |
| `local_layer_accepts_caster` | `shadow_shader_interposition.inc` | Windows-only optional local caster cull |
| `local_visible_begin_draw` | `shadow_shader_interposition.inc` | Visible local draw admission and transform setup |

The source-selection rule is simple: `GetShadowLights()` decides which lights
are eligible and in what order. The `SetLightGL` census is for ordinary lighting
and sun-shadow lift only. The Windows priority experiment is disabled until its
field offset is verified.

## Receiver and shaders

| Symbol | File | Purpose |
| --- | --- | --- |
| `draw_static_receiver` | `shadow_overlay_runtime.inc` | Main fullscreen composite and receiver gating |
| `build_static_receiver_program` | `shadow_fullscreen_receiver.inc` | Builds the current receiver GLSL |
| `capture_scene_depth` / `capture_scene_color` | `shadow_fullscreen_receiver.inc` | Copies completed scene buffers for reconstruction/composite |
| `nwnLayerLit` | receiver GLSL string | Directional cascade visibility test |
| `nwnLocalShadeCov` | receiver GLSL string | Local contact-shadow visibility and coverage |
| `my_shader_source` | `shadow_shader_interposition.inc` | Runtime shader-source interception/injection |
| `my_uniform_matrix4fv` | `shadow_shader_interposition.inc` | Matrix upload observation/bridge |
| draw wrappers | `shadow_shader_interposition.inc` | GL draw interception and pass classification |

## Configuration and platform

| Symbol | File | Purpose |
| --- | --- | --- |
| `settings_table` / `settings_reset_defaults` | `shadow_diagnostics_settings.inc` | Persisted settings registry and reset values |
| `apply_resolution_change` | `shadow_replay.inc` | GL-context-safe staged target reallocation |
| `shadow_getenv` | `shadow_config.cpp` | Memoized environment/default lookup |
| `NWN_SHIP` | `nwn_platform.h` | Shipping policy, independent of platform mechanics |
| `NWN_WIN_LOCAL_FASTPATH` | `nwn_platform.h` | Windows-only local-light mechanics |
| `nwn_overlay_render` | `nwn_overlay_imgui.cpp` | Dear ImGui panel rendering |

## Transparency experiment

| Symbol | File | Purpose |
| --- | --- | --- |
| `read_settings` | `nwn_oit.cpp` | Reads opt-in transparency experiment switches |
| `census_observe_draw` / `census_observe_draw_after` | `nwn_oit.cpp` | Reports live program, blend, depth, and cull state |
| `foliage_program` | `nwn_oit.cpp` | Joins a live program to the source-classified foliage registry |
| `nwn_oit_needs_shader_sources` | `nwn_oit.cpp` | Requests shader-source observation only when required |
| `nwn_oit_needs_draw_observer` | `nwn_oit.cpp` | Gates per-draw observation |
| `nwn_oit_bucket_begin` / `nwn_oit_bucket_complete` | `nwn_oit.cpp` | Experimental bucket lifecycle |
| `nwn_oit_prepare` / `nwn_oit_frame` | `nwn_oit.cpp` | Experimental target/frame lifecycle |
| `private_foliage_replay` | `nwn_oit.cpp` | Earlier weighted-OIT replay; not the accepted A2C baseline |

Program and shader object IDs are process-local. Material-mode ownership and
the verified Linux bucket census are documented in
[TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md).

## Narrow inspection recipes

```bash
rg -n "SceneRender_detour|SceneRenderDrawBucket_trace_detour" \
  nwn_shadowmap.cpp shadow_trace_cascade.inc
rg -n "replay_bucket_into_|render_static_world_map|guarded_render_bucket" \
  shadow_replay.inc
rg -n "capture_local_light_shadow|g_localLight|g_engineShadow" \
  shadow_local_lights.inc nwn_shadowmap.cpp shadow_trace_cascade.inc
rg -n "draw_static_receiver|build_static_receiver_program|nwnLocalShadeCov" \
  shadow_overlay_runtime.inc shadow_fullscreen_receiver.inc
rg -n "settings_table|settings_reset_defaults|kSettingsMax" \
  shadow_diagnostics_settings.inc
rg -n "nwn_oit_needs_|nwn_oit_bucket_|census_observe_draw" nwn_oit.cpp
```

If a symbol moves, update this file's ownership table only; do not add brittle
line numbers back.
