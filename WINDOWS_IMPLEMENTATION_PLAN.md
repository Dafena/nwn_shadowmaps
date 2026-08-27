# Windows implementation plan

Updated 2026-08-26. The maintainer explicitly authorized Windows work after
the accepted Linux baseline. Linux remains the behavioural reference, while a
native Windows run is the final authority for Windows correctness. Proton is a
useful installation and smoke test, not a substitute for native-driver proof.

Mode 3 weighted OIT is parked and is not part of this plan.

## Existing foundation

The repository already builds a shipping `win/version.dll` from the shared
renderer. The proxy forwards all 17 system `version.dll` exports, resolves the
game's exported MSVC symbols, intercepts OpenGL 1.1 through IAT patches and
modern OpenGL through `wglGetProcAddress`, delays GL binding until a context is
current, and provides Windows-specific overlay input and local-light mechanics.

Material identity transport is now present on Windows. Exact MSVC exports and
runtime tests confirm material creation, shared-MTR parsing, standard-texture
binding/name lookup, and shared-material initialization. The exported
destructors exist but their Subhook trampolines are unsafe on this executable,
so Windows refuses them and resets reused shared identities at
`SharedMaterial::Init`. Visible Mode 2 parity is complete and strict material
routing is now a Windows shipping default; `NWN_ALPHA_MODE_ROUTING=0` remains
an explicit troubleshooting opt-out. The validated material hook pipeline also
defaults to stage 4/4; lower `NWN_WIN_MATERIAL_HOOK_STAGE` values are diagnostic
downgrades rather than shipping requirements.

## Checkpoints

### 1. Freeze and inventory the Windows baseline

- Build `win/version.dll` and verify all 17 proxy exports.
- Record the exact NWN executable version, DLL hash, GPU/driver, resolution,
  MSAA setting, and install path.
- Launch with `NWN_SHADOWMAP_LOG=1` and inventory resolved engine symbols,
  IAT hooks, `wglGetProcAddress` wrappers, deferred GL binding, and startup
  warnings.
- Confirm that removing the DLL restores an untouched native baseline.

Exit criterion: the game starts and exits cleanly, the proxy forwards every
required export, the expected hooks install once, and the log belongs to the
tested artifact.

### 2. Establish directional-shadow parity

- Test directional CSM before enabling transparency work or changing local
  lights.
- Compare the matched Linux scene for cascade coverage, camera motion,
  alpha-aware casters, static/dynamic geometry, day/night policy, fog, water,
  UI, pause, menus, and area transitions.
- Verify the Windows `AurEnableShadowing`/`AurDisableShadowing`/
  `AurSetDynamicProjectionLight` observation path. Do not invent a replacement
  for unavailable `CNWCArea` fields or `ShadowOpacity`.
- Confirm native stencil-shadow suppression and Creature Shadow Detail behavior.

Exit criterion: directional shadows are stable in the regression scenes with
no Linux change and no Windows-only visual corruption.

### 3. Establish local-light parity

- Validate `LightManager::GetShadowLights()` resolution and engine-order source
  ownership.
- Exercise the existing `NWN_WIN_LOCAL_FASTPATH`: per-light caster culling,
  tracked current program, and publication when any non-empty layer completes.
- Test moving characters, static geometry, alpha casters, multiple lamps, area
  transitions, and incomplete-generation retention.
- Keep `lightpriority` disabled until a native runtime measurement proves its
  Windows field offset.

Exit criterion: selected local lights cast stable shadows without blanking the
directional receiver or publishing partial/stale generations.

### 4. Verify the Windows product surface

Status: **confirmed in game on 2026-08-24.** Settings are anchored beside the
exact `nwmain.exe`, stale hidden controls cannot override shipping behavior,
the version-7 migration preserves the confirmed Windows lift default, logging
is opt-in, and Win32 panel input/persistence were verified by the maintainer.

- Check shipping defaults, the `Ctrl+Shift+F11` panel, Win32 input capture,
  settings persistence beside the executable, and opt-in logging.
- Confirm that hidden diagnostic controls cannot be restored from stale saved
  settings and silently disable shadows.
- Record native FPS/GPU cost in the matched control areas before material
  routing is introduced.

Exit criterion: a tester can install, configure, diagnose, and remove the DLL
without a launcher or hidden state.

### 5. Port material identity transport for Mode 2

Status: **complete; transport and safe lifecycle hooks confirmed in game on
2026-08-26.**
The exact v89.8193.37-17 creation/parse/bind/texture/init exports are mapped,
artifact-verified, and returned safely during an area load. A staged test
proved that the `Material` destructor trampoline crashes after registry cleanup;
both Windows destructor hooks are therefore permanently refused. Stage 4 now
uses `SharedMaterial::Init` as the safe pointer-reuse reset boundary and passed.
The final native-area-first transition census correlated
`tcm02_leaves04`/`_1`/`_2` with Mode 0/2/3 respectively while retaining the
same base texture identity, proving the Windows shared-to-concrete association.

- Identify and verify the Windows MSVC exports/signatures for Material shared
  initialization, `SharedMaterial::ParseField`, standard-texture binding,
  texture/name access, and shared initialization. Destructor exports may be
  inventoried but must not be hooked on this executable.
- Add those bindings to the Windows symbol map. Missing or unsafe hooks must
  fail closed to native rendering.
- Implement Windows-only safe detours/trampolines; do not use Linux's
  remove/call/reinstall fallback on a potentially threaded Windows path.
- Reuse the accepted hashed concrete/shared registry and late-field refresh.
  On Windows, overwrite concrete pointer reuse at material creation and reset
  shared pointer reuse at `SharedMaterial::Init`; Linux retains destructor-based
  slot recycling.
- Run a read-only census with unmarked, Mode 2, framebuffer-sampling, and
  volumetric materials. No pixels may change during this checkpoint.

Exit criterion: logs correlate each live draw with the correct stable material
identity and MTR fields across direct loads and area transitions, construction-
time pointer reuse does not retain stale fields, and native-area performance is
unchanged. Windows destructor hooks are not part of this criterion.

### 6. Enable Windows Mode 2 A2C

Status: **complete; confirmed in game on 2026-08-26.** Mode 2 alone selected
A2C once the live framebuffer reported four samples. Mode 0 stayed native and
parked Mode 3 explicitly failed closed to native. The log also confirmed the
direct directional and local A2C receiver uniforms were available.

- Route only explicit `NWN_ALPHA_MODE 2` ordinary alpha materials.
- Require at least two live MSAA samples; otherwise render natively.
- Preserve native rendering for unmarked/unknown modes, Mode 3,
  `sample_framebuffer 1/2`, volumetric, water, emitters, UI, shadow passes,
  replays, and injector-owned draws.
- Verify exact restoration of blend, depth, cull, multisample, shader uniform,
  texture, framebuffer, viewport, and mask state.
- Support the explicit custom continuous-alpha marker only after stock-shader
  Mode 2 passes.

Exit criterion: Mode 2 changes only marked materials, Mode 3 stays native, and
0x/2x/4x/8x MSAA behavior matches the accepted Linux contract.

### 7. Integrate A2C shadows and accepted emitter behavior

Status: **complete; confirmed in game on 2026-08-26.** Mode 2 receives stable
directional and local shadows, opaque geometry intersects it correctly, and
the accepted multi-layer emitter limitation matches Linux. The Windows
world-camera inverse removed whole-surface A2C flicker. Ordinary-light sun
lifting now follows the complete engine light budget rather than a stale
injector cap of eight, so carried lights remain effective across camera zoom
and movement. Local-shadow refresh is a persisted Low (25 ms), Medium (16 ms),
or Ultra (every rendered frame) option; Ultra was accepted in game.

- Verify directional and local shadow reception per covered sample.
- Preserve alpha-aware shadow casting from Mode 2 materials.
- Test opaque characters/static meshes intersecting foliage and one/several
  overlapping foliage layers in front of torch/fire particles.
- Treat discrete dark-region masks and reduced emitter visibility through many
  A2C layers as the documented Mode 2 limitation. Do not revive OIT or make all
  emitters depthless as a Windows workaround.

Exit criterion: Windows matches the accepted Linux Mode 2 compromise without
regressing native materials, shadows, fog, water, or UI.

### 8. Regression, performance, and release gate

Status: **next.** Begin from the confirmed Windows DLL savepoint produced after
checkpoint 7; do not reopen Mode 3 OIT during this gate.

- Run the same native, shadow-heavy, local-light, foliage, custom-hair,
  framebuffer, volumetric, water/fog, UI, and area-transition scenes on Linux
  and native Windows.
- Compare shadows-only, routing-only, and Mode 2 costs separately.
- Test long sessions and repeated area transitions to exercise registry
  recycling and resource reuse.
- Rebuild from clean sources, verify the DLL hash/exports, and retest the exact
  copied artifact.

Exit criterion: no Linux regression, no native-area routing tax, no stale
material identities, and no unresolved Windows warnings relevant to enabled
features. Create a savepoint before any wider tester release.

## Working cadence

Stop for maintainer runtime confirmation after each checkpoint. Do not combine
symbol discovery, shadow parity, and visible Mode 2 routing into one test build;
each checkpoint must have one observable question and a native fallback.
