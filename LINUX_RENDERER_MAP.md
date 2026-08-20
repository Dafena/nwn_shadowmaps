# Linux renderer map

This is the current renderer-facing map for the Linux NWN:EE executable. It
records engine boundaries used by the injector, not a historical phase plan.
Exact addresses and mangled names belong in `shadow_engine_bindings.inc`; this
file explains ownership and safe timing.

## Engine boundaries used by the injector

| Boundary | Injector use |
| --- | --- |
| `Scene::Render()` | Selected gameplay-frame entry and immutable camera/context capture |
| `Scene::RenderDrawBucket(int)` | Original visible bucket draw followed by private sun/local replay |
| `Scene::RenderDynamicGeometry()` | Dynamic-stage timing and completion observation |
| `Scene::RenderSinglePass()` | Scene-order trace and normal renderer observation |
| `Scene::RenderShadows(int,bool)` | Native stencil-shadow classification/suppression |
| `LightManager::GetShadowLights(int)` | Authoritative local shadow-source list |
| `CNWCArea::UpdateShadowingLights()` | Linux area-policy observation/cross-check |
| `RenderInterface` matrix functions | Matrix-stack substitution for private depth replay |
| GLEW/OpenGL entry points | Shader interception, FBO work, and draw wrappers |

The injector does not call arbitrary exported renderer methods merely because
they exist. In particular, standalone `Scene::RenderDrawBucket()` calls are
unsafe outside the engine's active sequence; current replay is anchored to the
normal bucket hook.

## Current frame contract

At selected area-scene entry, the injector snapshots:

- scene identity and frame serial;
- viewport and camera projection/view matrices;
- camera position and rigid view inverse;
- area light direction and cascade fit state.

Later engine matrix traffic is not treated as the frame camera. Private replay
temporarily writes light matrices into NWN's matrix stack, flags both stacks
dirty, calls the original bucket, then restores the exact previous matrices and
matrix mode.

## Shadow path

The accepted directional path is:

```text
Scene::Render entry
  capture area context and fit cascade state
  NWN renders its normal visible buckets
    original bucket draw
    private sun cascade replay (when enabled)
    private local-light replay (when prepared)
  receiver copies completed scene buffers and composites shadows
  overlay renders last
  post-scene local-light setup prepares the next generation
Scene::Render exit
```

The exact order matters. The receiver must not sample a target while its
generation is being cleared or refilled, and local capture must not run before
scene-depth capture.

## Engine authority

`GetShadowLights()` returns the selected non-ambient local sources. The injector
retains that order and applies only its configured source budget. It does not
replace the list with camera distance, render order, or a census of visible
objects. `SetLightGL` data is read-only census information for normal light
lift, never local-shadow ownership.

The Windows build has an optional priority-ordering experiment, but its field
offset is currently disabled. This Linux map must not be changed to mirror that
unverified Windows path.

## Legacy stencil shadows

NWN's `Scene::RenderShadows`, `RenderStaticShadows`,
`RenderDynamicShadows`, and shadow-plane programs generate stencil-volume or
blob-shadow geometry. The injector may observe those passes to classify them
and can suppress the engine pass in its configured path. Those generated
volumes are not reusable local-light caster meshes.

## Safe inspection

Use the source map in [SYMBOL_INDEX.md](SYMBOL_INDEX.md) and inspect bounded
windows around symbols:

```bash
rg -n "SceneRender_detour|SceneRenderDrawBucket_trace_detour" \
  nwn_shadowmap.cpp shadow_trace_cascade.inc
rg -n "LightGetShadowLights|GetShadowLights|UpdateShadowingLights" \
  nwn_shadowmap.cpp shadow_engine_bindings.inc shadow_trace_cascade.inc
rg -n "mtx_entry|SetMatrixMode|FlagDirty|savedView|savedProj" \
  nwn_shadowmap.cpp shadow_replay.inc shadow_shader_interposition.inc
```

## What this map does not claim

It does not claim that every engine export is safe to call, that the current
visible bucket stream contains all possible shadow casters, or that local
contact cones provide full point-light coverage. Those are known limitations,
not missing documentation.
