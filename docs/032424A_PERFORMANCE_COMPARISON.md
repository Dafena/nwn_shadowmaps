# Exact baseline audit:032424a5 versus current

2026-11-17, source-only. Requested commit:032424a5ef4ea07bedaffc3658ed25ff61143023, Merge pull request5,2026-11-09. Local Git contains and verifies this complete object ID. GitHub page fetch failed; comparison uses the exact local Git object, not a nearby inferred baseline. Current means ecfe847 plus the uncommitted test2 diagnostic/weather experiment changes. Runtime testing remains paused.

## Changes by stage

| Range | Meaningful renderer changes |
| --- | --- |
|032424a→ac076eb|Drawing probe modules, frame/material/UI-input/draw hooks, optional texture upload observers and optional token hook.|
|ac076eb→3759624|Enable previously parked volumetric fog; rewrite march; add heat haze, script overrides/custom-token controls; change private rain depth occlusion/splashes.|
|3759624→12757b0|Linux tag attachment batching, simplified wet film/no impact rings,8sample shelter filter, weather controls, private rain default retune, settings0 semantics correction, opt-in profilers.|
|12757b0→working tree|Dedicated diagnostic selection lease, per-draw/bucket timing, dry-weather upload skip experiment; normal-path integration of these candidates pending.|

## What the requested baseline already contains

032424a already has automatic wet film/puddles/snow, the full per-draw weather upload payload,8sample-native-compatible receiver tagging, local-light selection/capture/replay, separate local receiving/casting settings, and opt-in private rain. These were not introduced by the fog commit or this chat.

shadow_local_lights.inc and shadow_replay.inc are identical between032424a and current working tree.032424a→3759624 also leaves weather_runtime.inc and shadow_receiver_tags.inc unchanged. Current weather observer overhead~1.8ms and local replay~4.9ms therefore correspond to existing paths, not proof of newly added implementation cost.

## Critical fog distinction

032424a has kGodraysFeatureAvailable=false: fog cannot be enabled, even through saved settings. Current has feature available=true, initially OFF, with script-aware enable gates. FogON adds real functionality absent in032424a. In test2 it measured~1.9ms GPU; it does not explain the whole sceneCPU gap. Heat is also new, defaultOFF. Both fog/heat return before their GL passes when their effective script/player setting is OFF. Fog/heat shader implementations have no further changes after3759624.

## Added disabled-feature overhead

Drawing frame/material/draw/input entrypoints now exist, but ordinary expensive drawing work returns on a cached NWN_DRAW_PROBE opt-in check; texture observation installs only with that opt-in. Native texture upload/deletion interposition still forwards even if observing is disabled. No evidence yet attributes a multi-ms normal-frame regression to these tiny gates; an exact old/current comparison is required.

Script controls are enabled for token listening by default and add mutex-protected effective-value reads. Token parsing occurs on token messages, not a per-draw scan. Fog/heat/private-rain gates perform effective-setting reads even while player options are OFF. The recorded test2 log reports no safe token trampoline; do not assume an area script was actively overriding settings. No script override is established as a slowdown cause.

Diagnostic-only ownership wrappers/probes in the preview are a separate artifact from the normal library; benchmark phases share that overhead. Normal/shipping promotion is still pending.

## Verified configuration discrepancies

Earlier records show Low25ms local update versus recent Ultra0ms/every-frame. Pre-clean saved snapshot budget1 versus recent3; test2 engine list offers2 lights and captures2 while enabled. Map size1024 appears in both earlier and recent loaded-settings logs;256 is a launcher default, not a verified earlier effective size. The snapshot is from within the chat, not an untouched032424a session. Who/when changed these settings is unknown.

The local receiving control can beOFF while capture isON in both versions. Verified test2 local_cast1/local_receive0 replayCPU4.8665ms; actual local_castOFF replay0.0003ms and sceneCPU16.6673→12.0477ms. This existing settings separation explains measured wasted work in the tested configuration, without proving it caused the historical old/new difference.

032424a treats NO_SETTINGS=0 as ignore-settings; current correctly loads settings with0. Exact same env may therefore apply different effective map/cadence/source/effect values. Normal launch-linux.sh unsets that variable and runs unchanged run-dev.sh, so this correction alone does not explain a change under that normal launcher. Settings version16→19 also changes retuning/migration behavior; do not reuse the current INI as proof of historical032424a settings.

## Changes which plausibly affect GPU state/shader cost

Tag attachment lifetime changed from per-draw to scene-batched in normal Linux checkpoint12757b0. Dedicated preview also leases draw-buffer selection across compatible draws. Both preserve engine per-draw classifications and8sample target; measured gains are real but do not rule out untested state interactions.

Weather GLSL changed during the chat: noise derivative simplification, impact rings removed,16→8 shelter taps, added film/puddle gates and reflection trace-step budget. These reduce explicit work, but changed shader structure may affect driver compilation; it cannot be exonerated by source inspection alone. Rejected reduced-resolution reflection FBO and shelter-bounds experiments are removed.

Private rain budget2048→4352/radius28→40/splash1→.250; default remainsOFF. These changes cannot explain private-rainOFF test2.

## Required controlled comparison

No matched runtime measurement of exact032424a versus current exists. The prior older-build test used12757b0, which already includes chat changes. Next test when authorized to resume must use exact032424a against current with fog/heatOFF (baseline cannot render them), matched actual local capture/receive/cadence/source cap/map settings, same test2 camera/video/weather and clean instrumentation. Build only from root checkout and preserve current source/INI. A current NO_SETTINGS=0 invocation cannot be blindly reused against032424a. If a repeatable regression remains, separate drawing+fog-merge commit3759624 from chat changes, then isolate tag attachment lifetime versus weather shader changes.

No settings, game resources, launcher or accepted source implementation were changed by this audit. Testing has not resumed.

## Historical runtime follow-up

Exact032424a libraryca8ba11f953815ab4e2bd6efac34ff4280003e17245dd5c5c01b4121cfb84310, PID344900, verified in mappings; copied isolated settings at/tmp/nwn-032424a-test, NO_SETTINGS unset to load them. User reported it ran very badly; no numerical FPS was supplied.

Exactb47f9805bdc45291f3b62b7a153eb83fd614c2f6 library12f66f80b92e4610a19388c9eddbe8959c1cf1c71b8fa0f28a2a10573afac4c3, PID345622, mappings verified, isolated settings copied from prior test. User reported noticeably faster, approximately40–60FPS. User instructed same test2 camera, VSyncOFF, masterON/castersOFF/local-castOFF. These are human-reported comparisons, not automatic matched frame traces; precise delta is unknown.

Immediate successor0cc705d (parentb47f980) adds the208line shadow_receiver_tags.inc and per-draw shader/tag begin/end interception, along with layer scheduling and local capture/fade changes. This is before private-rain merge6fb689b and fog3759624. Next historical test isolates that direct successor commit; no single feature has yet been proven the regression cause. Earlier statement that local code was unchanged refers032424a→current, not b47f980→032424a (which does change local capture/fade mechanics).

### Direct successor historical test

Exact0cc705d built from root, SHA256bcd595204ba6b493fde8a154b5a8f2df534df2d3d95a597c50c92a9dfc22ed1d, PID346378 verified in mappings. User reported below40FPS versus b47f98040–60FPS. This is a coarse human comparison without exact matched timings, so0cc705d is a candidate first regression commit, not a conclusively proven performance boundary.

Post-run isolated settings in BOTH b47f980 and0cc705d show master1, moving1, fixed1, local_cast1/local_receive0, quality2/sources3/map1024. Thus these runs did not establish the requested stripped-down controlsOFF, although saved states match each other. Preserve their actual configuration for the next one-variable diagnostic rather than silently changing it.

Prepared separate historical attribution artifact: exact0cc705d plus only an opt-in early return from begin_sun_receiver_tag. Shader injection and other new layer/local/weather changes remain intact; receiver falls back to its existing no-tags handling. Required classification is intentionally absent, so this variant may render incorrect shadows/highlights and is never a valid visual fix. It is separate from normal/candidate libraries and source modifications are restored after building. Next runtime test pending.

### Tag-capture isolation recovered parent performance

Historical0cc705d plus only opt-in begin_sun_receiver_tag early return, SHA25675dba1b5f9965a19119538a7b3bc208f7416c089e1515ef9700e9ee00b453b85, PID346924 verified in mappings. Log confirms history-tag-probe capture bypass active. Same copied settings/no requested changes; user reports back around40–60FPS, versus below40 for exact0cc705d and40–60 for parentb47f980. This is strong one-variable evidence that native receiver-tag capture/interposition introduced at0cc705d accounts for a substantial portion of the historical drop. Coarse ranges/camera reconstruction prevent quantifying exact loss. It does not identify which particular tag GL operations dominate; earlier matched current experiments separately implicated attachment/selection transitions.

The shader tag output remains injected in this variant, while MRT setup/capture/classification is bypassed. Thus recovery points to the active tag capture path rather than requiring removal of the entire changed shader or layer system. Missing data uses the existing no-tag receiver fallback and may break creature filtering/highlight behavior; this is not an acceptable shipping fix. No source rollback or production change performed. Parent40–60 still below native122–145 indicates additional injector cost predates tagging.

### Current optimized preview matches parent FPS range with tags retained

PID347438, isolated historical comparison settings copied under/tmp/nwn-tagging-fix-test. Only copied file schema version set19 to avoid migration/retuning while preserving existing values; original game settings untouched. Current dedicated preview selected via markerPID+2. Log verifies native receiver tags live atFBO5/1920x1006/8samples,72verified lease slots, candidateON/inactive_weather1. No historical tag bypass is enabled. User reports40–60FPS, same coarse range asb47f980 and diagnostic0cc705d tag-bypass, versus below40 exact0cc705d. This supports recovery of the historical tagging regression in this scene without removing tag data. It does not establish exact FPS parity or a visual pass (user answered FPS range only). The older-code baseline still incurs substantial cost versus reported native122–145FPS. Normal/shipping integration and dry→wet runtime transition validation remain pending.

Maintainer subsequently confirmed test2 shadows, hover highlights and transparency look correct under camera movement in the current combined preview. This closes the test2 visual check for the tagging recovery; dry→wet runtime transition and normal/shipping integration remain separate outstanding work.
