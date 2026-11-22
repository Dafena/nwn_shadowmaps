# Injector overhead optimization — interview and proposed plan

2026-11-17. Planning only; renderer implementation and runtime tests are paused during the interview. This document supersedes the narrower next-tagging investigation as the active planning scope, without replacing prior measurements.

## Confirmed decisions

- Target at least60FPS in ChapterOne test2 at current appearance/video settings.
- Preserve previous acceptance rule: at least1ms matched completed-frame saving in a view, no repeatable slowdown or added stutter in any view actually compared, correct visuals. Initial scope is test2 only; broader acceptance deferred. No MSAA, resolution or quality reduction disguised as optimization.
- First establish and reduce baseline injector overhead with effects disabled; then optimize enabled effect work.

## Evidence and limitations

Normal Linux artifact integration is runtime-validated (~47FPStest2,100+FPSrainyBalamb). Exact historical comparison identifies added tag capture at0cc705d as a substantial loss; modern selection leasing recovers the earlier coarse40–60FPS range with tags intact. The remaining old-code versus native122–145FPS gap is not quantified by matched traces. UserFPS ranges, selected-scene CPU/GPU intervals and completed-frame timings are distinct evidence. Never sum overlapping stage intervals or treat disabled shadow master as complete injector pass-through.

Current strongest concrete lead: local casting can beON while receivingOFF. Verified local replayCPU4.8665→0.0003ms when capture actually disabled; sceneCPU16.6673→12.0477ms. Receiver and A2C use local maps only with receiving enabled; fog lamp glow uses a separate native light census. Debug captures remain potential data consumers. Prepared bucket/dynamic replays and generation lifecycle must also be gated, not merely post-scene staging.

Weather per-draw uploads were measured~1.8ms, including masterOFF. Dry-payload skipping is already integrated and saves1.78–1.97ms in a matched dry run. Active-weather generation/value deduplication is unmeasured. First consider batching a single scoped binding save/restore before persistent binding caches.

Current draw-buffer ownership fences are not a general GL-state mirror. Broader caches need specific native mutation, object/context lifetime and invalidation evidence. Existing weather registry is keyed by bare program ID and link/delete resets priming only: expanded payload/location caches need proper linked-program generations and deletion/name-reuse/context handling. Per-draw native eligibility, depth writes, material/object identity, highlight and transparency coverage must remain current.

## Settled design tree

- Q3 settled: focus on test2 first; broader acceptance/coverage deferred at maintainer request. No cross-area safety claim from test2-only results.
- Q4 settled: stop producing unused effect data; safely rebuild on re-enable. Explicitly requested capture diagnostics still count as consumers; normal hidden effects do not justify warm production.
- Q5 settled: test2-only candidates remain experimental until broader scene checks. No normal-default promotion from this initial scope.
- Q6 settled: one/two-frame safe rebuild when re-enabling an effect is acceptable; stale shadows, flashes and broken transparency are not.

## Proposed execution tree (not yet authorized for implementation)

1. Verify/store clean current artifacts and isolated effective settings. Establish matched no-injector, loaded forwarding-only diagnostic, normal effects-disabled, and current full-effects cases. Preserve explicit material routing as its own measured case; visually different native/pass-through runs are attribution, not acceptance substitutes.
2. Measure actual completed-frame median/p95 plus asynchronous CPU/GPU attribution and draw/replay counts. Verify effective live switches, scene/weather/camera/video/MSAA, local map/cadence/source settings, no competing GPU applications and no stale library.
3. Follow measured costs. If disabled-path overhead dominates, fix its proven unnecessary work; if it is small, move to active-feature work rather than inventing cache gains.
4. Conditional priority: unused local capture demand across every producer/consumer; active-weather payload/scoped binding work; remaining measured hook work. Do not revive prior failed capability/texture/validation caches based on call count alone.
5. Before broader cache work define exact owner, validity interval, all producers/mutators, invalidation and safe fallback. Treat shader/context/resource lifecycle tracking as a prerequisite, not an assumed property of existing draw-buffer fences. Program relink/delete/name reuse; area/context changes; native GL state mutation; resize/MSAA/resource replacement and effect toggles require explicit handling.
6. For each candidate run separate baseline/candidate/baseline pairs, complete-frame acceptance, relevant state/mock/shader/coverage regressions and agreed runtime visual checks. Reject unmeasurable gains and unsafe fallbacks.
7. Keep this phase’s candidates in a separate experimental artifact. After test2 success, defer broader-scene acceptance and normal-default promotion until that coverage is explicitly resumed. Any later normal Linux development/shipping integration must compile diagnostics out and verify actual runtime artifacts. Linux reference; preserve Windows behavior.

## Documentation

Canonical overhead/effect terms are in GLOSSARY.md. Full evidence remains TAG_WORK_PROFILING.md and032424A_PERFORMANCE_COMPARISON.md. No ADR yet: no specific hard-to-reverse ownership/cache architecture has been chosen. Final shared-understanding confirmation is required before implementation.

## Operational contract

- Use the existing clean normal integration as the current source/artifact reference; preserve prior checkpoints and user work. Keep candidate controls diagnostic, reversible and outside persisted user settings.
- No game resources, ordinary launcher or original INI changes; use isolated settings and verify actual loaded library plus effective live switches.
- Use VSyncOFF, same camera/resolution/MSAA/weather/native light selection and video settings. Native versus forwarding-only comparisons are attribution only when their visual behavior differs. Separate baseline overhead from enabled-effect cost.
- Performance acceptance uses matched completed-frame medians and p95/stutter observations, not a reduction in isolated GL-call timings. At least1ms gain per retained candidate in test2;60FPS remains the combined full-quality target, not a guaranteed result of each experiment. If an experimental change reduces quality or produces stale data, reject it regardless of speed.
- For suspended capture, audit every consumer including explicit diagnostics, every entry point (staging, bucket replay, dynamic duplicate), prepared work, completed publication and generations/fades/cadence. Resume with fresh complete data within the agreed one/two-frame rebuild allowance; retain receiver-before-staging order and exact native authority.
- No blanket once-per-frame texture binding or mutable GL-state cache. Cache only what has a proven owner, change signal and invalidation path; otherwise keep draw-current state/fallback. Full program identity and location invalidation must precede expanded weather payload caching.
- Required checks include effectOFF/ON toggles, cold/re-entry, weather transitions, program/resource lifecycle where touched, exact GL restoration and native MSAA/transparency/highlight behavior in test2. These are correctness cases, not an unapproved expansion to additional performance scenes.

## Interview completion

Q1–Q6 are settled. No additional user choices remain before execution. Maintainer confirmed: Yes—start this plan. Planning interview complete; execution begins with matched baseline measurement. No new ADR: this plan commits to reversible measured experiments rather than choosing a hard-to-reverse cache architecture.

## Execution start

Prepared isolated run directory/tmp/nwn-injector-overhead. Using installed MangoHud0.8.4-1+ (local shipped docs/config) with HUD hidden, no FPS limit, no uploads, log_interval0 and20second log sessions triggered by mangohudctl; same measurement tool across all cases. Renderer remains unchanged. Native baseline first, then loaded-but-OFF and effectsOFF with native shadow hiding disabled. Verify logging captures actual per-frame data before using median/p95. Profile helper overhead is common to cases and absolute values remain instrumented.

### Measurement tool correction

MangoHud was loaded in native PID466618, but a20second remote logging request produced no CSV and no active logging message queue. The attempt is rejected: zero usable timing samples, not a baseline result. Maintainer reminded us that the established RenderDoc setup already exists. Reuse/tmp/nwn-renderdoc/renderdoc_1.47 and its working capture/control/replay scripts for matched GPU draw costs; keep existing internal SDL-swap and asynchronous stage tools for live injector comparisons. GPU replay timings and capture-instrumented FPS do not replace ordinary completed-frame acceptance. No renderer optimization has started; native frame-time baseline is still unmeasured. Prepared renderer-free RenderDoc launcher with no MangoHud and no forced VSync/quality changes.

### Native RenderDoc capture saved

Renderer-free PID470490 verified with only librenderdoc.so, no renderer/MangoHud. User restored stationarytest2/VSyncOFF. Existing target-control script verified PID then triggered one OpenGL frame:14475,85,250,805bytes, saved/tmp/nwn-injector-overhead/renderdoc-native/test2_frame14475.rdc. No GPU replay or frame-rate conclusion yet. Next case is normal library with NWN_SHADOWMAP_OFF1, which skips renderer hook installation but leaves exported preload forwarding present; this is not equivalent to unloading the library.

### Main-renderer-OFF capture and camera handling

PID471363 verified root normal library plusRenderDoc, OFF1 and constructor log: no Scene::Render hook installation. Other modules/resolver wrappers still initialize/intercept, so label this case main-rendererOFF, not fully inert. User accidentally triggeredF12, producing validtest2_frame7043.rdc (84,980,481bytes). Embedded thumbnail confirms area; camera differs from native14475. Both are suitable for structural/API-cost investigation, not precise matched timing deltas. CPU-only Chrome trace conversion produced API timelines for both, no GPU replay yet.

Maintainer cannot manually match camera pixel-perfectly. Do not request this again. Prefer default post-load view for further structural captures; use camera matrices/counts to qualify differences. Candidate acceptance comparisons must alternate paths within the same stationary session to eliminate cross-launch camera mismatch. No small-gain claim from mismatched frames. Prepared effectsOFF case with isolated settings: master/casters/local cast+receive/fog/heat/film/puddles/private rain/native-rain hidingOFF; native engine shadow hidingOFF; script overrides disabled to prevent a nominally disabled effect being re-enabled. Material-routing remains normal and must be treated separately if active.

### All-effectsOFF capture and structural result

PID474366 verified normal renderer plusRenderDoc and isolatedOFF settings; saved OpenGL frame9287 at/tmp/nwn-injector-overhead/renderdoc-effects-off/test2_frame9287.rdc (104,119,199bytes). User loaded defaulttest2 camera. Native14475/main-rendererOFF7043 cameras differ; no precise timing delta or FPS gain is inferred.

CPU-only RenderDoc Chrome trace export, restricted to chunks after Internal::Frame Metadata and before End of Capture:

| Case | GL calls in frame | Draw/multidraw calls | glUniform1f |
| --- | ---: | ---: | ---: |
|Native|8339|4645|105|
|Main rendererOFF|8318|4656|107|
|Renderer active/effectsOFF|17566|4614|8592|

Whole export also contains resource initialization: exclude that prefix (11414/11262/11424 trace events respectively). Raw API counts/recorded durations in/tmp/nwn-injector-overhead/api-comparison.json initially cover whole export, so use the frame-filtered table above for live frame counts. Trace displayTimeUnit isns; begin/end events must be paired, not assumed phX. Captured API durations include RenderDoc overhead and are not accepted normal-play timings. Queries are not necessarily recorded in the replay event stream.

The large repeated scalar-weather-upload increase is a concrete CPU-side lead, not yet an accepted optimization or cause of the entireFPS gap. Current inactive-weather fast path still uploads eligibility/rain/snow on every draw before skipping the remaining payload. Next: analyze GPU draws with live game closed, verify shader gating/ownership, then build a separate reversible experiment with proper program/context lifecycle and compare paths within one stationary session. Current normal renderer implementation unchanged in this phase.

### Capture replay analysis completed

Game closed and no other game observed in GPU process monitor. Replayed all3frames via the established RenderDoc script,4counter runs each (first treated aswarmup), recordedGPUclocks, shaders, representativecamera uniforms and targets in/tmp/nwn-injector-overhead/replay-analysis.json. Draw actions4645/4656/4614 and fragment invocations~6.98M/7.22M/6.88M respectively; all contain1920x1006/8sample world targets. Cameras and animation differ. Generic per-event duration sums~16–18ms are replay/query measurements, not game frame time; clocks varied materially (native960MHz/810MHz memory versus effectsOFF1665MHz/7001MHz), so no small GPU regression conclusion is valid. No clock policy was changed. The robust finding is roughly8487extra glUniform1f calls, accounting for most of the9227extra frame API calls in effectsOFF.

### Experimental candidate1: dormant weather scalar uploads

Added only to dedicated diagnostic build. On inactive rain+snow (both <=0.001, matching shader gates), after full sampler/payload initialization and known inactive amount uploads, skip repeated eligibility/amount uploads for the same program in the same selected-scene frame. CPU native bucket/classification still executes. Re-upload at least once per program per frame, immediately on active weather and on transition back toinactive. No texture-binding or generalGL-state mirror, no native draw decisions cached.

Diagnostic program registry fully invalidates on link/delete/name reuse, reuses invalidated slots even at capacity, and clears on context identity change. Unknown context disables new elision. Toggle boundaries reset context tracking. This lifecycle handling is shared by both diagnostic comparison phases; normal build retains existing behavior. Candidates remain experimental per Q5.

CPU integration test tests/weather_dormant_upload_regression.cpp passed50same-frame inactive draws without uploads, per-frame refresh, wet/dry/snow transitions, unknown/new context, relink/delete/recycled program names, full registry slot reuse, and fallback. Existing inactive-weather and lease tests passed. Normal library hash remains45e060b5...ecb1314f (unchanged). Dedicated benchmark variant5 alternates8→9→8→9→8 for5eight-second phases, excluding2second warmups, with the integrated dry fast path and selection lease enabled in both phases. This compares current-normal behavior to scalar-elision candidate, not old pre-optimization behavior. No runtime gain claimed yet.

### Candidate1 rejected by matched completed-frame measurements

PID477411, variant5 completed all5phases with camera/focus/settings guards intact and4487draws/world throughout. Normal integrated inactive-weather baseline versus dormant-scalar upload elision:

|Phase|Variant|Median ms|p95 ms|Frames|
|---|---|---:|---:|---:|
|0|Baseline|11.8661|12.4849|521|
|1|Dormant elision|11.7674|12.4157|526|
|2|Baseline|11.8216|12.4673|524|
|3|Dormant elision|11.8041|12.5156|521|
|4|Baseline|11.9455|12.8811|513|

Paired gains0.07645ms and0.07945ms, well below1ms. Reject; API count reduction is not sufficient evidence of useful performance. GPU scene3.5–3.7ms, CPU scene8.46–8.62ms across phases. Tag sample field−1 means no tag framebuffer was used with masterOFF; it is not the native game's MSAA setting. Current baseline preview flags restored after automatic stop.

Rejected candidate source/tests and replayable patch archived under/tmp/nwn-injector-overhead/rejected-dormant-upload; its registry/cache changes removed from active source. Normal artifact unchanged. Next prioritize the measured unused local capture path (previously~4.9ms), with lifecycle/consumer guards, rather than promoting a micro-optimization.

### Candidate2 prepared: demand-based local map suspension

Separate Linux diagnostic module shadow_local_capture_demand.inc, compiled to no-op allow() in normal/Windows builds. Demand requires existing master/capture/casting controls plus local receiving OR an explicit dump/trace consumer. Gated frame-entry, post-scene staging, bucket replay and dynamic duplicate preparation. Suspension cancels prepared flags/counts, invalidates published/filtered map readiness and serials, resets capture cadence/completion epochs and layer stamps. Retains GPU allocations; same-area fade history reconciles through existing authority on resume. Area changes clear retained join/radius history. Complete generations still publish only at the existing bucket-completion point; receiver/staging ordering and native light selection are unchanged.

CPU tests passed baseline/no-op preservation, hidden staging/replay cancellation, explicit dump/trace demand, no stale publication on resume, directional caster independence, master/casting/capture toggles, area changes and fallback. Existing selection-lease regression passed. Normal artifact remains45e060b5...ecb1314f.

Variant5 now names local-capture-demand (the prior failed variant source is archived separately); sequence8→9→8→9→8, normal integrated lease/inactive-weather optimization enabled in BOTH phases. Benchmark now guards actual local cast/receive/map/source/cadence values as well as prior scene/effect settings. Use isolated reference settings from validated normal-preview comparison (masterON/local_castON/local_receiveOFF), not all-effectsOFF, so only unused local capture changes and current visible appearance stays intact. Runtime acceptance pending.

### Candidate2 matched runtime comparison — 2026-11-18

Resumed after maintainer pause. Temporary launcher recreated after /tmp cleanup; original settings/launcher and normal artifact unchanged (SHA45e060b5...ecb1314f). Diagnostic PID12093 verified mapped root libnwn_shadowmap_tag_bench.so, no RenderDoc/MangoHud. Native Linux, test2 default stationary camera, VSync OFF;1920x1006, actual tag target8samples. Master/fog/moving/fixed ON; local receiving OFF, casting ON,1024map,3sources,Ultra0ms. Isolated settings copied from game settings with explicit reference controls restored; this is not an all-effects-OFF comparison.

Variant5 completed all5eight-second phases with no guard aborts or lease gaps. Baseline/candidate/baseline/candidate/baseline actual completed-frame medians:21.2892/15.7462/20.2551/16.8410/20.3525ms. Corresponding p95:21.8289/17.2203/21.6032/17.2550/22.3293ms. Frames283/374/292/366/287. Candidate savings against adjacent baseline means5.0260ms and3.4628ms, both exceed1ms target. CPU scene~18.3–18.4→13.7–13.8ms; scene GPU~13.1→9.2ms. Draws3973→3403, consistent with stopping570 unused replay draws; these supplementary timings/counts are not summed as frame savings.

Source log /tmp/nwn-injector-overhead/local-demand-test/game.log. Candidate passes initial test2 timing threshold, not full acceptance. Preview3 requested after run to hold both accepted optimizations plus demand gate ON. Visual checks, local-receiver re-enable freshness and broader regression remain pending. Experimental only; normal build unchanged.

Candidate2 user visual check passed in test2. User then enabled local receiving: shadows resumed correctly, FPS returned~47 as expected because demand exists. No performance gain claimed while receiving ON. Normal artifact still unchanged.

### Enabled local-shadow attribution — 2026-11-18

PID13874, root diagnostic library with NWN_SHADOW_STAGE_PROFILE=1, candidate preview3 confirmed; local_cast=1/local_receive=1, master/fog/both sun caster optionsON, film/puddles/private rainOFF. Test2 default stationary camera,1920x1006, VSyncOFF requested. Stage mode2 retained attachment batching.613GPU/614CPU samples, no camera abort. CPU local replay4.7575ms; sun replay4.7405ms; shelter0.0006ms. SceneCPU19.1418ms/GPU14.9996ms; native including hooksCPU18.9273ms/GPU12.4529ms; receiverGPU0.5816ms with composite0.4147ms; fog1.7398ms. Intervals overlap and must not be summed. Scene-entry SAMPLES0 is not world-target MSAA (known8sample world); no MSAA change made.

This identifies local replay as substantial CPU cost, not an isolated GPU attribution. Preparing narrower diagnostic-only CPU attribution for local preparation, visible dynamic duplication and filtered-object duplicate capture. Existing group timer no-ops in normal builds. No rendering behavior change or optimization accepted from these new probes. Log /tmp/nwn-injector-overhead/local-demand-test/local-profile.log.

### Enabled local detailed attribution and attachment experiment

PID14521, same launcher/settings,625GPU/626CPU samples, local cast/receiveON. Local replayCPU4.7076ms, filtered duplication nested within it1.8138ms, local preparation0.0016ms, visible dynamic duplication0ms in this view. SceneCPU18.7548/GPU15.0170ms; compositeGPU0.4188ms. CPU intervals overlap; preparation is post-native staging and its estimate is not a complete selected-scene total. Log local-detail-profile.log. This supports targeting repeated filtered-map setup rather than changing engine source selection or quality.

Prepared diagnostic-only filtered-attachment reuse candidate (benchmark variant6:9→10→9→10→9). Both baseline/candidate retain selection lease, inactive-weather skip and local-demand gate; receivingON keeps capture wanted. Within each guarded local bucket/layer, first eligible filtered duplicate attaches current write texture/layer; later eligible duplicates retain it, checking exact FBO/texture/layer. No state knowledge survives the guarded replay. All draws, per-draw binds/viewports, creature exclusion, clears, source authority, map resolution/cadence and publication remain unchanged. Normal/Windows helper always attaches. No FPS claim pending matched completed-frame run. Dedicated build, shader checks, demand lifecycle regression and diff check passed; normal artifact not rebuilt or replaced.

### Filtered-attachment experiment rejected

PID15327, variant6, local receiving/castingON, unchanged master/fog/casters/settings. Same-session baseline/candidate/baseline/candidate/baseline frame medians21.2765/21.3148/21.3548/21.2949/21.3679ms; p9522.9523/22.3388/23.3561/23.3869/23.2644ms. Frames278/279/274/275/275;~4048draws/world; no guard abort or lease gaps, actual tag target8samples. Adjacent-baseline mean savings0.00085ms and0.06645ms, far below1ms target. Reject attachment reuse, not a meaningful improvement. Candidate source archived under/tmp/nwn-injector-overhead/rejected-filtered-attachment, removed from active source. Running preview3 requested (accepted optimizations plus demand gate, rejected reuseOFF). Normal artifact unchanged. Log filtered-comparison.log.

Next inspect the actual native replay shader cost/driver submissions rather than retry attachment caching or reducing update cadence without visual authorization. Existing4.7ms replay CPU timing includes driver submission/stalls, not proof of4.7ms pureCPU arithmetic; GPU-event attribution is required before a depth-shader redesign.

### Enabled local-shadow RenderDoc capture analyzed — 2026-11-18

Official portable RenderDoc1.47 restored from renderdoc.org/stable/1.47/renderdoc_1.47.tar.gz, exact prior archive SHA200361c2761da1ca77a2acfe0c37e8db5839ba7cb9f204e8394d66dad7e6336a, buildb0e1370309f7d7bf986a73830446ca39d9b8c1fd. PID16516 verified both root diagnostic library andlibrenderdoc.so, noMangoHud; preview3 active, local receiving/castingON. User stationary defaulttest2, VSyncOFF. Saved test2_frame7512.rdc(~270MiB) under/tmp/nwn-injector-overhead/local-shadow-capture; game then closed before replay.

Frame4633draws. Native world1920x1006/8samples:2872draws. Local1024x1024/1sample targets:588common draws and230filtered draws (818total). Sun2048x2048/1sample:588common+230filtered. Scene1920x1006/1sample:121draws, plus4no-depth fullscreen draws. Local target array4 layers; selected source logslots2. This independently confirms local maps are NOT multisampled, despite native8xMSAA.

Four GPU counter replays and representative slow-draw shader sources saved in analysis.json/shader-*.txt. Event-duration sums varied significantly between repeats; these are query/replay costs, not completed game frame time. The shader sources are full native material families with lighting/weather/tag code. Source presence alone does not prove execution cost: driver dead-code elimination and alpha coverage matter. Verified NO_DISCARD1/KEYHOLING0 families393/405 had no gl_FragDepth writes; other sampled local families haveNO_DISCARD0 and must preserve alpha coverage.

A capture-only diagnostic replaced just those two opaque fragment shaders with empty-main variants, baseline→replacement→baseline,4counter runs each. No source/game shader was changed. Local event-sum medians excluding each warmup:5.938176/5.684224/5.846016ms, but individual main/filtered target times moved in opposite directions and repeats varied materially. Replacement also affects world draws using these programs, so this is not a local-only visual-preserving candidate or actual frame gain. No1ms result established; do not implement/promote a depth-only shader on this evidence. Replacement resources removed on replay shutdown. GPU capture complete, no further runtime setting changes. Next substantial opportunity needs reducing repeated local geometry submission while respecting engine-selected sources and exact material coverage; attachment caching and simple opaque shader stripping are not demonstrated solutions.

### Shared caster-work planning interview — 2026-11-18

Maintainer invoked grill-with-docs after proposing common sun/local caster work. Q1 settled: sharing may cover only verified compatible draws; all other draws retain existing rendering. Appearance, creature exclusion and settings must remain unchanged. Q2 settled: bounded reversible experiment first, not a committed redesign. Existing acceptance remains test2 first, >=1ms repeatable completed-frame gain, no worse stutter/visuals, experimental until broader checks. Fresh-context read-only fact review underway. No implementation authorized by this interview until final shared-plan confirmation.

Shared-work factual review complete: sun/local replay already consume the normal visible engine bucket stream and share owner/creature classification. Filtered maps already avoid a separate engine traversal via per-draw duplication, but require additional GL submissions. Sun orthographic and local perspective maps are distinct depth information. A previous uniform-only transform substitution failed for static draws without refreshed uniforms (shadow_replay.inc443–459). No safe reusable full geometry/material snapshot is established. Engine GetShadowLights order/eligibility, matrix-stack restoration, native material coverage, creature culling, independent bias and completed-generation publication remain binding.

Proposed bounded investigation, pending final maintainer confirmation: inventory repeated traversal/material/transform preparation and identify a verified compatible draw subset; establish whether its complete state can be reused within the same frame without changing the engine-selected caster stream. If feasible, build one opt-in diagnostic prototype retaining independent projections/depth targets and fallback for every unsupported draw. Preserve all current appearance/settings, alpha coverage, creature exclusion, source selection/order, update cadence and safe publication. Compare actual completed frames in one stationary test2 session, >=1ms repeatable gain, no worse stutter/visuals. Keep experimental until broader checks. If even a bounded prototype requires broad native-state capture or a new layered renderer, return with that design/cost before implementing the redesign. No renderer implementation begins during interview. No ADR yet: no hard-to-reverse architecture selected.

### Paused by maintainer — 2026-11-18

Maintainer requested: “pause here but have all ready for the next step.” Testing and renderer implementation are paused. The final shared-plan prompt received this pause request rather than an explicit confirmation to start; do not treat it as authorization to begin the new shared-geometry prototype. On resumption, use the recorded bounded plan and confirm any outstanding scope before implementation; do not repeat settled Q1/Q2.

Resume checklist:
1. Read AGENTS.md, CURRENT_TASK.md, this section and SYMBOL_INDEX.md. Preserve all uncommitted integration work and untouched launch-linux.sh. Only build/deploy the root checkout.
2. Review the saved capture/analysis under/tmp/nwn-injector-overhead/local-shadow-capture (test2_frame7512.rdc, analysis.json, shader sources, depth-upper-bound.json). Temporary files can disappear after reboot; persistent findings above suffice to avoid repeating rejected experiments.
3. Inventory same-frame reusable geometry/material/transform state for a verified compatible subset. No existing complete snapshot representation is established. Preserve engine-visible caster authority, GetShadowLights order, native coverage, static/skinned transforms, separate projections/maps, creature exclusion and safe publication.
4. If a bounded proof is feasible, use a separate diagnostic-only experiment with fallback; otherwise present the larger architecture and cost before implementing it. Require >=1ms matched completed-frame gain in test2, no worse stutter/visuals; experimental until broader checks.

Current runtime state: NWN was closed for capture analysis and was not relaunched afterward. RenderDoc replay finished. Normal development artifact SHA25645e060b5c4e8b03a42d4d94e445c0bad8969dbe8ce652df21ebe3504ecb1314f remains unchanged; current dedicated diagnostic library retains the unused-local-capture experiment and additional attribution probes. Rejected filtered-attachment reuse was removed from active source and diagnostic library rebuilt. No new checkpoint commit or push was made. User’s local receivingON visual check passed; it restores capture cost (~47FPS), as expected. No enabled-local-shadow optimization met the acceptance target yet.

### Resumed bounded feasibility investigation — 2026-11-20

Maintainer requested continuation. Resuming the recorded compatible-subset investigation; broader renderer redesign still requires returning with its design before implementation. Temporary prior captures/tools no longer present, persistent evidence retained. Source review confirms immediate duplicate draw helpers preserve live native geometry/material state, but currently track only selected transform uniforms; this is not a complete reusable draw ABI. A static uniform-interception failure is already documented and must not be repeated.

Added diagnostic-only shadow_caster_census.inc, triggered for one selected world frame by PID marker/tmp/nwn-caster-census.start. Writes per-draw phase/bucket/layer/current engine draw-item pointer/ordinal/owner/program/count/indexed status, active uniform declarations, attached shader sources and current named transform values into an isolated OUT_DIR. Draw-item pointer is only diagnostic identity within this capture, not a persistent engine handle. Transport offsets, instancing, texture/vertex state, resource lifetimes and compatibility are NOT certified by this report. It is an input inventory, not a geometry snapshot or optimization. No GL state setters or drawing decisions changed. Diagnostic collection frame is explicitly excluded from performance measurement; no data retained as rendering authority. Normal/Windows stubs are no-ops. Build and shader checks/diff check pass; normal library SHA45e060b5...ecb1314f unchanged. Runtime collection pending.


### First shared-input census collected — 2026-11-20

PID59987 frame934 completed,18programs inspected, no rendering changes. Sun/local layer0 each294 matching draw-item signatures; same seven programs,115non-creature items in three programs. Reconstructed m_view_inv*m_mv maximum delta0.000012658. Driver attached-shader source queries returned length0; all programs expose bone arrays and other view uniforms, so this is not a rigid/opaque compatibility certificate. Full bindings/draw variants/material/resource state remain uncertified. See docs/SHARED_CASTER_FEASIBILITY.md for results, explicit limits and a proposed scoped shared-draw design. This reaches the recorded broader-design review boundary before implementation. No geometry-sharing renderer or gain claimed. Normal library unchanged. Game remains open in the read-only diagnostic session; census automatically stopped after one frame.


### Restricted shared-draw design prepared — 2026-11-20

Maintainer requested design before implementation. docs/SHARED_CASTER_DESIGN.md proposes first proving local-slot-to-local-slot reuse of verified rigid opaque non-creature meshes at the existing Linux PartTriMesh::Draw boundary. Sun sharing is a later extension. Explicitly accounts for native preparation that must actually be bypassed, retained per-target draws and fallback bucket iteration; no gain assumed. Exact linked sources, active deformation/coverage state, transform recipe, hook preparation coverage/side effects and resource lifetime are unresolved read-only gates. Then immediate mesh-scope fanout and a bounded same-frame completion ledger may be prototyped only after review. No generic recorder, partial map publication, quality changes or shared renderer implemented. User review pending for scope narrowing and gates.


### Local-light-first design scope accepted — 2026-11-20

Maintainer selected “Yes—local-light sharing first”. Proceed with read-only gateA in docs/SHARED_CASTER_DESIGN.md, not renderer implementation. Added diagnostic-only12second caster timing at existing Linux PartTriMesh::Draw adapter, nested scopes counted once. Reports total/non-creature mesh-body CPU time, original local draw coverage and call count; no GL queries/file I/O in timed per-draw path. Camera guard and2second warmup; not completed-frame gain and not an optimization. Existing one-frame input report now records actual skinmesh and fAlphaDiscardValue values. Isolated launch-boundary.sh enables existing source dumps at compile time to address post-link source_unavailable. Diagnostic build/diff check pass, normal library untouched. Runtime boundary measurement pending; requires restarting current game with new diagnostic library.


Boundary gateA PID61417:487frames, PartTriMesh body3.220257ms including2.662743ms non-creature;486/574original local draws covered. Separate census1936:71unskinned no-alpha-uniform non-creature candidates (155/153/154 nativeNO_DISCARD1),37cutout items with0.2threshold fallback. Shader dumps recovered. Added diagnostic-only forwarding submit wrapper to separate later-slot candidate mesh-body cost from actual native driver draw submissions; all calls still forwarded exactly once. Follow-up nomination comes only from the same-session census and is diagnostic identity, never rendering authority. Read-only gate remains in progress, no renderer prototype. Dedicated build, normal syntax check and diff check pass. Detailed findings in SHARED_CASTER_FEASIBILITY.md.


### Local-only headroom gate failed — 2026-11-20

PID62970 census1016 then472timed frames:74diagnostic-nominated Parts, later-light mesh-body0.869539ms incl0.247356ms driver draw submission; per-frame remainder median0.621844ms. Local586draws,498covered/root Part calls. No camera abort. This is CPU attribution, not actual completed-frame gain; even the full later mesh body is under1ms. Decline the initial local-only sharing prototype; do not spend implementation on it or promote changes. Broader sun/local reuse is not ruled out but needs its own scope/evidence, not linear extrapolation. Full results/limitations in SHARED_CASTER_FEASIBILITY.md; design disposition updated. Normal library unchanged, no shared renderer implemented, diagnostic probes stopped automatically. Game remains open for user. No commit or push.


### Broader sun/local investigation requested — 2026-11-20

Maintainer chose “Investigate sun/local sharing”. Extended read-only timings to include nominated mesh calls in sun dynamic buckets2/3, using first sun cascade as proposed producer; later sun cascades plus local slots are measured consumers. Separate shader/coverage and target-policy contracts remain binding. Cross-sun/local design section added with explicit source predicate and sun in-place versus local-generation publication risks. No shared renderer implemented. Need joint and consumer-specific headroom before choosing prototype scope; do not extrapolate local-only numbers. New diagnostic build pending/completing; runtime collection requires restarting current probe session.


### Joint sun/local headroom measured — 2026-11-20

PID63680 census791 then478timed frames,71source first-sun calls/213later calls.
Consumer body2.540650ms minus native driver draw submissions (per-frame) leaves
median1.808048ms generous CPU preparation headroom. Local consumers alone:
body1.696201ms, driver0.486113ms, remainder1.210195ms. Not actual-frame gain or
compatibility proof. Documents updated with proposal for first-sun producer to
private local write consumers, sun original path retained, slim~0.21ms overhead
budget and all exact contract gates. Review pending before rendering prototype;
later sun consumer takeover remains larger publication scope. Normal artifact
unchanged, game open, probes stopped automatically.


Prototype scope conditionally authorized by maintainer; remaining contract gates
must pass first. Native inspection found exported model/opacity/renderability
queries and SetModelMatrix in the engine bucket before mesh dispatch. Extended
the read-only one-frame census to report these authoritative values at Part
entry and actual draw, including current shader. Dedicated diagnostic build and
normal syntax/diff checks pass. No sharing/suppression implemented or enabled;
normal artifact unchanged. Runtime contract collection pending new launch.


### Focused audit replaces geometry-sharing work; threshold0.6ms

Maintainer redirected work to added-overhead audit and approved incremental
>=0.6ms repeatable completed-frame gains, same visuals/no worse stutter. Geometry
sharing set aside; no shared renderer implemented. docs/INJECTOR_OVERHEAD_AUDIT.md
records current audit. Found trampoline-less PartTriMesh::Draw and GetNextItem
fallbacks rewrite code every callback, introduced in historical added machinery.
Earlier Part scope times included this fallback and must not be attributed
entirely to native preparation. Prepared Linux diagnostic-only exact-prefix
native gateways, preserving original native instructions/body and predicates.
Synthetic installed-hook regression, diagnostic build and normal syntax/diff
checks pass. Variant6 matched test prepared; runtime/visual acceptance pending.
Normal library unchanged. Existing rejected micro-optimizations remain rejected
at0.6ms; do not revive geometry sharing based on lower headroom alone.


Audit gateway runtime PID68965 variant6 passed initial timing: paired completed
frame savings1.28295/1.2758ms; baseline medians20.0720/20.0473/19.8933 vs candidate
18.7767/18.6945ms, p95improved.3959draws/world all phases,3654mesh+3926iterator gateway
calls/world candidate,0lease gaps. Preview4 requested continuouslyON for visuals.
No shader, tagging, caster or quality reductions. See INJECTOR_OVERHEAD_AUDIT.md.
Normal artifact unchanged; visual/broader checks and normal integration pending.


Gateway audit candidate test2 continuous visual preview passed by maintainer:
camera movement/highlights/shadows/foliage/transparency looked correct. Paired
completed-frame gains1.28295/1.2758ms meet current0.6ms target. Keep experimental
until broader checks and clean normal integration. No new default/artifact
promotion, checkpoint or push. Geometry-sharing investigation remains set aside.
