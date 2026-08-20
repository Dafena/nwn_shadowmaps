# Agent handoff: NWN:EE shadow-map injector

This repository is the active development tree. The repository root is the
only checkout that should be built or deployed.

Read this file, then [CURRENT_TASK.md](CURRENT_TASK.md), before editing. Use
[SYMBOL_INDEX.md](SYMBOL_INDEX.md) for narrow source inspection. Do not dump
the large root translation unit broadly.

## Current baseline

The clean baseline is the `main` branch at the latest repository commit. The
renderer is split into same-translation-unit `.inc` modules included by
`nwn_shadowmap.cpp`; only the pure configuration and math helpers are separate
translation units. Linux development and shipping builds, and the Windows
proxy build, come from this tree.

The accepted implementation currently includes:

- scene-entry camera/context capture and cascaded directional shadow targets;
- world-anchored static sun shadows plus visible dynamic/alpha bucket capture;
- fullscreen scene-depth reconstruction and directional/local composite;
- local-light contact shadows with one downward perspective layer per selected
  source;
- local layers filled from the engine's visible bucket pass, after the original
  bucket draw and under a re-entry guard;
- area sun/moon/opacity policy observation;
- persisted ImGui settings and development diagnostics;
- Linux `LD_PRELOAD` and Windows `version.dll` platform paths.

The code is experimental. Do not describe it as a complete replacement for
NWN's lighting or shadow system.

## Non-negotiable engineering rules

### Linux-first development policy

Until the injector is working on Linux, Linux is the priority target for every
new feature and fix. Do not spend implementation effort on Windows unless the
maintainer explicitly requests a Windows-specific task. Windows builds may
remain compiling when practical, but Windows runtime behaviour must not drive
shared-code design decisions.

### Platform separation

If Linux works and Windows does not, fix Windows in a Windows-only path. Do not
change shared C++ or GLSL to solve a Windows-only symptom. Use
`NWN_WIN_LOCAL_FASTPATH` for local-light mechanics and keep it separate from
`NWN_SHIP`, which controls whether a build is a tester-facing shipping build.

This rule applies to shader expressions too. A shared attenuation, bias, or
normal calculation is a shared behaviour even if the symptom was first seen on
Windows.

### Engine authority

When NWN exposes a decision, use NWN's result. Missing data is a safe no-op.
Do not replace engine choices with camera distance, visible-object guesses,
draw-order census, or dynamic-anchor ownership.

For local shadow ownership, `LightManager::GetShadowLights()` is authoritative
and its non-ambient entries are consumed in engine order. `SetLightGL` remains a
read-only census for ordinary local lighting and sun-shadow lift; it must not
select, rank, or replace local shadow sources.

The Windows `lightpriority` read is currently disabled because its field offset
has not been verified. Do not enable or extend it to Linux without a new runtime
measurement and explicit direction.

### Replay and GL state

- Any call back into `Scene::RenderDrawBucket` must be protected by the
  appropriate replay flag and `guarded_render_bucket`.
- Apply light transforms through NWN's matrix stack, flag both stacks dirty,
  restore the original matrices byte-for-byte, and restore the original matrix
  mode before drawing resumes.
- Restore every GL state touched by an injector pass: FBO, viewport, program,
  active texture and bindings, depth, blend, cull, scissor, colour/depth masks,
  polygon offset, and texture comparison state.
- Keep the receiver before post-scene local-light staging. The receiver may
  intentionally consume the last complete local generation; do not publish
  partially filled replacement metadata.
- Do not reintroduce hidden NWN geometry into the live draw stream. The full-BSP
  submission is a one-shot world-map operation only.

### Settings and artifacts

Every new user-facing setting must have all of these:

1. a live variable and environment default;
2. an ImGui control where appropriate;
3. a `settings_table()` entry;
4. a matching `settings_reset_defaults()` value;
5. a version/retune update when an existing default changes.

Use `kSettingsMax` everywhere. A bound-checked settings table can otherwise
silently drop the newest entries.

Verify the artifact that will actually be run. Development output is
`libnwn_shadowmap.so`; Linux shipping output is
`libnwn_shadowmap_deploy.so`; Windows shipping output is `win/version.dll`.
Do not confuse a source build with a copied game-directory artifact.

## Current open work

The active task is a Linux-first smooth-alpha transparency experiment. Start
with stock foliage in static alpha bucket 1, using the source-classified
`NO_DISCARD 0` + `fAlphaDiscardValue` shader family. The proven live baseline
uses source-over blending, cutoff `0.2000`, depth testing, and depth writes.
The inert accumulation shader branch is available through
`NWN_OIT_FOLIAGE_SHADER=1` and has linked successfully on Linux. The next slice
is a private late replay of bucket 1 against copied scene depth; do not suppress
the engine draw until late replay and resolve are proven.

Second-story tile flicker is deferred. With NWN's automatic second-story
hiding, the engine's visible bucket contents change at an overhang boundary.
Because the sun map follows that visible fill, the shadow strobe is expected
from the current design.

Two approaches are explicitly rejected:

1. submitting the full unculled BSP every frame, because it pollutes NWN's live
   draw stream and breaks normal rendering;
2. holding a map based on draw-count changes, because a single wall can enter or
   leave as only one draw among hundreds.

The next design is two injector-owned static generations. A fill change should
complete a new generation privately, then the receiver cross-fades from the old
generation to the new one. Do not begin by changing the engine draw list,
guessing a tile state, or keying the transition to a draw-count threshold.

Other known limitations:

- local-light projection is a single downward contact cone, not full point-light
  cube coverage;
- caster submission follows the normal camera-visible stream, so off-screen
  casters can be absent even when their shadows would be visible;
- local capture is currently single-buffered and ordered after the receiver;
- Windows priority ordering remains disabled pending field-offset evidence;
- legacy stencil suppression and the final lighting integration still require
  care before this can be called a replacement system.

## Source map

| File | Scope |
| --- | --- |
| `nwn_shadowmap.cpp` | Root state, hooks, startup, scene flow |
| `shadow_engine_bindings.inc` | Engine bindings and symbol resolution |
| `shadow_gl_api.inc` | GL declarations and resolver surface |
| `shadow_targets.inc` | FBO/texture lifecycle and target state |
| `shadow_replay.inc` | Sun/static-world/local replay implementation |
| `shadow_local_lights.inc` | Local selection, matrices, preparation, capture |
| `shadow_shader_interposition.inc` | Shader and draw interception |
| `shadow_fullscreen_receiver.inc` | Receiver programs and scene copies |
| `shadow_overlay_runtime.inc` | Overlay lifecycle and frame ordering |
| `shadow_diagnostics_settings.inc` | Settings, logging, readback, dumps |
| `shadow_trace_cascade.inc` | Trace hooks, cascade state, bucket hook |
| `shadow_config.{h,cpp}` | Environment lookup and shipping defaults |
| `shadow_math.{h,cpp}` | Pure math helpers |
| `nwn_platform.{h,cpp}` | Platform-specific mechanics |

Useful narrow searches:

```bash
rg -n "SceneRender_detour|SceneRenderDrawBucket_trace_detour" \
  nwn_shadowmap.cpp shadow_trace_cascade.inc
rg -n "replay_bucket_into_|capture_local_light_shadow" \
  shadow_replay.inc shadow_local_lights.inc
rg -n "GetShadowLights|EngineShadowLight|g_localPriorityOffset" \
  nwn_shadowmap.cpp shadow_local_lights.inc shadow_trace_cascade.inc
rg -n "draw_static_receiver|capture_scene_depth|local" \
  shadow_overlay_runtime.inc shadow_fullscreen_receiver.inc
```

## Verification

```bash
python3 check_shaders.py
make
make deploy
cd win && make
```

Runtime verification is required for renderer changes. Record the exact
launcher, platform, game build, GPU/driver, video settings, and relevant
environment overrides. Keep diagnostic changes opt-in and reversible.
