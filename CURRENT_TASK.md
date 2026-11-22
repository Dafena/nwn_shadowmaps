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

Layer scheduling now has a Windows-specific first-item adapter and a verified
virtual Part draw-slot adapter, gated on complete safe installation. Linux scheduling is
the behavioral reference. Scene-local visit generations and bounded scene
recycling prevent GUI renders or repeated area changes from losing discovery.

## Validation limits and next work

- Linux development, shipping and Windows builds must all stay current.
- DLL exports/symbol checks are not runtime validation.
- Native Windows verification remains explicitly pending by maintainer choice.
  Proton checks are valuable Windows-path smoke/visual checks, not native-driver
  certification. Do not present earlier Windows weather acceptance as proof of
  the new layer behavior.
- Linux matched layer/torch views and shipping clear/rain/snow/interior
  transitions passed. Final Windows DLL layer/torch checks passed under Proton.
  Proton rain/snow startup fixtures stayed clear and did not exercise those
  conditions; integrated Windows weather acceptance is still pending.
- Type-0 frame-time comparisons were presentation-capped near60Hz; they do not
  establish uncapped throughput. Timing-overlay startup failures are excluded.
- Final crowded multiplayer15actor snow-trail acceptance and high-budget rain
  acceptance remain separate limitations; see WEATHER_PARTICLES.md and
  WINDOWS_IMPLEMENTATION_PLAN.md.
- Second-story tile shadow strobes remain deferred. Do not reintroduce full
  unculled BSP every frame or draw-count-based generation guesses.

Useful findings from the previous session are preserved in source history and
local audit evidence, rather than carried as hundreds of stale task updates.

## Linux performance follow-up (2026-11-17)

The root working tree now includes validated Linux draw-buffer selection leasing and an inactive-weather upload fast path. Checkpoint12757b0 protects the first accepted improvements; ecfe847 protects the diagnostic lease candidate. New integration work is not yet checkpointed. Normal development/shipping libraries compile and regression/shader checks pass; normal Linux runtime matches preview (~47FPS ChapterOne test2,100+FPS rainyBalamb). Shipping runtime was not repeated. Details, artifact hashes and remaining overhead: docs/TAG_WORK_PROFILING.md. Exact historical comparison against032424a/b47f980 and tagging isolation: docs/032424A_PERFORMANCE_COMPARISON.md.

Normal per-draw CPU samplers/benchmark swap hooks compile out. Linux installs selection-boundary hooks only with verified coverage, otherwise retains per-draw selection. Windows behavior remains unchanged. Current launch-linux.sh is pre-existing and remains untracked/untouched. Root library now contains the normal integration, not the earlier12757b0 historical artifact. Original game INI/resources are unchanged by isolated tests.


## Performance investigation paused (2026-11-18)

Maintainer paused testing/implementation after shared sun/local caster-work planning. Resume handoff and pending final-plan confirmation are in docs/INJECTOR_OVERHEAD_PLAN.md, section “Paused by maintainer”. Settled decisions: compatible subset sharing is acceptable; prove a bounded reversible experiment before a larger redesign. No shared-geometry prototype implemented. Local receivingOFF demand gating saved3.5–5ms experimentally; receivingON resumes shadows correctly and their cost. Attachment reuse rejected; shader replay simplification did not establish a useful gain. Normal library unchanged, game closed after GPU capture analysis. Preserve dirty working tree; no new commit/push requested.


## Bounded caster-work investigation resumed (2026-11-20)

Maintainer resumed investigation. Diagnostic-only one-frame caster input inventory built; collection pending. See docs/INJECTOR_OVERHEAD_PLAN.md latest section. No shared geometry renderer or optimization implemented; normal artifact unchanged. Preserve previous compatible-subset/1ms/test2/experimental acceptance and larger-redesign review boundary.


## Local-only sharing gate disposition (2026-11-20)

Read-only candidate-headroom measurement complete. Initial rigid opaque
non-creature subset has only~0.87ms later-local-light mesh-body time, including
~0.25ms driver draws; remainder~0.62ms before new sharing overhead. Decline that
prototype against the>=1ms completed-frame target. No shared renderer built.
See SHARED_CASTER_FEASIBILITY.md and SHARED_CASTER_DESIGN.md for evidence and
limits. Cross-sun/local reuse or another bottleneck would be a new scope; no
gain demonstrated there. Normal library unchanged. Diagnostic game remains
open with probes stopped; no new commit/push.


## Active: focused added-overhead audit (2026-11-20)

Maintainer set geometry sharing aside and approved audit of tagging/classification/
draw hooks with>=0.6ms repeatable completed-frame acceptance, unchanged visuals,
no worse stutter. See docs/INJECTOR_OVERHEAD_AUDIT.md. Strong candidate: replace
per-item remove/call/reinstall fallback for two trampoline-less Linux hooks with
exact-prefix verified native gateways, diagnostic-only. Tests/build/syntax pass;
matched runtime pending. Earlier mesh timings include that hook fallback and do
not bound a hook-mechanics optimization. No shared renderer or normal promotion.
Preserve dirty tree, original launcher/settings and existing normal artifact.


## Audit candidate validated in test2 (2026-11-20)

Exact-prefix Linux diagnostic gateways for trampoline-less PartTriMesh::Draw
and GetNextItem avoid per-item code unpatch/repatch. Matched actual-frame savings
1.28295/1.2758ms, identical3959draws/world, improvedp95,0lease gaps. Maintainer's
continuous test2 camera/highlight/shadow/foliage/transparency check passed.
Candidate remains experimental; broader checks and normal integration pending.
Normal library SHA45e060b5...ecb1314f unchanged, no new commit/push. See
INJECTOR_OVERHEAD_AUDIT.md for measurements and exact validation limits.


## Native gateway normal integration built (2026-11-20)

Maintainer's broader Balamb preview reported~150FPS and no visual problems so far.
Verified native gateways now enabled in normal Linux x86-64 with exact-prefix/
callee fallback; diagnostic toggles/counters compile out normally. Module is
shadow_native_gateways.inc. Only this validated fix promoted; local-demand
experiment remains diagnostic-only. Normal/shipping builds, gateway regression,
shader/diff checks pass. Normal SHA03b0794f...3d3aab; shippingd51c5d09...8c4f63.
Clean normal runtime prepared under/tmp/nwn-audit-normal; runtime check pending,
shipping runtime not repeated. No original settings/launcher changes or commit.


## Normal integration regression: NOT accepted (2026-11-20)

Maintainer reported detached local character shadow and lower loading-view FPS
in Balamb on normal build03b0794f... . Files match preview settings, normal mapped
library/flags verified. Broader preview and test2 gateway gains remain evidence;
normal runtime acceptance FAILED. Investigating differences before fixes/quality
tuning. Preview also enables diagnostic local-demand/area reset, normal doesn't;
this is a lead, not proven cause. Isolation diagnostic mode5=gatewayON/lifecycleOFF,
mode4=bothON, mode2=bothOFF, lease/weatherON throughout. Launch/settings under
/tmp/nwn-audit-isolation. Current gamePID79501normal remains open. Normal and
shipping artifacts are unaccepted pending triage; no shipping runtime claim.


## Regression triage update (2026-11-20)

Detached local-light shadow unchanged in diagnostic modes5(gatewayON/lifecycleOFF),
4(bothON),3(gatewayOFF/lifecycleON): not implicated by either toggle. Keep separate
existing shadow issue. Settled Balamb gateway A/B/A passed with positive0.37925/
0.3434ms savings (~148→156FPS),1016draws/world unchanged,0gaps; test2 gain remains
1.28ms. Loading-counter320→220report not reproduced by gateway toggle. This is
still diagnostic-session evidence, not completed normal-vs-preview integration
acceptance. Preview4restored, normal concern remains pending controlled isolation.

## Daytime fog brightness correction (2026-11-20)

Maintainer prioritized daytime volumetric whiteout over local-light profiling.
Combined-scatter shoulder plus destination-headroom LDR blend implemented in
 godrays_runtime.inc; no settings/default changes or added rendering pass.
Numeric composition regression fails old/pass new; shader checks and builds
pass. Actual normal Linux NVIDIA runtime daylight test2 default camera shows
improved contrast and retained haze. Exact user-adjusted low-angle/night visual
acceptance remains pending; do not claim those verified. See
 docs/FOG_BRIGHTNESS_NORMALIZATION.md. Temporary fog marker removed. Private
GPU display enabled unattended save loading; original resources/settings intact.
No successful adjusted-camera local-shadow timing yet; resume after visual check.

## Fog lamp fading and torch startup (2026-11-20)

Maintainer requested glow0.2 and smooth fog-light appearance. Default/reset0.2,
settingsv20 selective v19 migration verified. Fog-only eight-slot identity fade
(0.5s in /0.35s out), delayed new-light admission, area/pause/off reset; radiance
weighted before shared shoulder. Engine lighting/shadow ownership unchanged.
Normal Linux test2 torch visibly equipped via inventory radial menu;100 snapshots
show gradual rise and no sampled screen-wide flash. Unit transition regression,
shader/energy checks pass. See FOG_BRIGHTNESS_NORMALIZATION.md for limits.

## Fog follows engine ordinary-light limit (2026-11-20)

Requested hardcoded fog eight-light cap removed. Live lamp_upload_max() drives
selection; CPU/history/GLSL capacity128, separate from local shadow budget.
Glow0.2/fades preserved, reducing limit smoothly retires excess sources. Tests
cover32→4 and full128; shaders/energy and Linux development/shipping builds pass.
Actual normal NVIDIA test2 runtime: engine limit128, selected22 fog candidates,
full-resolution fog rendered successfully. No matched FPS gain/cost claim.

## Fog resolution detail-loss correction (2026-11-20)

Maintainer reported distant/transparent detail erased at Quarter. Old upsample
normalized background-only samples onto unmatched foreground. Depth rejection
plus same-shader exact-depth selective refinement added for Quarter/Half; Full
now skips neighbor interpolation, retaining depth-restricted noise filtering. Unit27 added with complete restore.
Depth regression red old/green new; shaders/energy/dev+shipping builds pass.
Actual normal GPU Quarter480x252, Half960x503 and Full1920x1006 rendered; exact rainy camera
and matched performance remain pending. See FOG_DEPTH_RECONSTRUCTION.md.

## Respect authored fog opacity distance (2026-11-21)

Maintainer showed silhouettes beyond native fog's opaque end. Shared fog marcher
previously used hidden surface distance/marchFar135 despite native fog end45.
Now nwnFogMarchEnd clamps at authored view-depth end converted to radial metres;
low-res/refinement and lamp chord use same cutoff. No authored fog leaves extra
atmosphere available. Actual-expression regression passes frontal/oblique rays,
foreground and no-authored-fog cases. Shader/build checks pass. Exact supplied
camera visual acceptance remains pending; do not claim all silhouettes solved.

## Maintainer checkpoint and Windows artifact (2026-11-21)

Maintainer reports the current authored-distance fog result looks good and asks
for a checkpoint plus Windows DLL. Preserve current Linux rendering/performance
work, opt-in diagnostic investigations and evidence together; no geometry-sharing
renderer was implemented. Exact screenshot acceptance is now maintainer-confirmed
for the current fog result, not blanket acceptance of all areas/platforms.

Windows cross-build from root tree succeeded without warnings. win/version.dll
SHA256 fb7a4e6ad2446c92630f1366758e14be1d7746d70db690253168a300e8c5c472.
Artifact verification: x86-64 PE32+;17/17 proxy exports;60/60 mapped exports against
installed nwmain.exe89.8193.37-17. No Windows runtime test or deployment claimed.
Added fog/gateway header dependencies to Windows make rules to avoid stale DLLs.
Fog math/depth/fade regressions, shader validation and diff checks pass.
User's pre-existing untracked launch-linux.sh is preserved outside the checkpoint;
Python bytecode caches and generated DLL/build output are not source checkpoint
content. No remote push requested.

## PR preparation and maintainer confirmation (2026-11-21)

Maintainer confirms the delivered build worked and requests cleanup plus a PR
for merge. This supersedes the prior runtime-unconfirmed delivery status; no
additional Windows scene/GPU details were supplied. Preserve evidence and
opt-in diagnostics, remove generated Python cache, ignore future caches and the
diagnostic build directory, and fix the remaining census whitespace. Keep the
local machine-specific launch-linux.sh outside version control without deleting
it; retain compiled Linux/Windows artifacts for testing.

Final gate: Linux development/shipping/diagnostic builds, shader compilation,
fog depth/energy/opacity/fade, native gateway, buffer lease, local demand,
inactive-weather and real weather-surface software-GL regressions passed.
Windows rebuild and 17 proxy/60 game-export verification passed. PR targets main;
no direct merge is requested or performed during preparation.

## Repository evidence cleanup (2026-11-21)

After PR #6 merged, maintainer requested further folder cleanup. Historical
transparency census reports now live under docs/evidence/transparency with an
index; all 18 files retain their exact contents. Transparency documentation links
follow the move, and fresh capture examples write to /tmp to protect the archive.
Old local test libraries and runtime logs/dumps were moved to ignored
local-archive/2026-11-21; root intermediate object files were removed and can be
rebuilt normally. Current Linux libraries, Windows DLL, settings and the local
launcher were preserved. No renderer or build behavior changed.
