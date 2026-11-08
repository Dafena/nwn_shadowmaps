# Release integration audit — 2026-11-08

## Approved scope and acceptance

Baseline: `b47f9805bdc45291f3b62b7a153eb83fd614c2f6` (main before local integration).
Review command: `git diff b47f980...HEAD`.

The maintainer approved a whole-plugin audit of pending changes, incorporation
of rain PR #4, cleanup of rejected active experiments while preserving useful
findings, and an implementation of continuous-alpha layer support for Windows.
Preserve accepted Linux behavior, authored geometry/materials, saves and installed
game assets. Keep new rain particles and native-rain hiding optional; existing
weather defaults must not change merely by loading the update.

Publish one integration PR rather than directly updating main. Require builds,
automated regressions, actual Linux/Proton visual and frame-time comparisons for
Type-0 blending/torch lighting, ordinary areas, weather and area transitions.
Native Windows verification remains explicitly pending (maintainer has no
native Windows test access for this run); do not call a DLL build native parity.
Proton exercises the Windows path but is not native Windows driver certification.

## Integration record

- Current local source and untracked code/tests were snapshotted before changes.
- Accepted local changes checkpoint: `0cc705d`.
- Rain PR4 commit `74f706a` merged with conflict resolution: `6fb689b`.
- Personal absolute-path launchers and generated assets are not release sources.

## Audit findings and fixes

### Standards review

- Fixed first-frame receiver-tag clears overwriting unrelated indexed colour
  masks. The GPU regression now starts with distinct masks and verifies all
  are restored.
- Replaced process-lifetime layer discovery (4096-entry ceiling and linear
  lookup on ordinary draws) with scene-local maps and a bounded32-scene LRU.
  Only visits to that same scene age its entries; UI/portrait renders do not.
- Added the layer module and policy header to Windows incremental dependencies.
- Restored both platform Draw adapters on shutdown, including ownership-checked
  restoration of the Windows virtual slot and page protection.
- CI validates Linux host builds without distributing them as portable shipping
  binaries. Linux releases still require `make portable` per BUILD.md.

### Specification review

- A material losing layer opt-in during the first queued submission no longer
  receives a duplicate native blend on its second submission.
- Windows now handles native GetFirstItem consuming item0. Its unique virtual
  Draw-slot adapter avoids the unsupported prologue trampoline; missing or
  ambiguous adapters leave native drawing in control.
- Removed the rejected per-layer A2C depth-copy experiment from active sources.
  Standard Mode2 A2C remains independent; parked OIT remains parked.
- Preserved original geometry, UV2.x alpha, scrolling, colours, priority groups,
  native solid depth tests and ordinary unmarked materials. No export coverage
  duplicates, depth copies, new layer editor or inferred mesh priorities.
- Rain PR #4 is merged into this branch. Both new rain features default off.

The standards and specification reviews were independent read-only reviews.
Their reported code findings were corrected; the release gates below remain
explicitly open. No installed game resource or proxy binary was replaced.

## Build and automated results

All commands passed on the final rendering source:

```bash
make -j2
make -j2 deploy
make -C win -j2 verify NWN_WIN_EXE=/path/to/bin/win32/nwmain.exe
python3 check_shaders.py
make test-weather
for test in tests/run-*-regression.sh; do bash "$test"; done
```

The DLL exposes17/17 proxy exports and resolves57/57 mapped exports in the tested
NWN89.8193.37-17 executable. Weather tests compiled/drew/read back real Mesa GL4.6
and forced3.3 contexts, including depth, alpha, state, shelter and area gates.
GPU receiver-tag, caster, contact-depth, local-light-fade, A2C ownership and layer
scheduling regressions passed. Layer tests include >5000 identities, intervening
GUI scenes, scene retirement, equal priorities and lost opt-in.

Final local Windows DLL SHA256:
`05f08c327d791da2084f88f7ca3a7d5db296ca24a8193d02bb69d601906ae8dd`.
Executable SHA256:
`3b7cb1252e0edb2ce22d7971f333aade027039ae30a45b4bc64732c3e6bec73a`.
These identify the tested binaries, not a promise of compatibility with another
engine build. The workflow runs the same regression suite on pull requests;
remote CI status must be checked separately from these local results.

## Actual game checks

Host: Linux, NVIDIA RTX3060Ti, driver615.78.08,1280×800. Layer scenes used the
accepted Type-0 HAK in private profiles. Native Linux used the accepted baseline
and candidate development libraries; the stock weather fixture used the shipping
library through `run-shadowmaps-clean.sh`. The Windows shipping DLL was copied
only into a private game staging directory and launched with Proton Experimental
and `WINEDLLOVERRIDES=version=n,b`. Installed assets and unrelated Wine processes
were preserved.

| Check | Observed result | Limit |
| --- | --- | --- |
| Linux Type-0 baseline/candidate | Matching lava/shoreline views, real inventory torch response,56 ordered core/fade draws; camera delta≤4.77e-6, projection identical | Selected matched views, not exhaustive map certification |
| Windows DLL through Proton | Unique virtual Draw slot active at RVA0xd883d0; explicit layer module active; lava/shoreline and held-torch views inspected | Proton is not native Windows |
| Linux ordinary stock area | Stock ttr01 scene rendered with shipping full effects | Small fixture with exposed edges/water |
| Linux weather/area changes | In one game: clear→rain→snow→interior→clear; private rain ready, native rain retained; interior immediately cleared rain0.058/snow1.000 | Native-rain hiding was deliberately off |
| Proton stock/interior | Stock geometry rendered; interior area flags observed | Rain/snow startup fixtures stayed clear: precipitation was not exercised |

MSAA on the actual layer receiver target was16 samples. A separate read-FBO
query reported0 samples and is not the active-target measurement. First Windows
trials that refused the prologue hook, incorrect fixtures and timing-overlay
startup failures were retained as failed evidence and excluded from passes.

## Performance observations

The matched Type-0 windows each contained599 presentation intervals over10s,
with adaptive VSync near60Hz:

| Native Linux | Mean ms | Median ms | P95 ms | P99 ms |
| --- | ---: | ---: | ---: | ---: |
| Accepted baseline |16.6663|16.6762|30.4723|30.8941|
| Integration candidate |16.6667|16.6678|30.5103|30.9369|

These are presentation-capped pacing measurements, not uncapped GPU throughput
or proof of a universal performance improvement. Final Windows-path steady Wine
presentation samples were approximately59.48–60.55FPS, also capped. The small
Linux stock clear-return fixture averaged5.4124ms (P95 7.3806ms,923 intervals),
without a matched baseline, so it is not a regression comparison.

## Open acceptance gates — keep integration PR draft

- Native Windows visual/driver validation is pending by maintainer choice.
  Follow [Windows validation checklist](WINDOWS_RELEASE_CHECK.md).
- Integrated Windows/Proton actual rain/snow transitions and optional native-rain
  hiding still need an exercised rainy/snowy session. A clear-weather startup
  is not evidence for those paths.
- Rain budgets2048/32768/262144, shelter/VFX interaction and moving-camera stress
  need representative in-game frame-time checks. Maximum budget is not a
  performance guarantee.
- Crowded multiplayer snow trails, broad-area fog/night/light comparisons and
  save/load transitions remain acceptance tasks, not claims supplied by unit
  tests or selected captures. Existing offscreen-caster/local-contact limitations
  and deferred second-story strobes remain documented in AGENTS.md.

The maintainer's local evidence directory is
`WorldMaker/reports/plugin-release-audit-20261108/`: JSON summaries, matching PNGs,
raw presentation intervals, engine logs, scripts and pre-audit snapshots.
Generated game resources and screenshots are not embedded in this source PR.
A maintainer can review and reproduce the code now; do not mark the combined
release fully certified or merge solely because compilation/CI is green.
