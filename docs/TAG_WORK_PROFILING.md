# Further tagging investigation — 2026-11-17

Protected accepted checkpoint: `12757b0`. Investigation branch: `codex/tag-work-investigation-20261117`. Acceptance remains at least1ms of frame-time improvement in one of the two Balamb views, no repeatable slowdown/stutter in the other, and unchanged visuals. No further optimization has passed acceptance.

## Measurement method

Optional `make tag-bench` builds `libnwn_shadowmap_tag_bench.so` from this root checkout with `NWN_TAG_WORK_BENCH_DIAGNOSTIC`. Normal development/shipping builds compile the new benchmark hooks, sampled timers and candidate switches out. The diagnostic uses verified SDL swap completion intervals for actual frame median/p95, asynchronous GPU scene/native timestamps and1-in16 sampled CPU draw-group estimates. GPU intervals include scheduling dependencies; CPU group attribution is not an independent speedup budget.

NVIDIA RTX3060Ti, driver620.70.06; native NWN89.8193.37-17[26c6e573],1920×1006, actual world tag framebuffer8samples. VSyncOFF, receiverON, both caster optionsON, full-resolution fogON, puddlesOFF, clear weather. The existing test1 save was loaded without overwriting it; its camera was manually reconstructed toward the supplied stairs reference and raised slightly to avoid millimetre-scale collision jitter. It is not claimed pixel-identical to the original screenshot. A lighter sea-facing reload was only a harness sanity check, not a second accepted benchmark view. The original rainy view is still pending.

Each phase runs8seconds with2seconds excluded, alternating checkpoint/candidate/checkpoint. Camera, projection, relevant settings, weather, focus and VSync guards abort invalid runs. Rejected runs produced no performance result. Controller updates now occur only after the selected scene is confirmed to have rendered the world; secondary views do not drive guards. All runs restore candidate flags afterward.

## First candidates: rejected

Heavy stairs run,1132 observed draws/world,8sample tag target:

| Phase | Variant | Frame median ms | Frame p95 ms | Sample count |
| --- | --- | ---: | ---: | ---: |
|0|Checkpoint|13.8758|14.1510|431|
|1|Texture no-allocation fast path|13.8713|14.4529|432|
|2|Checkpoint|13.8767|14.2191|431|
|3|Context/scene-bounded capability cache|13.8798|14.2590|430|
|4|Checkpoint|13.9217|14.7269|427|
|5|Both|13.8676|14.3676|430|
|6|Checkpoint|13.8809|14.1369|431|

Paired gains against the mean of adjacent checkpoints:0.00495ms,0.01940ms and0.03370ms respectively. None approaches1ms. The texture path reduced sampled CPU texture work from approximately0.267ms to0.030ms but not frame time. The capability cache added context lookup overhead. An explicit pause hotkey was used during earlier setup; its status was not independently verified, so this run is diagnostic evidence only, not gameplay acceptance. Both candidates remain disabled in normal builds.

## Validation query upper bound: rejected

A second run with gameplay running and the stable raised camera checked completeness initially, then diagnostically skipped repeated same-frame checks. This does NOT track all native attachment/storage mutations and is never a production proposal.

| Phase | Variant | Frame median ms | Frame p95 ms | Sample count |
| --- | --- | ---: | ---: | ---: |
|0|Checkpoint|13.8883|14.4382|430|
|1|Skip repeated checks (diagnostic)|13.9713|14.5627|429|
|2|Checkpoint|13.8503|14.3448|430|
|3|Skip repeated checks (diagnostic)|13.9576|14.7967|427|
|4|Checkpoint|13.9026|14.4061|430|

Sampled validation CPU time fell from approximately1.44ms to0.08ms, but actual frame time did not improve. Paired gains were approximately−0.102ms and−0.081ms. This is consistent with waiting moving elsewhere in the GPU/driver dependency chain; it does not establish that1.4ms of useful work can be removed.

## Next diagnostic

Measure separately with the tag draw target disabled and with its writes masked while retaining the buffer-selection calls. Both deliberately remove classification data temporarily and cannot be promoted as optimizations. No MSAA setting, native geometry, tag precision or normal renderer default is changed. Any eventual wider state lease must account for native clears/blits and all state mutations, not just draw calls. [Khronos specifies that colour blits write each enabled destination draw buffer](https://wikis.khronos.org/opengl/GLAPI/glBlitFramebuffer), so leaving a private integer output selected across arbitrary native operations is not automatically equivalent.

Raw local evidence lives under `/tmp/nwn-tag-work-bench/`; numerical results above are retained here because temporary files may disappear. The existing user launch script remains unmodified.

## Buffer-selection lease: first matched stairs run (2026-11-17)

User restored test1 stairs camera. Native Linux, RTX3060Ti/NVIDIA620.70.06,1920x1006, VSync off, actual tag framebuffer8samples; shadow master, both caster options and full-resolution fog on, weather clear. Dedicated root-checkout diagnostic library, PID324762, variant3. Five8-second phases with2-second warmups excluded.

| Phase | Variant | Frame median ms | Frame p95 ms | Frames |
| --- | --- | ---: | ---: | ---: |
|0|Checkpoint|13.8389|15.0182|428|
|1|Buffer-selection lease|8.1492|9.3225|721|
|2|Checkpoint|13.7548|15.0006|431|
|3|Buffer-selection lease|8.2132|9.1875|719|
|4|Checkpoint|13.8477|15.0209|427|

Paired savings against adjacent checkpoint medians:5.64765ms and5.58805ms. Candidate retained classification output and8sample target; approximately980 compatible draws reused selection across4 leases per world, zero detected ownership gaps. Candidate observed1054 draws/world versus1056 checkpoint; small native draw-count difference is recorded, not hidden. Native GPU interval fell from11.19–11.28ms to3.36–3.40ms. This meets the timing target in this view, but is **not accepted** until visual checks and the second Balamb view pass. Checkpoint behavior restored automatically on completion.

## Rainy view: matched second run

Same process, user restored original rainy Balamb view with detailed wet film ON, puddles OFF, full-resolution fog ON, casters ON and VSync OFF. Variant3 completed normally; checkpoint restored.

| Phase | Variant | Frame median ms | Frame p95 ms | Frames |
| --- | --- | ---: | ---: | ---: |
|0|Checkpoint|12.6471|13.7354|473|
|1|Buffer-selection lease|7.5289|8.5316|786|
|2|Checkpoint|12.6553|13.0620|474|
|3|Buffer-selection lease|7.3777|8.3027|797|
|4|Checkpoint|12.6196|13.2258|473|

Paired savings5.1223ms and5.25975ms. Candidate767 reused selections/world across4 leases; zero detected gaps. Native draws860.6/world versus checkpoint862.5–862.7. Timing criterion passes both scenes; visual validation and clean non-profiling preview remain pending.

## Continuous visual preview: passed

2026-11-17, process326270, same dedicated diagnostic library and launcher. Log confirmed72 verified native slots installed and `visual preview candidate ON`. No automatic timing run was active during inspection. User checked both Balamb views, camera movement, hover highlights, foliage/transparency, shadows and wet film, reporting no problems with the candidate and substantially better performance even with puddles enabled. Existing fog/light artifacts were explicitly deferred by the user; this does not establish their cause or resolve them.

Both agreed Balamb timing and visual criteria now pass for the experimental candidate. This is validation of the dedicated preview, not a claim that the normal development/shipping artifacts already contain the new path. Production integration and clean artifact verification remain outstanding. Original checkpoint12757b0 remains intact.

## Requested checkpoint

The maintainer requested a checkpoint immediately after the successful preview. Source, diagnostic build support, ownership-model regression tests and this evidence are preserved together. CPU ownership-model regression rerun passed; diff whitespace checks passed. Verified preview artifact SHA256: `d2bcd2b344c4345523e80793be27af4bb74c373f8c70e3ff92d470e6529425e8`. Local launch-linux.sh is pre-existing and excluded. No game resources or saved settings are included. Production integration remains pending.

## Post-checkpoint regression report — acceptance suspended

After checkpoint ecfe847 the user reported severe slowdowns, around40FPS, in ordinary game maps that previously ran well. The two Balamb runs therefore do not establish general safety. No production promotion is authorized by those limited results. In the existing preview process326270, `/tmp/nwn-tag-work-preview` was set to PID plus0; log confirmed `visual preview checkpoint ON`. User same-location comparison pending. This disables the active buffer-selection lease but leaves installed diagnostic dispatch hooks present, so a subsequent clean-library restart may still be needed to isolate hook overhead or retained state. Cause is unconfirmed.

## Chapter One test2: regression reproduced and attribution started

Native Linux test2, user matched slow view, PID330378,1920x1006,VSync OFF, actual tag target8samples. At stage capture master, fog, moving/fixed casters, wet film, puddles and private rain were ON. User reported no-injector122–145FPS; exact pre-candidate artifact hash37d680...7edd gave25–30FPS, masterOFF80FPS. These user runs lack automatic frame-time records.

Matched automatic variant3: checkpoint medians38.3306/38.4835/38.3855ms; candidate24.0386/24.0425ms. Candidate p9526.2564/26.4955ms versus checkpoint40.3040–42.1059ms. Paired gains14.36845ms/14.392ms. All phases3987draws/world; candidate2751 reused selections and4 leases/world, zero detected gaps. Candidate improves this scene but does not resolve the absolute slowdown. This falsifies the initial suspicion that the active candidate alone introduced the40FPS behavior; installed diagnostic overhead and other effects still require separation.

Candidate held ON, stage profiler mode2: sceneGPU15.1624ms/CPU21.1434ms; native including hooksGPU12.3372ms/CPU20.9069ms; receiverGPU0.8151ms, depth0.0707ms, colour0.2877ms, composite0.4137ms, fog1.8166ms. Overlapping intervals must not be summed. Stage-entry SAMPLES0 is a different framebuffer from the actual8sample world target. Native scene including hooks dominates; this does not yet separate native geometry, shadow replays, weather and interception CPU cost. Next matched masterOFF run pending.

### test2 masterOFF stage comparison

Same view/PID, user changed only master OFF. Stage capture confirmed receiver0, fog/caster/weather switches otherwise unchanged.1032samples: sceneGPU3.5625ms/CPU9.0892ms; native including hooksGPU3.3741ms/CPU8.9846ms. Receiver0.0010ms, fog0.0041ms, no depth/colour/composite samples. Compared with candidate masterON sceneGPU15.1624ms/CPU21.1434ms, substantial work is master-gated outside the final composite. Stage samples are not completed presentation frame times, and no FPS inference is made from them.

Ranked next hypotheses: (1) caster capture/replay dominates the remaining masterON cost: disabling both casters with masterON should reduce native scene intervals; (2) per-draw classification/state handling dominates: cost should persist with castersOFF and drop with a controlled tagging bypass; (3) weather processing dominates some native work: weather-only ablations should reduce costs remaining after the first two comparisons. MasterOFF residual cost needs a later clean matched no-injector timing run, not subtraction of unmatched user FPS ranges.

### test2 casterOFF and tag-bypass probes

Candidate held ON, same view, masterON/fogON/weather switches unchanged. User disabled both moving/fixed casters.636samples: sceneGPU10.9998ms/CPU17.1657ms; nativeGPU8.0691ms/CPU16.9411ms; receiver0.7137ms; fog2.0029ms. Compared to castersON, scene improves roughly4ms, but material residual remains.

Temporary stage mode1 bypassed tag creation (not a valid visual optimization), restored automatically:745samples sceneGPU10.0270ms/CPU14.0853ms; nativeGPU7.3759ms/CPU13.8634ms; receiver0.6369ms; fog1.7879ms. Disabling tagging helps CPU-stage time by~3ms, GPU by~1ms, insufficient to explain all masterON versusOFF difference. Rendered content differs under missing classification, so this is attribution only. Next weather/fog separation pending.

### test2 fogOFF

Same view, candidateON/masterON/castersOFF; user disabled only fog.653samples: sceneGPU9.0624ms/CPU16.7875ms; nativeGPU8.1695ms/CPU16.5085ms; receiverGPU0.6717ms, depth0.0952ms, colour0.1536ms, composite0.4065ms; fog0.0010ms. Fog accounts for~1.9ms sceneGPU, not the large native CPU residual. Next grouped weatherOFF comparison will test wet film/puddles/private rain processing; it will not separately attribute those three components.

### test2 surface effectsOFF and repeated tagging bypass

Surface-effectsOFF stage mode2:642samples, film0/puddles0/privateRain1 (private rain had not yet been disabled), fog0/casters0/master1. SceneGPU9.3716ms/CPU17.1763ms; nativeGPU8.3343ms/CPU16.8641ms; receiver0.6830ms. No improvement from film/puddlesOFF. Subsequent temporary tag-bypass mode1 confirmed privateRain0 as well:723samples sceneGPU8.8607ms/CPU14.7542ms, nativeGPU7.8756ms/CPU14.4468ms, receiver0.6963ms. Private-rain setting changed between these runs, so this pair is not a pure one-variable tagging comparison. Tagging restored automatically afterward.

Narrow source inspection: capture_local_light_shadow and replay_bucket_into_local_light_layers are master-gated independently of directional moving/fixed caster toggles. Performance → Lights casting shadows is documented to disable local capture. Next test targets this separate replay path; no engine light-selection changes proposed.

### test2 local capture switch and CPU probe preparation

User disabled Performance → Lights casting shadows. Stage mode2,629samples: sceneGPU8.8535ms/CPU17.4803ms; nativeGPU7.9442ms/CPU17.1730ms; receiver0.5540ms; all weather flags0, fog0, casters0, master1. No material gain. Live local switch is not included in current stage log, so its state is user-reported rather than independently verified.

External debugger sampling attempted via gdb attach330378, immediately denied by OS ptrace policy; game not paused. Prepared diagnostic-only1/16 CPU attribution of weather observation, layer before_draw and A2C shadow begin/end. These measure CPU wall intervals and may include driver waits; they are not independent GPU costs. Normal builds compile timers out. Restart required to load probes.

### test2 first sampled draw CPU attribution

Restarted PID334041 with candidate previewON; mode2,693samples, masterON/castersOFF/fogOFF/weather flagsOFF. Sampled1/16 per-call CPU wall estimates: weather observation1.8923ms/world, layer before_draw0.1214ms, A2C shadow begin0.0706ms/end0.0697ms. SceneGPU8.4623ms/CPU16.1737ms; nativeGPU7.7947ms/CPU16.0079ms; receiver0.5079ms. Values are estimates and nested intervals are not additive independent totals.

Weather push_draw_state currently uploads eligibility, rain/snow/time, capture matrices/samplers and shelter/snow state per eligible draw even when wet film/puddles are disabled. This is a measured optimization lead, not yet proof that safe deduplication will improve completed frame times. Per-draw engine classification must remain authoritative and native uniform changes/program relinks must invalidate any proposed cache. Most remaining CPU time remains unattributed by these four groups. Next matched masterOFF sample will determine which measured groups are actually master-dependent.

### test2 matching masterOFF CPU sample

PID334041,1045samples; master0, casters/fog/film/puddles/privateRain0. SceneGPU3.3178ms/CPU9.1265ms; nativeGPU3.2266ms/CPU8.9395ms. Weather observation1.7893ms, layer0.1061ms, alpha begin0.0010ms/end0.0505ms. Weather overhead is shared, not the~7ms masterON scene CPU delta.

Prepared deeper diagnostic timing: common draw funnel total, native cross-module observers, tag begin/end, finish funnel total and full-rate bucket-level local/sun/shelter replay. These intervals can nest and must not be summed. Reporting now includes actual live local cast/receive flags: saved local_emit_cast remained1 and local capture messages persisted, so earlier user-reported localOFF isolation is not independently established. No source optimization or normal-build default changed.

### test2 deeper CPU sample: local capture still active

PID335865, candidate previewON,670samples; masterON, casters/fog/weatherOFF. Live report: local_cast1/local_receive0. Local bucket replay CPU4.8665ms/world; sun replay0.2248ms; shelter0.0004ms. Common draw funnel4.6500ms contains weather1.8063ms, tag begin2.0976ms, layer0.1395ms and observer before0.1867ms. Finish funnel0.5792ms contains observer after0.2662ms/tag end0.0866ms. Alpha shadow begin/end0.0655/0.0692ms. Nested values are not independently additive. NativeGPU7.7005ms/CPU16.4018ms, sceneGPU8.4326ms/CPU16.6673ms.

Key finding: the prior localOFF test actually had receivingOFF but captureON; direct live flags resolve the ambiguity. User asked to disable the separate Performance → Lights casting shadows capture switch for a matched comparison. A potential future deep-module issue is the separation allowing disabled visible local shadows to retain expensive capture; no behavior change is implemented until intended semantics are confirmed.

### test2 verified local captureOFF

PID335865,829CPU/828GPU samples. Direct flags local_cast0/local_receive0. Local replayCPU0.0003ms versus4.8665ms before; sceneGPU4.5199ms/CPU12.0477ms versus8.4326ms/16.6673ms; nativeGPU3.6844ms/CPU11.7793ms. WeatherCPU1.8298ms, tag begin2.1078ms. This is a verified switch-based isolation, not a code optimization.

Prepared diagnostic-only inactive-weather upload candidate, benchmark variant4: buffer-selection lease baseline → inactive weather fast path → baseline → fast path → baseline. It always uploads eligibility and exact rain/snow amounts; skips remaining payload only when both <=0.001 (matching both shader branch thresholds), only after one full initialization; relink/delete reset priming. Full payload resumes for active rain/snow. No cache of engine draw classification or weather authority. Mocked push_draw_state integration check passed initial payload, class changes, wet/dry/snow transitions, threshold boundary, disabled-probe restoration and link invalidation. Runtime gain unmeasured; no production promotion.

### test2 inactive-weather variant4: partial run, not accepted

PID338171. First arm refused (game unfocused). Focused NWN explicitly without camera input, rearmed variant4.2709draws/world, exact matching view guards; masterON, casters/fog/weatherOFF, rain0. Phase0 baseline lease15.0898ms median/p9516.3438, phase1 inactive-weather13.6362/p9515.5339, phase2 baseline15.2542/p9517.3530. Paired gain1.5358ms; sceneCPU11.6776/10.0841/11.7649ms, GPU3.4017/3.4181/3.7007ms. Ownership gaps0,2620selection reuses across4leases/world. Focus lost in phase3, run aborted and all experimental flags restored. Partial pair supports the CPU hypothesis but repeat/complete acceptance is pending; no claim of successful full five-phase run.

### test2 inactive-weather variant4: complete repeat passed timing criterion

Same PID338171/camera/settings,2709draws/world, zero ownership gaps. Baseline is the existing buffer-selection lease; only inactive-weather payload skipping changes. Actual framebuffer8samples, VSyncOFF.

| Phase | Variant | Frame median ms | Frame p95 ms | Frames |
| --- | --- | ---: | ---: | ---: |
|0|Lease baseline|15.3601|16.7753|393|
|1|Inactive weather|13.7265|15.0870|440|
|2|Lease baseline|15.6604|18.2472|383|
|3|Inactive weather|13.6310|15.1109|442|
|4|Lease baseline|15.5414|16.8834|390|

Paired savings1.78375ms and1.9699ms, approximately64–65FPS to73FPS. SceneCPU baseline11.7974/12.0722/11.9243ms versus10.0956/10.0366ms. Candidate improves median and p95; full five-phase run completed and restored checkpoint experiment flags. No rendering quality setting changes. Transition/init mock integration test retained as tests/weather_inactive_upload_regression.cpp and CPU-only runner. Runtime wet/snow transition and broader scene checks remain pending; normal/shipping integration remains pending. This improves dry-area overhead only, not the whole test2 deficit.

## Combined preview dry-to-rain validation

2026-11-17, restarted PID357231, same combined preview (selection lease plus inactive-weather upload skip). User loaded drytest2 then rainytest1 in the same process, enabled wet film/puddles/private rain, waited15seconds and reported all wet effects resumed correctly. This completes the pending user runtime transition check. Normal Linux integration now proceeding; clean normal artifact runtime check remains required.

## Normal Linux integration — checks passed, runtime pending

Selection leases and inactive-weather payload skipping are now enabled in normal Linux development/shipping source. Windows retains its former path. Normal world entry attempts lease installation once with a valid selected context; missing coverage retains per-draw selection. Ownership gaps disable reuse safely. Shutdown restores owned dispatch slots; normal nwn_oit state capture participates in the fence callback. Dry-weather skip preserves per-draw eligibility/amount uploads, full first initialization and relink/delete invalidation.

Both normal libraries compile without warnings. CPU ownership and inactive-weather integration tests passed. check_shaders.py passed. Sun/creature/highlight/MSAA regression passed, including Mesa software GPU coverage checks. nm verified normal/shipping artifacts have lease installation but no tag benchmark swap hook or per-draw CPU samplers.

Development SHA25645e060b5c4e8b03a42d4d94e445c0bad8969dbe8ce652df21ebe3504ecb1314f; shipping536c789b33cb0da517bed577b96885fdca9744bd0678ef5233019b99ff2fc36d. Clean isolated-settings normal launcher prepared at/tmp/nwn-normal-integration-test/launch.sh. Final runtime verification of the normal artifact pending. Existing local launch-linux.sh, game resources and original INI untouched.

## Clean normal artifact runtime: passed

PID362256, normal libnwn_shadowmap.so verified in process mappings; no NWN_TAG_WORK_BENCH, stage/wet profiling or historical bypass flags in process environment. Launcher/tmp/nwn-normal-integration-test/launch.sh uses the root normal development artifact and isolated settings copied from the validated combined preview. Native Linux89.8193.37-17, RTX3060Ti/NVIDIA620.70.06,1920x1006, VSyncOFF per ongoing test configuration. Log confirms72verified selection-lease slots installed, actual receiver-tag framebuffer5 with8samples, no reported ownership gaps.

Maintainer checkedtest2 then rainyBalamb in the same session: virtually same behavior/performance as optimized preview; test2~47FPS and rainyBalamb100+FPS. Prior combined-preview test2 camera-motion visuals and dry→rain resume checks passed. This validates integration in the normal Linux development artifact. Shipping library builds/tests pass but this clean runtime session did not load the shipping artifact. Runtime results are scene/settings-dependent; neither122–145FPS native parity nor60FPS test2 is claimed.

Remaining test2 cost predates tagging. Current copied settings still have local_emit_cast1/local_enabled0 (captureON, receivingOFF). Prior measurements isolated~4.9ms local replay in this condition. Source audit shows visible local receiver/A2C consumption is gated by local receiving; fog lamp glow uses a separate engine light census. A possible next optimization is avoiding unused map capture, but all consumers/lifecycle/diagnostic behavior need auditing before changing it. No such additional optimization has been implemented here.
