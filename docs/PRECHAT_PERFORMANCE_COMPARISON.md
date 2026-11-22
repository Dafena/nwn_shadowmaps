# Pre-chat versus current performance audit

Source-only investigation, 2026-11-17. Runtime testing remains paused at maintainer request. No game launch, GPU workload, settings edit or source rollback was performed for this audit.

## Reference versions

- `ac076eb`: immediately before the fog/heat/script-controls commit.
- `3759624`: fog/heat/script-controls commit, November16; main/origin/main and the pre-chat source baseline. Existing rain/weather code had already been merged.
- `12757b0`: first checkpoint created during this chat, **not** the pre-chat version.
- `ecfe847`: buffer-selection diagnostic candidate checkpoint. Current working changes add test2 CPU attribution and the diagnostic inactive-weather experiment.

The previous test described as the older checkpoint used12757b0, not3759624. It established that the new buffer-selection candidate was not solely responsible for test2, but did not establish whether3759624 was faster there. No matched3759624 versus current test2 run exists yet.

## Findings

### Runtime configuration changed

Historical capture logs (`/tmp/nwn-renderdoc/game.log`, `/tmp/nwn-native-wet-film-profile/game.log`, `/tmp/nwn-balamb-baseline/batch-preview.log`) show Low local update cadence25ms. Recent test2 logs show Ultra0ms, meaning a capture may be staged every frame. The saved pre-clean snapshot `/tmp/nwn-clean-development-probe/settings-before-clean.ini` contains local_cube_quality0/source budget1/local_map1024. Current settings contain quality2/source budget3/local_map1024 (casting subsequently disabled for isolation). Test2 logs show engine-authoritative candidates2/cap3/capturing2; Balamb logs show candidates1/cap3/capturing1. Scene source counts differ naturally and are not a new selection algorithm.

The snapshot establishes settings drift within this investigation; it is not an untouched pre-chat configuration. The records do not establish who changed those values or when.1024map size is present in both earlier loaded-settings logs and recent tests; the launcher defaults to256, so do not assert an actual256→1024 historical transition without a matching old run. Four times the edge length would mean16times the texels, not16times the frame cost.

Measured test2 local replayCPU4.8665ms with live local_cast1/local_receive0; actual captureOFF reduced replay to0.0003ms and sceneCPU16.6673→12.0477ms. Turning OFF receiving alone does not disable capture in either old or current code. This is a real existing cost/configuration trap, not a newly introduced local-capture implementation.

### Settings-loading behavior changed

3759624 treats any presence of NWN_SHADOWMAP_NO_SETTINGS as skip-settings, including string0.12757b0 fixes0 to load saved settings. Both old and current clean launchers document/use0 as the load-settings override, so that invocation now applies saved map size, cadence, source budget, effects, etc., where old code would ignore the file. A source comparison with the same literal environment can therefore run different effective settings. This is a plausible old/current performance difference and must be controlled in the eventual A/B.

The existing local launch-linux.sh unsets NO_SETTINGS and invokes run-dev.sh; run-dev.sh does not set it. Thus the0-flag correction does not by itself explain a difference under that exact normal launcher. This script was not edited.

### Code unchanged since fog merge

- shadow_local_lights.inc and shadow_replay.inc are identical between3759624 and current working source. Local selection still follows NWN authority.
- godrays_runtime.inc, shadow_heat_haze.inc and shadow_script_env.inc are identical since3759624.
- Local capture/replay, per-draw weather payload and receiver tags already existed before this chat, and are unchanged by ac076eb→3759624.
- Transparency implementation changes since3759624 are diagnostic ownership coordination only; normal builds compile the callback away. No new Mode3 activation.

3759624 itself enabled the previously parked fog feature (still opt-in), rewrote its march, added heat haze/script controls and modified private rain/splash behavior. Comparing againstac076eb with fog active would compare different feature availability, not simply a regression in the same effect.

### Code changed during this chat

- Accepted normal Linux scene-lifetime receiver-tag attachment batching; former attachment changed every draw. Matched Balamb selected-scene interval improved~5.8ms. Native tag writes,8sample target and per-draw classifications remain. This alters GL state lifetime and remains a genuine candidate to test for scene-specific regressions despite Balamb gains.
- Diagnostic-only longer draw-buffer selection leases: matched test2 full-effects old checkpoint~38.4ms/candidate~24.0ms. It helps this scene; cannot establish behavior in every map.
- Wet-film noise simplification, impact-ring removal, shelter filter16→8, optional puddle/film controls and10/5/3 trace steps. Shelter size default remains Original; failed low-resolution reflection-buffer and bounds-map trials were removed. Some shader branch/uniform additions could affect driver compilation; source inspection alone does not exclude a scene-specific shader regression.
- Private rain defaults2048→4352drops,28→40m, splash1→0.250. Private rain remains defaultOFF; larger particle budget can matter only when its path is active. It cannot explain the stripped-down test2 case with private rainOFF.
- New profiler/experimental hooks: mostly compile-time diagnostic-only; stage and wet-film profilers opt-in. Recent preview runs use the diagnostic library, not the same artifact as normal launches. Benchmark comparison phases share instrumentation overhead. The cleaned launcher removed forced verbose local-light tracing.
- Dry-weather upload skip is diagnostic-only, saves1.78–1.97ms in matched dry test2; wet/snow runtime transition check pending.

## Artifact distinction

The current normal libnwn_shadowmap.so was rebuilt from12757b0 for the earlier comparison and matches original SHA25637d680197cae15a8e408166627e72435f583446ca200085de416059e40eb7edd. The source working tree is newer; the dedicated libnwn_shadowmap_tag_bench.so contains the preview candidates. Neither artifact is a fresh3759624 baseline. Normal/shipping integration of the new candidates remains pending.

## Next matched comparison when testing resumes

Build exact3759624 and current normal/candidate behavior sequentially from the root checkout, preserving the working tree and settings. Comparetest2 with the same native executable/camera/weather/video settings and **verified effective** local cadence, source cap, map size, receiving/casting flags, fog, film/puddles/rain and diagnostics. Use either an unset NO_SETTINGS with an isolated identical saved configuration, or explicit skip-settings in both with fully matched defaults; do not use0 blindly across versions. Measure completed frame medians/p95 and actual scene/source counts. Test pre-chat versus current with scene attachment batching disabled if the first pair establishes a regression; then separate weather shader changes. Do not restore an old persistent INI or lower image quality silently.

Conclusion: code history does not yet prove the current source is slower than the actual pre-chat merge. Effective-setting drift is verified and is the strongest concrete discrepancy found. The real3759624 matched comparison remains necessary before attributing the gap to a specific code change.
