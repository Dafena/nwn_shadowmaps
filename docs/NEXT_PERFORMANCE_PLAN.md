# Next Linux performance investigation — confirmed

Protected checkpoint: `12757b0` on `codex/balamb-performance-checkpoint-20261117`. The maintainer confirmed the complete plan on2026-11-17.

## Settled decisions

- Target at least1ms of repeatable frame-time improvement with checkpoint visual quality preserved.
- Use only the two established Balamb views for now. No additional area is an acceptance requirement.
- Keep an optimization if it saves at least1ms in either view, with no repeatable slowdown or added stutter in the other, and correct visuals in both. Both views need not improve.
- Preserve the existing full-resolution fog, actual8sample MSAA/transparency coverage, shadows, engine-derived classifications and GL state-restoration contract. Use matched effect/weather/video settings within each comparison.

## Evidence and open hypotheses

Accepted batching reduced a selected scene GPU interval16.1444→10.3455ms. Whole-scene intervals do not establish which remaining operation dominates. The current profiler needs finer diagnostic separation before selecting another optimization.

Read-only source audit found repeated per-draw capability queries, texture-binding queries/restores on the no-allocation path, draw-buffer selection/restoration, framebuffer validation and classification uploads. Context capability caching and no-allocation fast paths are lower-risk hypotheses. Broader draw-buffer/state caching requires explicit ownership and invalidation evidence; bucket scope alone does not prove it safe. Tag classification must remain per draw. Reducing tag contents or MSAA coverage is outside the visual contract.

## Proposed execution

1. Preserve checkpoint12757b0; do not change the local launch script or game resources. Record the currently active artifact, launcher, video/effect/weather settings and GPU/driver conditions for each run.
2. Extend opt-in diagnostics to distinguish remaining tagging CPU/state work from draw-buffer transitions and multisample tag-write cost. Pair actual uncapped frame-time measurements with CPU/GPU stage timings; a selected GPU interval alone does not satisfy the1ms frame-time target. Do not force synchronization or change rendering quality to make measurements easier.
3. Start with redundant-work candidates: context-scoped immutable capability checks and the successful-allocation fast path. Treat them as hypotheses; measure before claiming a gain. Test changes separately where attribution would otherwise be ambiguous.
4. Consider broader state/draw-buffer changes only if measurement justifies them and all relevant native/injector mutation, object lifecycle and restoration boundaries are accounted for. Preserve per-draw engine classification, native MSAA/discard/A2C coverage, tag contents and exact state restoration. Diagnostic bypasses are never accepted as visual optimizations.
5. Compare checkpoint/candidate/checkpoint in matched, stationary, uncapped runs at both Balamb views. Record frame-time median and tail behavior as well as CPU/GPU diagnostics. Repeated comparisons must distinguish a repeatable gain from noise. Restore full shadow/caster/fog/weather settings for acceptance even if intermediate diagnostics temporarily isolate work.
6. Retain only a change meeting the agreed1ms/no-regression rule and visual checks covering shadows, highlights, foliage/character transparency, camera movement and relevant wet-film/fog behavior in these two views. Reject/revert failed experiments. Document results and rebuild the Linux development/shipping artifacts that will actually run.

## Confirmation status

The maintainer confirmed: Yes—start this investigation. All interview decisions above are settled.

## Documentation policy

Canonical terms live in `GLOSSARY.md`; implementation details and measurements stay here and in `WET_FILM_OPTIMIZATION.md`. No ADR is warranted yet: no hard-to-reverse architectural choice has been made.

## Instrumentation and first candidates

Investigation branch: `codex/tag-work-investigation-20261117`; checkpoint branch remains protected. `NWN_TAG_WORK_BENCH=1` is opt-in and does not persist. The executable's measured SDL_GL_SwapWindow thunk is a writable indirect jump slot; the diagnostic verifies the instruction shape and slot writability, forwards each original swap, and restores the slot on shutdown. Completed swap intervals provide actual frame median/p95; they include natural presentation/queue behavior and are not GPU-busy counters. VSync must beOFF. No new synchronization is introduced.

A PID in `/tmp/nwn-tag-work-benchmark.start` arms seven8second phases: checkpoint → texture fast path → checkpoint → capability cache → checkpoint → both → checkpoint. Each phase excludes its first2seconds. CPU draw-group attribution samples one in16 observed draws; estimated group times overlap begin-total and must not be summed as independent totals. GPU scene/native intervals remain available alongside frame metrics. Camera, projection, weather, relevant switches, VSync or focus changes abort and restore checkpoint candidate flags.

The texture candidate skips binding queries/restores only when all existing tag/dummy textures and exact dimensions/sample count are already present; resize, sample changes and missing targets retain normal allocation. The capability candidate caches only immutable MAX_DRAW_BUFFERS, bounded by both current GL-context identity and selected scene serial; unknown contexts query normally. It does not cache mutable draw-buffer/FBO state. Neither candidate is enabled on normal launches or changes sample coverage/tag contents. Native gain is unmeasured.

## First measured outcomes

See [TAG_WORK_PROFILING.md](TAG_WORK_PROFILING.md). The no-allocation fast path, capability cache and their combination failed the1ms criterion. Diagnostic repeated-validation suppression also did not improve actual frame time. None is enabled in normal play. The benchmark now has a separate diagnostic build, preserving normal hot-path behavior. The subsequent buffer-selection lease passed matched timing runs and user visual checks in both Balamb views; see the profiling record. It remains exclusive to the diagnostic preview. Normal development/shipping integration is the next task, not part of this checkpoint.

## Current outcome

Both Balamb views passed lease timing/visual criteria. ChapterOne test2 historical comparison identified tag-capture overhead introduced in0cc705d; modern selection leasing recovers the earlier40–60FPS range with tags intact. Inactive-weather upload skipping adds1.78–1.97ms saving in matched drytest2. Combined preview dry→rain transitions passed; normal Linux development build runtime matched preview (~47FPStest2/100+FPSrainyBalamb). Full evidence and current artifact status are in TAG_WORK_PROFILING.md. The remaining scene cost predates tagging and is separate from the completed optimization/integration phase.
