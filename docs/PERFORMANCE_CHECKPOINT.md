# Linux Balamb performance checkpoint — 2026-11-17

Baseline repository commit: `3759624`. This checkpoint records the working Linux renderer before further tagging optimizations. Detailed investigation and limitations: [WET_FILM_OPTIMIZATION.md](WET_FILM_OPTIMIZATION.md).

## Accepted state

- Linux batches the receiver-tag attachment for one native scene pass, restores draw buffers after each draw, and removes its attachment before postprocessing. Windows retains the former per-draw lifetime.
- Wet film is ON/OFF; detailed moving sheen remains and wet-film impact rings are removed. Puddles/reflection/refraction and private rain remain independently controlled.
- Shelter filter uses eight disk samples. Reducing sixteen to eight did not produce an observed FPS benefit; do not attribute the measured tagging gains to this change.
- Existing private-rain defaults:4352 drops,40m radius,0.250 splash strength; private rain and native-rain hiding remain opt-in.
- Timing tools are opt-in diagnostics. Normal launch needs no profiling flags or temporary preview script. Existing local `launch-linux.sh` is intentionally excluded from the checkpoint.

## Evidence

Matched native test1 scene GPU interval: normal per-draw attachment16.1444ms, batched10.3455ms (approximately5.8ms saved). The final receiver remained about0.35ms; bypassing tagging is diagnostic only and is not the accepted implementation. All values are selected-scene intervals, not full presented-frame or hardware-busy timings.

Maintainer reports correct visuals while checking shadows, hover highlights, transparency, camera movement and full-resolution fog. Heavy full-fog spot:40s→72FPS. Rainy detailed wet filmON/puddlesOFF/full-resolution fog: former55FPS view→80FPS, heavy40s spot→69FPS. The requested60FPS target is met in these tested views, not guaranteed across the entire game.

Linux development/shipping builds, shader checks, wet-surface GPU regressions and receiver-tag state/GPU regressions passed during this work. Tag tests cover masks, per-draw classification, cleanup, engine-owned replacement/deleted-FBO protection and multisample coverage. Windows batching runtime validation is not claimed.

## Built artifacts at checkpoint

Build outputs are ignored by Git; rebuild from this root checkout. SHA256 of the validated source build artifacts:

- `libnwn_shadowmap.so`: `37d680197cae15a8e408166627e72435f583446ca200085de416059e40eb7edd`
- `libnwn_shadowmap_deploy.so`: `a44cb513c1d947f1906de7326fc610b43af89d6a6e9afd9b8378dc2c50dac7b6`

## Next investigation

Profile remaining per-draw draw-buffer transitions, state validation and multisample tag writes. Preserve this checkpoint, native coverage, engine classification and exact GL state restoration. No further optimization is included or claimed here.
