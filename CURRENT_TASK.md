# Current task checkpoint

Updated 2026-08-20. A Linux-first foliage transparency experiment is now in
progress. The source/program registry and inert shader branch remain opt-in at
the current checkpoint. No engine draw is redirected or suppressed yet.

## Active task: foliage-first smooth-alpha OIT

The goal is to prove the console-derived weighted transparency path on stock
foliage before applying it to character hair. The target look is smooth alpha
blending with only near-zero texels discarded. It is not temporal dithering or
alpha-to-coverage.

The first implementation milestone is `NWN_OIT_FOLIAGE_CENSUS=1`. It:

1. recognizes the stock `NO_DISCARD 0` + `fAlphaDiscardValue` fragment-source
   signature;
2. keeps shader/program IDs process-local and joins them at draw time;
3. reports the live bucket, blend factors, alpha cutoff, depth test/write, and
   cull state once per bucket/program pair;
4. does not allocate OIT targets, redirect or suppress draws, or change the
   visible frame.

Run the first Linux evidence pass with:

```bash
env -u NWN_OIT NWN_OIT_FOLIAGE_CENSUS=1 ./run-dev.sh
rg -n '\[oit\]\[foliage' shadowmap-phase1.log
```

The first Linux evidence pass completed on 2026-08-20 (RTX 3060 Ti, native
Linux client, 1920x1006 viewport). It established:

- stock alpha/card programs are source-over blended;
- eligible draws appeared in buckets 1 and 3;
- `fAlphaDiscardValue` was `0.2000`;
- depth testing and depth writes were both enabled;
- culling was enabled.

The reusable process-local program registry now joins those programs to the
source-classified fragment once and caches the cutoff and OIT-pass uniform
locations. Shader object IDs remain runtime-only and are never treated as
stable identifiers. The standalone census path also captures NWN's draw entry
points during GL resolution; it no longer depends on the heavyweight shadow
trace launcher to observe geometry.

The same run exposed a stale `settings_tick()` cache bound: the settings table
had grown to `kSettingsMax` while its comparison cache remained 32 entries.
That overflow caused the startup crash and is fixed by sizing both from
`kSettingsMax`.

`NWN_OIT_FOLIAGE_SHADER=1` now injects an inert MRT branch into this exact stock
shader family. `nwnOitPass=0` preserves the stock cutoff and output. A future
late replay can set it to 1 to use a `0.001` cutoff and emit the console-derived
combined/sum/translucence values. A Linux runtime pass proved the shaders link
and expose live `nwnOitPass` locations while the ordinary draw still reports
cutoff `0.2000`.

Next: create a private late replay of static alpha bucket 1, copying the final
opaque scene depth into the accumulation FBO and leaving the screen untouched.
The first visible test must remain restricted to bucket 1 so character hair
and other dynamic alpha cards are not silently pulled into the foliage trial.

## Current implementation checkpoint

The local-light fill path that older notes called “planned” is already in the
source. `shadow_trace_cascade.inc` calls
`replay_bucket_into_local_light_layers()` immediately after NWN's original
`Scene::RenderDrawBucket()` call. `shadow_replay.inc` installs each selected
light's view/projection into NWN's matrix stack, renders the bucket through the
re-entry guard, restores the matrices byte-for-byte, and restores GL state.

The active local path is therefore:

1. NWN exposes the selected local shadow-light list.
2. The injector copies that list in engine order, subject to the configured
   source budget and the native three-source ceiling.
3. Post-scene setup prepares one downward perspective layer per source and a
   spare layer for a fading source.
4. The normal visible bucket pass fills those layers immediately after each
   original bucket draw.
5. The fullscreen receiver samples the last coherent local generation. Local
   capture remains after the receiver because moving it earlier broke scene-depth
   capture.

Do not re-open the old standalone bucket-loop, warm-up, emitter-tier, or
six-face cube-probe designs. They were removed or superseded.

## Deferred shadow issue: second-story tile flicker

NWN's “Hide second story tiles = Auto” setting removes and restores tiles from
the per-frame visible draw list as the player walks under them. The shadow map
follows that same list, so static shadows can strobe at the visibility
boundary.

Two approaches were tried and rejected:

- full-BSP submission every frame: the unculled geometry was fed into NWN's
  live buckets and damaged normal rendering;
- a hold-last-map heuristic based on draw counts: a whole wall can be one draw
  among hundreds, so draw-count variation is not a reliable fill transition.

### Deferred design

Use two injector-owned static layer sets. When the current static fill changes,
complete the replacement privately. Keep the old generation published until the
new one is complete, then cross-fade the receiver from old to new. The trigger
must come from the static fill/generation itself, not an inferred tile state or
a draw-count threshold. The engine's draw stream must remain untouched.

The first implementation should be opt-in and diagnostic-friendly:

1. identify the existing static target ownership and freshness boundaries;
2. add a second static generation without changing receiver output;
3. prove both generations are complete and world-aligned;
4. add a short composite-only transition;
5. test under an overhang, then test area changes, camera motion, alpha foliage,
   menus, and local-light shadows.

## Other unresolved items

- Local lights are contact shadows from one downward cone per light, not full
  point-light cube coverage.
- Normal camera culling limits the caster set for both sun and local captures.
- Windows' `lightpriority` field offset is still unverified; its sort is
  disabled. Do not apply that experiment to Linux.
- Legacy stencil suppression, area-policy transitions, and the final lighting
  integration need runtime coverage across both platforms before shipping
  changes are treated as complete.

## Working rules for the next change

- Read `AGENTS.md` before editing.
- Linux is the priority target for every feature until the injector is working
  there. Work on Windows only when the maintainer explicitly requests a
  Windows-specific task.
- Use the source, not an old status section, as the implementation authority.
- Keep Linux and Windows changes separate when behaviour differs.
- Preserve engine authority: `GetShadowLights()` selects local sources.
- Preserve replay guards, matrix-stack save/restore, and complete GL-state
  restoration.
- Never publish a partially filled target generation.
- Add or update the ImGui control, settings table, reset default, and version
  when adding a persisted setting.
- Run `python3 check_shaders.py` after shader-string changes.
- Build the relevant artifact and record runtime evidence for renderer changes.

## Useful commands

```bash
git status --short --branch
rg -n "replay_bucket_into_|capture_local_light_shadow" \
  shadow_replay.inc shadow_local_lights.inc shadow_trace_cascade.inc
rg -n "staticWorld|g_cascadeStatic|FramebufferTextureLayer" \
  nwn_shadowmap.cpp shadow_*.inc
python3 check_shaders.py
make
```
