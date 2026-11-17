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
