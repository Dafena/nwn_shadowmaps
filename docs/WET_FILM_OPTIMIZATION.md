# Wet-film optimization — design interview

Status: compact-film gameplay remained50–55FPS ON /60FPS OFF. Shelter-bounds acceleration regressed52→48FPS and was removed. A2048² shelter map improved the maintainer’s view to55FPS. A live resolution control is built for the approved1024² comparison;60FPS remains unproven.
Date: 2026-11-16.

## Settled decisions

- Wet film has ON/OFF states. ON retains the current Detailed effect.
- Remove Simple sheen: the maintainer cannot distinguish it from Off, but it costs performance. Legacy Detailed maps to ON; legacy Simple and Off map to OFF.
- Puddles remain independently switchable. The profiling scene keeps them OFF.
- Private rain is outside the optimization target: switching it did not change the maintainer’s observed FPS.
- Preserve the accepted transparency/coverage, engine authority and resource-loading behavior. Linux is the reference target.

## Evidence and limits

Maintainer’s fixed rainy Balamb comparison: wet film Detailed50FPS, Simple56FPS, Off60FPS, puddles disabled. The presentation was capped; these numbers do not establish uncapped GPU cost. The live material target reports1920×1006 with8 samples on an RTX3060Ti / driver620.70.06.

Existing developer-launch cost reports use wall-clock sections with glFinish stalls, not per-material GPU profiling. Isolated shader fixtures were useful for correctness and rejecting candidates, but did not establish the native-frame bottleneck. Tested arithmetic rewrites, lookup caches, loop and visibility experiments did not show a reliable further Full-quality win and were not incorporated.

The initial environment had no native frame-capture tool. A temporary official portable RenderDoc1.47 installation was subsequently used (see capture evidence below). Any capture setup must verify that the root injector is loaded and correctly represented in the captured/replayed frame. Development-launch diagnostics must not be mistaken for a clean performance baseline.

## Confirmed design tree

1. Visual contract: preserve apparent Detailed quality, density, motion, highlights and transparency. Small changes in random ripple placement are acceptable. Accepted.
2. Success criterion: Detailed wet film holds60FPS in the same rainy Balamb view, supported by GPU frame-time evidence and fixed camera/video/effect settings. Accepted.
3. Capture constraints: temporary GPU capture tooling, test-game restarts and brief pauses are acceptable. Installed game resources and unrelated settings remain untouched. Accepted.

Next, choose the capture method and repeatable scene fixture; measure native-frame ON/OFF differences; identify the limiting work before selecting a redesign. Tool/setup choices depend on capture constraints. A larger redesign is not yet selected.

## Change acceptance

Keep ON/OFF, camera, scene weather, puddles, private rain, fog, resolution, MSAA and video settings explicit in comparisons. Separate frame-time evidence from capped FPS and from isolated shader results. Verify the exact root build loaded by the game. Do not retain a change merely because it executes fewer apparent shader operations or allocates a smaller target.


## Native capture evidence — 2026-11-16

Temporary tool: official portable RenderDoc1.47, build `b0e1370309f7d7bf986a73830446ca39d9b8c1fd`, under `/tmp/nwn-renderdoc/renderdoc_1.47`. Archive SHA256: `200361c2761da1ca77a2acfe0c37e8db5839ba7cb9f204e8394d66dad7e6336a`.

Launcher: `/tmp/nwn-renderdoc/profile-launch.sh` wrapping the repository-root `run-shadowmaps-clean.sh`; root shipping `libnwn_shadowmap_deploy.so` and RenderDoc were both verified in the native game’s mappings. The wrapper set `NWN_DEV_NO_BUILD=1`, `NWN_SHADOWMAP_NO_SETTINGS=0`, `NWN_SHADOWMAP_LOG=1`, and the native game/output directory. RenderDoc used `--wait-for-exit --opt-hook-children --opt-disallow-vsync`; disallowing VSync was a temporary capture override. The clean launcher no longer forces verbose local-light tracing. Saved settings were loaded; no game resources or video configuration were replaced.

Platform: native Linux, NWN8193 build revision37 shader macros, RTX3060Ti / NVIDIA620.70.06. Executable SHA256: `6d19c39bc646af5ddbc333ff31797a4acd9019506bcb45ef1b93b1bd5925e700`. Scene target:1920×1006,8×MSAA,RGBA8. Maintainer held rainy Balamb camera/effects fixed, puddlesOFF and full-resolution fog, changing only wet film.

Saved frames (temporary evidence, not shipping assets):

- ON: `/tmp/nwn-renderdoc/balamb_frame3880.rdc`.
- OFF: `/tmp/nwn-renderdoc/balamb_frame11026.rdc`.
- Both open and replay successfully, with960 actions and943 draws and matching material-draw counts. Stored thumbnails show the expected checkbox states and matching scene. The ON replay’s final image also matches the captured scene, including injector effects.
- The OFF target-control client initially received an old ON-capture notification. The later OFF file was identified and validated directly; the metadata note was corrected. Never identify a new capture solely from the first queued notification.
- At representative ground draw10481 (264 indices, world-material fragment shader `ResourceId::495`), captured uniforms verify rain1.0, noise-ready1.0, puddles-disabled1, wet-film-disabled0. Shader/resource IDs are specific to these captures.

### Provisional probes; performance verdict pending

`/tmp/nwn-renderdoc/profile.py` collected per-event GPU duration and fragment invocation counts. The main world material accounts for653 draws and about4.744million fragment invocations. Fragment counts match closely between frames. NVIDIA vendor counters were unavailable (NvPerf library missing); generic GPU duration and invocation counters work.

`probes.py`, `probes2.py`, and `probes3.py` temporarily replace shaders only inside saved-frame replay. They do not modify the installed injector or game resources. Probes bypassed the ripple field, noise field, shelter lookup, or wet film, and separately tested constant-folding inactive puddle/snow branches, a smaller ripple neighborhood, and removal of a redundant normalization. No probe is an accepted production optimization.

Early repeated draw10481 samples were around0.269ms with detailed film,0.159ms with film bypassed,0.208ms with ripples bypassed,0.261ms with noise bypassed, and0.243ms with shelter exposure bypassed. These are individual draw timings, not whole-frame costs or projected FPS. A later unchanged baseline varied from0.264ms to0.737ms. Host inspection found another game running on the same GPU during replay analysis. Therefore these samples are provisional: repeat the comparison with competing game activity stopped before ranking candidates or claiming gains. Do not infer a60FPS result from them.

The captured shader establishes two concrete sources of repeated work: the impact field examines nine procedural cells per affected fragment, and precipitation exposure performs16 shadow-map comparisons per fragment. The actual noise texture is used, but its dimensions/resolution have not yet been established as a bottleneck. Next: repeat isolated probes with a stable GPU, validate the resulting images, then choose an optimization and verify outside RenderDoc.


### First candidate: compact film impacts

After the maintainer closed the competing game, replay still showed clock-dependent timings. Capturing GPU core/memory clocks alongside each run explained much of that variance. A temporary stock `GPUPowerMizerMode=1` preference did not hold clocks constant; its prior value0 was restored by a `finally` guard after both benchmark runs. No overclock or hardware clock limit was applied.

Candidate evidence: `/tmp/nwn-renderdoc/candidate-probes.json`, with baseline/candidate/baseline order, eight samples each, and logged clocks. Representative draw10481 baseline samples near0.263ms at1950MHz compare with0.243ms at1890MHz for the candidate. With7001MHz memory held steady, scaling these ALU-heavy draw timings to1950MHz gives about0.263ms baseline versus0.235ms candidate (roughly10–11% less for this draw); the ending unchanged baseline also returns near0.263–0.266ms after the same scaling. This clock normalization is qualified evidence, not a fixed-clock benchmark or a whole-frame/FPS claim. Full-frame timings still contain preemption/idle noise.

The candidate retains the original general nine-cell ripple kernel for puddles. Wet film has a separate fixed-half-scale kernel: maximum ring radius0.31cells plus Gaussian support0.1875cells is0.4975cells, so four cells adjoining the nearest grid corner contain all retained support. The cutoff is `q² <= 0.140625`; omitted tails have height at most `exp(-8.15625)` each. Centres, density and animation phases remain unchanged. This avoids a new cache texture, pass or texture unit. Native capture comparison of the exact film-only candidate differed by at most2/255 in any colour channel, with no pixel exceeding that difference. That single captured view does not replace moving-camera runtime validation.

The production shader now uses this compact kernel only for wet film. Regression fixtures extract the actual production GLSL and compare it with the preserved general kernel across signed world coordinates and phases0,1.3,11.7,4095. On NVIDIA, maximum sampled height error was0.0003892 and gradient-component error0.03512; Mesa also passed the analytic tail bounds. Existing wet/dry, opaque/alpha, shelter, procedural/texture noise, singular-gradient, optical-work gating, settings migration and trace-budget checks passed. `python3 check_shaders.py`, `make -j2`, and `make deploy -j2` passed. Root development and shipping libraries are rebuilt.

Next acceptance check: normal `launch-linux.sh` with the same rainy Balamb camera, puddlesOFF, full-resolution fog, unchanged video settings; compare wet filmON/OFF and inspect moving highlights and transparency. The maintainer has been asked for these results using individual multiple-choice input. Do not claim the60FPS target is met until that check succeeds. Larger ripple caches and shelter-map acceleration remain unimplemented alternatives if this local optimization is insufficient.


### Gameplay feedback and rejected field cache

The maintainer tested the rebuilt root library through normal `launch-linux.sh`: wet filmON50–55FPS, OFF60FPS, with the specified rainy Balamb comparison. Process194143 was verified to map the root development library, with no RenderDoc library loaded. The reported range does not establish an exact FPS improvement from the former50FPS baseline, and it does not satisfy the60FPS target.

A standalone GPU prototype then evaluated a1024² RGBA16F ripple-field patch spanning16 procedural cells. It used the actual compact production field, compared analytic and interpolated results, and timed batches of64 frames using GPU elapsed queries, including one rebuild per cached frame. With two1920×1006 field draws per simulated frame, direct evaluation cost about0.161–0.164ms; rebuilding and sampling the cache cost about0.180–0.208ms. Sampled maximum height error was0.08575 and gradient-component error5.859. This is an isolated field fixture, not a native-frame result, but it fails the prototype’s cost and quality criteria. The cache was rejected and never integrated. Files remain clearly separated under `/tmp/nwn-film-cache-prototype`. The normal test game had already closed before the timed run; no pause was applied.

Next hypothesis: exact conservative min/max bounds for the existing shelter depth map could bypass its16 comparisons in fully exposed or fully sheltered regions, retaining the original filter near discontinuities. This is a hypothesis, not an implementation or performance claim. Test it against captured depth data and preserve the post-filter dither, height comparison, sampler semantics and roof-edge behavior before changing production code.


### Rejected conservative shelter-bounds experiment

A prototype used captured4096² D24 shelter data and exact conservative32×32 tile minima/maxima. It retained the original16-sample filter whenever the complete bilinear footprint could not be proved fully exposed/sheltered, including the original final dither. On the captured camera depth, exposure matched exactly at all five tested offsets, with about40% shortcuts at the nominal view; isolated shelter evaluation fell from roughly0.178ms to0.134ms per1920×1006 draw. This did not measure full native-frame savings or per-draw binding overhead.

An opt-in runtime generated bounds once per completed shelter generation, with source/area/scene/size identity and guarded texture/sampler/program/image/PBO state restoration. Mesa and NVIDIA state and reduction tests passed. NVIDIA’s native3.3 context exposes the required ARB compute/image extensions; the3.3 extension path passed too. Full material fixtures checked fully exposed, sheltered and conservative fallback cases. These correctness successes did not establish a performance win.

Native gameplay rejected it: the maintainer reported48FPS where the preceding scene had been52FPS. Process203961 was verified to load the root development artifact with `NWN_SHELTER_BOUNDS_PROBE=1`, no RenderDoc, and a completed256² bounds texture over an8192² source. The entire bounds path, shader branch, draw hooks, module and dedicated regression files were removed from active source. Rejected runtime/test sources remain only under `/tmp/nwn-shelter-bounds-prototype/rejected-runtime`. Do not promote it based on isolated shader timing.

### Resolution mismatch and approved next test

The normal development launcher’s shelter map is8192², while the clean shipping capture used4096². Within each ON/OFF comparison the source resolution was fixed, but captured component timing must not be presented as an exact reproduction of the normal-launch workload. The captured noise resource is1024² BC4, not a large uncompressed texture; the separate shelter depth resource is D24. Normal-launch8192² D24 shelter storage is16 times the pixel count of2048².

The maintainer explicitly approved reducing only the rain roof/blocker map to2048² as a trial, accepting slightly wider wet/dry transitions near roof edges. The world coverage, authoritative blocker capture,16-sample world-space shelter filter, wet-film detail, puddles, private rain and sun-map targets remain unchanged. This test is an internal reversible launch probe, `NWN_WEATHER_SHELTER_2K_PROBE=1`; normal launches retain the original source-size policy. The active target size is `min(g_staticWorldSize,2048)` only with that probe enabled. No UI preference, settings default, game resource or video setting was changed for this trial. Verify the actual target log and root artifact before requesting FPS.


### Live shelter precision control

The2048 trial was confirmed in process205529: the root development library loaded with the probe enabled, and the log reported a completed2048×2048 shelter target with extent128 and authoritative static buckets0+1. The maintainer reported55FPS, compared with about52FPS before the rejected acceleration experiment. Sun shadow resolution stayed8192. This improves the normal scene but does not meet60FPS.

To eliminate repeated restarts and reduce scene/time differences, the temporary launch-only probe was replaced by a persisted live setting:

- Variable: `g_weatherShelterSize`, default0 (Original, follows the existing sun-map size).
- Environment: `NWN_WEATHER_SHELTER_SIZE`, values0,4096,2048,1024; invalid input safely selects Original.
- UI: Wet surface film → Rain/snow shelter map; displays the actual active target size.
- Settings: `weather_shelter_size`, registered through `settings_table(...,kSettingsMax)`; reset0.
- Explicit sizes cap only the precipitation target: `min(g_staticWorldSize,requested)`. They do not resize sun targets or change the detailed film shader. The map is shared by rain/snow, as the control’s label states.
- No existing default changed, so no settings-version/retune bump was made. New setting registration, actual loading and reset behavior passed the settings regression. Development/shipping builds passed.

The maintainer approved testing1024 after the2048 result. The updated game was launched at2048 through the usual launcher, using the new environment key. They were asked to compare2048→1024 live in the same rainy Balamb view, wait15seconds each, and report FPS and roof-edge changes. Validation is pending. The old `NWN_WEATHER_SHELTER_2K_PROBE` and rejected bounds-probe implementation are no longer active controls.


### Eight-sample shelter trial (2026-11-16)

The maintainer requested eight shelter samples. The production exposure filter now uses eight equal-area golden-angle disk positions, normalized by 1/8, retaining the original 0.65-world-unit footprint, world-anchored rotation, bias and dither. This shared precipitation exposure function also serves snow. No MSAA, shelter-map resolution, ripple detail or sun-shadow setting changes. The separate profiling-only equivalent-filter candidate remains opt-in and is not enabled for this trial. Native FPS and roof-edge validation are pending.

The later matched live shelter-resolution comparison found the same FPS with visible appearance changes; the earlier cross-session 2048 result does not establish a gain. The completed native component run measured about 18.1 ms for Detailed versus 15.6 ms OFF in its selected GPU scene interval; shelter bypass saved approximately 1.0 ms, impact bypass 0.5 ms, and noise bypass only 0.07 ms. These are non-additive ablations, not whole-frame FPS measurements.

Eight-sample validation: Linux development and shipping artifacts rebuilt successfully; `python3 check_shaders.py`, the complete Mesa wet-surface GPU regression, and `git diff --check` passed. Native performance and roof-edge appearance remain to be measured in the clean development session.

### Remove wet-film impact rings

Eight-sample gameplay feedback was approximately55FPS. The maintainer then requested removing wet-film raindrop impacts as redundant with private rain/splashes. Removed the impact field call, its noise erasure and its normal-wave contribution from the material body; retained animated noise gradient, sheen, shelter and separate puddle/particle effects. The unused compact helper remains as historical regression evidence, with no production call and no GPU execution. Native ablation previously attributed approximately0.5ms to impacts; this is not a promised FPS gain. Updated field-entry regression requires zero impact entries in every film profile mode while noise and exposure retain their own gating.

Validation: complete Mesa material/field regression and shader checks passed. Field instrumentation confirms zero wet-film impact evaluations with film ON, preserving the moving-noise evaluation; film OFF skips both. Development library was replaced atomically so the existing game can exit safely. A restart is required to load the new artifact.

### Post-impact shelter investigation

Maintainer reported58.4FPS after removing film impacts, versus about55FPS with eight shelter samples and impacts present. They reported no FPS gain from16→8 samples. The active clean log showed three completed shelter captures over area setup/gameplay, not a per-frame rebuild; the cache refresh code is keyed to validity, dirty state, area/scene and movement beyond60% of its extent. Next matched diagnostic sequence is Full → shelter bypass → Full → equivalent shelter arithmetic/LOD candidate → Full (30seconds). This measures the current eight-tap, no-impact workload; old bypass savings cannot be assumed unchanged. Candidate is opt-in and unpromoted.

Current production-injected eight-tap equivalent candidate passed complete material comparison over a depth-edge fixture: maximum colour-component difference6.56e-7 on Mesa and1.80e-5 on NVIDIA RTX3060Ti. No performance claim follows from this quality check. The regression runner now compiles this actual injected shader pair. The candidate rotates the spread basis once and uses explicit level-zero shadow sampling; no new target or draw binding is introduced.

### Scope change: slow Balamb location baseline

Maintainer reports moving the character to another part of Balamb lowers FPS into the40s even with rain, puddles, wet film and fog disabled. Shadows, lighting, transparency handling and other injector work were not disabled. This observation does not isolate native area cost. Pause the equivalent-shelter performance experiment; no candidate has been promoted. First reproduce the same saved slow location/camera/weather/video settings using the exact native executable without any LD_PRELOAD. A temporary launcher lives under `/tmp/nwn-balamb-baseline/launch-native.sh`; it does not alter game resources or persisted settings. Capture FPS and verify loaded libraries before attributing the slowdown. If native is also slow, inspect native CPU/GPU workload; if native recovers, separate remaining injector passes and interception cost. The native visual path differs, so the baseline distinguishes whole-injector overhead, not an individual effect.

Slow-location reference: save `test1`, supplied screenshot `/tmp/codex-clipboard-4afdb82f-28aa-41ed-9876-3d29dcaa8604.png`, camera facing the broad stairs from outside the foreground balustrade. Injector overlay reports51.9FPS/19.27ms; user reports51FPS with rain/puddles/wet film/fogOFF. Other injector features were not disabled. Native comparison remains pending.

Native test1 baseline feedback: maintainer reports60FPS or higher with no injector, versus51.9FPS in the supplied injector screenshot with weather/fog effects disabled. This establishes a whole-path difference, not exact uncapped overhead or a shelter diagnosis. Next matched intervention uses the existing development Performance → Draw shadows at all (master) toggle (`g_receiverEnabled`), which gates receiver/screen copies and principal sun/local replay paths; remaining interposition/weather authority/transparency code is not thereby unloaded. No new rendering optimization selected.

Baseline correction from maintainer: shadow-master OFF yields approximately58FPS. They corrected the earlier approximately53FPS observation because fog had actually been ON. Thus the former approximately52/53 versus native60+ comparison cannot attribute that gap to shadows or non-fog injector work. Request a matched master ON→OFF pair at test1 with fog and all four weather effects confirmed OFF. Native remains reported60+, but present cap/uncapped throughput and paired shadow cost remain unestablished.

Master-switch confound confirmed: `draw_godrays` requires current-frame `g_sceneDepthSerial`, generated by `draw_static_receiver`; masterOFF skips that producer and therefore disables normal fog rendering too. User reports60FPS with masterOFF. This is NOT isolated shadow cost. Next intervention keeps masterON and disables only Fog lighting and sun shafts, verifying effective Pass: off (script overrides can supersede the checkbox). No shadow-only bottleneck attribution is justified yet.

Matched test1 observation: masterON, rain/wet film/puddlesOFF, fog effective Pass: off still reports below60FPS (exact value not supplied). MasterOFF previously reached60. This identifies a remaining cost in the master-gated path but does not distinguish shadow capture, depth/color resolve, composite or related material shadow sampling. Next reversible intervention toggles only Moving casters while masterON/fogOFF; receiver and screen copies remain active.

### Uncapped shadow-stage profiling

With VSync disabled, maintainer reports about59FPS with receiver masterON and both moving/fixed caster togglesOFF, versus179FPS with masterOFF. That is roughly16.95ms versus5.59ms presented frame time, but masterOFF also gates dependent effects/material shadow sampling; do not assign the11.36ms difference to the composite alone. Fog must remain explicitlyOFF for the next measurement.

Added opt-in `NWN_SHADOW_STAGE_PROFILE=1` diagnostic in `shadow_stage_profile.inc`. It uses nonblocking GL_TIMESTAMP query pairs in a32-record ring, with asynchronous availability checks and no glFinish, per-stage CPU intervals, a2second warmup and13second sample window. A PID in `/tmp/nwn-shadow-stage-profile.start` arms one15second run; a camera/projection/viewport change aborts it. Nested/non-world scenes are excluded from published samples. Timed overlapping intervals: selected scene total, native scene including injector hooks, final receiver total, depth capture, colour capture, final fullscreen composite draw, fog. Copy/draw probes exclude the earlier local-character prepass. GPU intervals can include scheduling/CPU starvation; they are not hardware-busy counters, and totals must not be added to their components. No settings, drawing commands or GL rendering state are changed. Normal launches remain uninstrumented.

Prepared clean development profiling launcher `/tmp/nwn-balamb-baseline/launch-profile.sh`, with wet-film experiment flags explicitly unset. Native test1 and supplied stairs screenshot remain the reference. Keep VSyncOFF, masterON, caster togglesOFF, fog Pass: off, wet film/puddles/private rainOFF for the first pass. Build and runtime verification remain distinct.

First shadow-stage run completed, PID255454,1920×1006, masterON/fogOFF/movingOFF/fixedOFF,767 accepted world samples. Median GPU/CPU milliseconds: scene16.7844/13.8476; native-with-hooks16.4137/13.6833; final receiver0.3482/0.0915; depth copy0.0737/0.0236; colour copy0.0594/0.0177; composite draw0.2099/0.0075; disabled fog0.0010/0.0005. This disproves final receiver as the dominant interval in this run. Begin-scene queried MSAA0 (not necessarily the later native world FBO sample count). Startup recorded film1/privateRain1/puddles0, but weather logging confirms clear state rain0/snow0, so rain effects were inactive by weather gating; retain this exact state for paired OFF run. Paired masterOFF requested, not yet collected. CPU native13.68ms warrants inspection of per-draw hooks and driver stalls as well as fragment work; GPU intervals alone cannot distinguish them. Receiver-tag path attaches/detaches private MRT and queries/restores draw buffers per eligible draw; it is a hypothesis, not a measured culprit.

Paired masterOFF completed with2441 samples: sceneGPU2.8744/CPU4.6634ms; nativeGPU2.8672/CPU4.6012ms; receiverGPU0.0010/CPU0.0004ms; depth/colour/composite absent; fogGPU0.0010ms. Same logged viewport/settings, except receiver0. The master-gated cost occurs predominantly in native scene hooks/material behavior, not final receiver. Next opt-in ablation accepts `PID 1` in the stage start marker to bypass only begin_sun_receiver_tag for15seconds, restoring the diagnostic flag on completion/abort. This intentionally removes current-frame receiver classification; never promote bypass as an optimization. Normal starts (`PID` or `PID 0`) retain tagging.

Same-session tag isolation (PID256558): bypass2144 samples, sceneGPU3.5430/CPU5.3941ms; nativeGPU3.1601/CPU5.1379ms; final receiverGPU0.3584/CPU0.0892ms. Restored normal baseline759samples, sceneGPU16.9267/CPU14.5903ms; nativeGPU16.5642/CPU14.3150ms; receiverGPU0.3441/CPU0.0923ms. This strongly isolates begin/end receiver tagging plus its MRT configuration as the dominant path; it does not yet distinguish attachment churn from tag writes themselves. Native tag log verifies sourceFBO2,1920×1006,8samples; begin-scene MSAA0 refers to a different framebuffer and must not be misreported as gameMSAA0.

Opt-in candidate mode2 holds only the verified-unowned reserved tag attachment across one native selected-scene pass, restores draw buffers after each draw, and removes its attachment before final receiver/postpasses. Cleanup preserves current draw-FBO binding, does not touch read-FBO state, verifies texture identity before detaching, and avoids resurrecting deleted native FBOs. It keeps tag format/depth span, actual sample count, native coverage and per-draw engine-derived creature/highlight classification unchanged. Default remains original per-draw attachment lifetime. Candidate is diagnostic-only until native performance and visual validation. State regression verifies32draws→2attachment changes, one clear, restored draw buffers/FBO/masks, engine replacement protection and deleted-FBO handling. Existing receiver tag GPU cases and shader checks pass on Mesa; native candidate runtime remains pending. No irreversible architecture decision or default change made.

Batched-attachment candidate mode2 completed in native process258234 with1110 accepted samples: sceneGPU10.3455/CPU10.6626ms; nativeGPU9.9564/CPU10.4065ms; receiverGPU0.3799/CPU0.0876ms; depth0.0727ms,colour0.0993ms,composite0.2028ms. MasterON/fogOFF/movingOFF/fixedOFF,1920×1006. Maintainer observed61FPS before arming (normal tagging). Candidate normal state restored automatically. Compared with prior normal16.9267ms this is approximately6.58ms lower GPU scene interval, but same-process normal baseline and visible correctness feedback are pending. Do not promote yet; full fog/rain/caster regression still required.

Same-process normal baseline after batch candidate (PID258234) completed796samples: sceneGPU16.1444/CPU13.9405ms; nativeGPU15.7614/CPU13.6780ms; receiverGPU0.3400/CPU0.0895ms. Candidate reduced scene interval by5.7989ms (~35.9%) and CPU3.2779ms in this comparison. Maintainer could not assess visual differences during the brief run; caster options wereOFF, so shadow correctness is explicitly unvalidated. Prepared Linux-only `NWN_SHADOW_TAG_BATCH_PROBE=1` clean preview launcher, no stage profiler or wet-film probes, to keep batching enabled while moving camera and examining actual shadows/highlights. It is an internal temporary experiment, no public setting or default changed.

### Linux batching acceptance

Maintainer reports the clean continuously batched preview looks correct after the requested caster/hover/camera/fog check; they did not supply FPS. Linux now defaults to the validated batched attachment lifetime; Windows retains per-draw lifetime pending platform-specific runtime evidence. Internal Linux `NWN_SHADOW_TAG_BATCH_PROBE=0` can restore the legacy path for A/B. This is an implementation detail, not a user-facing quality setting or persisted default; no settings retune is warranted. Profiler modes restore the launch configuration on completion/abort. The running clean preview already uses the same batching behavior; no restart is needed merely to collect the remaining full-effects FPS. Rainy detailed-film/full-resolution-fog60FPS remains unconfirmed.

Linux default build verification: development and shipping artifacts rebuilt after promotion. The full sun-creature/tag state and GPU regression passed again, including legacy fallback, batched attachment lifetime, engine-owned/deleted attachment handling, indexed masks, highlights, depth agreement and8sample A2C surfaces. `git diff --check` passed. Source normal Linux default and current opt-in preview execute the same batching path. Requested full-fog test1 FPS and rainy detailed-film/puddlesOFF FPS; results remain pending.

### Maintainer full-effects acceptance — 2026-11-17

Clean Linux preview with batching continuously active and no stage/wet-film profiling (process259558, root development library, `NWN_SHADOW_TAG_BATCH_PROBE=1`) passed the maintainer's visual check. In the requested test with both caster optionsON and full-resolution fogON, the heaviest location previously in the40s now reports72FPS. With detailed rainy wet filmON, puddlesOFF and full-resolution fogON, the original approximately55FPS location now reports80FPS; the heavy location previously in the40s reports69FPS. These are maintainer-reported gameplay rates, distinct from the asynchronous GPU scene intervals. The requested60FPS target is met in these reported views; this does not establish a universal area/game minimum. Runtime acceptance uses the same batching behavior now enabled by default in the rebuilt Linux artifacts. No additional restart is necessary merely to use the current preview. Windows remains on the previous attachment lifetime pending its own runtime validation.

Accepted explanation: costly per-draw receiver-tag attachment churn, rather than final shadow composite or rain-shelter resolution, dominated the investigated slow view. Holding the reserved tag attachment during one native scene pass preserves classification data while removing most attachment mutations. The matched scene interval improved16.1444→10.3455ms; the final composite remained approximately0.2ms. Shelter16→8 alone had no observed FPS benefit; film-impact removal separately improved the reported rainy view to58.4FPS before the tagging fix. Retain these distinctions when describing the improvement.
