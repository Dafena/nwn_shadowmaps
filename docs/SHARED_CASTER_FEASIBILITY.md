# Shared caster work: bounded feasibility findings

Status: investigation complete for the first input census; no shared renderer
implemented. Linux diagnostic run, 2026-11-20, PID59987, selected frame934.
Normal development artifact remains unchanged.

## Observed common work

The diagnostic frame contained294 distinct engine draw-item signatures in each
sun/local layer. All294 matched by item pointer, per-item ordinal, program,
count and indexed flag. This signature deliberately omits vertex/index offsets,
instancing, bindings and full material state: it is evidence of overlap, not a
safe rendering command identity or a stable engine handle.

Both passes used seven programs. Non-creature items numbered115, using three:

| Runtime program | Items per layer | Maximum reconstructed model delta |
| --- | ---: | ---: |
|155|75|0.000008405|
|152|38|0.000009861|
|140|2|0.000008542|

Across all294 matched items, maximum model delta was0.000012658 and median
0.000005864. Models were reconstructed as m_view_inv*m_mv using actual sampled
uniforms, not the scene-entry camera. This is a single stationary-frame numeric
comparison, not a moving-camera or deformation proof.

Each pass had two layers, hence588 main draws. The115 non-creature items imply
230 additional filtered duplicates. These duplicates call the underlying GL
entry directly and are not counted again by the common diagnostic hook; the
prior RenderDoc capture independently measured230 filtered draws.

## What the census does not establish

All seven programs expose m_mv, m_proj, m_view_inv and a64-element m_bones array;
some also expose m_view. Exposed bones do not establish active skinning for an
individual draw. Programs have102–116 active uniforms. Matrices alone are not
an adequate replacement for all native shader/material state.

Driver shader-source queries returned length0. The report explicitly records
source_unavailable; no shader compatibility, opacity or rigidity classification
can be inferred from program IDs or uniform names. The three non-creature
programs are investigation candidates, not a certified rigid/opaque whitelist.

The census does not record complete vertex/index/instance state, all uniform
values, textures, sampler settings or resource mutation/lifetimes. Identical
counts and shader IDs do not prove identical depth coverage. Native transforms,
source order, caster culling, coverage, bias, map cadence and publication must
remain authoritative.

Temporary evidence: /tmp/nwn-caster-census/caster-draws.csv,
caster-program-*.txt, summary.json and analyze.py. Do not use the collection
frame as a performance sample; it intentionally performs synchronous queries
and file writes. Persistent findings here survive temporary-file cleanup.

## Design boundary and proposed next milestone

Current immediate-duplicate helpers can borrow a live draw's geometry/material
bindings, but they do not provide a complete shared rendering contract. A safe
implementation needs more than replacing two matrices or reusing an attachment.
The maintainer's bounded-plan agreement requires returning with a design before
implementing that broader change.

Proposed design, not approved implementation:

1. Define a same-frame shared-draw contract for a verified non-creature subset.
   Obtain exact linked shader sources and identify transform/coverage inputs,
   draw variants and resource ownership before choosing eligibility.
2. Compare immediate multi-target duplication with an explicit scoped command
   snapshot. Explain where engine traversal/material setup would actually be
   removed; merely moving existing draws is not evidence of a saving.
3. Preserve separate sun/local projections, creature-filtered maps and every
   existing caster/material rule. Unsupported draws retain their existing path.
4. Define failure recovery before skipping any original replay work: an
   incomplete capture must never publish a partial map or silently omit a draw.
5. Only after design review, build an opt-in diagnostic prototype. Acceptance
   remains at least1ms repeatable completed-frame gain in test2, no worse
   stutter or visuals, experimental until broader regression checks.

No FPS gain or implementation feasibility is promised from the matching input
counts. Layered geometry-shader rendering or a combined map-storage layout
would be a separate, larger alternative, not part of this bounded prototype.


## GateA boundary measurement — 2026-11-20

PID61417 completed487timed world frames in the12second probe (2second warmup),
local receiving/castingON, no camera abort. Median native root PartTriMesh::Draw
body3.220257ms/frame, non-creature subset2.662743ms.486of574observed original local
draws were inside that adapter,486root calls; uncovered draws retain fallback.
These inclusive CPU body costs include driver submissions and filtered copies;
they are not saved time or completed-frame measurements.

A subsequent separate census frame1936 found71non-creature unskinned items using
program155/VS153/FS154 with no active fAlphaDiscardValue.37other non-creature items
use active cutoff0.2 and remain outside the initial opaque scope. Existing source
dumps now recovered the shader sources: native VS153 selects vPos directly when
skinmesh0, then m_mv and m_proj with POSITION_VIEW; native FS154 hasNO_DISCARD1,
KEYHOLING_ENABLED0 and no native gl_FragDepth writes. This describes the measured
native source family, not a lifetime-safe admission rule for arbitrary programs
or final injected sources.

Important headroom limit: with two selected local slots, reusing the first mesh
call can avoid only later-slot preparation. The2.66ms total includes BOTHslots,
alpha fallback items and per-target depth draw costs that still have to happen.
A diagnostic follow-up nominates actual same-session Part handles from the
one-frame unskinned/no-alpha-uniform census solely for profiling, then separately
times later-slot body versus actual driver draw calls. Nomination never changes
rendering and is not a persistent compatibility cache. Use the per-frame
body-minus-driver remainder only as a generous upper bound before new transform,
binding, state restoration and validation costs; not a promised gain. This
follow-up is built, normal syntax check passes, runtime pending.


## GateA result: local-only initial subset does not justify a prototype

PID62970, census frame1016 followed by472timed frames, no camera abort. Same
requested test2/default-camera/local-receiveON/castON/VSyncOFF configuration.
Current census nominated74non-creature unskinned/no-alpha-uniform Parts for
profiling; count differs from prior session's71because scene contents changed.
No cross-session FPS comparison is inferred.

| Same-session CPU measurement | Median ms/frame |
| --- | ---: |
|All root local mesh calls|3.336116|
|Non-creature mesh calls, including cutouts|2.794935|
|Nominated mesh calls across both local lights|1.767429|
|Nominated mesh calls for later local lights|0.869539|
|Native driver draw submissions inside those later calls|0.247356|
|Per-frame later body minus driver submissions|0.621844|

586original local draws/frame,498covered by the mesh adapter and498root mesh
calls. The timed forwarding wrappers invoke each original driver draw once;
they make no draw admission or state-setting changes. Timers and driver stalls
are part of these CPU measurements; none is completed-frame time or a measured
optimization gain. Nominations remain diagnostic handles only. The remainder
is generous potential preparation headroom before new validation, transform,
target-binding and restore overhead, not a rigorous maximum on every possible
GPU scheduling redesign. Individual medians are not subtracted to obtain the
remainder; it is calculated per frame then summarized.

Decision: do NOT implement the proposed first-local-to-later-local prototype
for this initial subset. Even its entire measured later mesh body is below1ms,
and the non-submission remainder is only~0.62ms. No evidence supports the
required repeatable1ms completed-frame saving. This does not prove that all
local-shadow optimization is impossible, nor that including more classes or
sharing across sun/local passes would meet the target.

A possible next design scope is cross-sun/local reuse, which has more repeated
passes to target, or a different measured bottleneck. It needs a new scoped
investigation/design review. Do not extrapolate0.62ms linearly across passes as
a promised saving. No shared renderer was implemented. Normal artifact and
original settings/resources remain unchanged; game remains open in diagnostic
mode, with timing/census automatically stopped. Runtime log:
/tmp/nwn-caster-census/headroom-game.log.


## Joint sun/local headroom — 2026-11-20

Maintainer selected broader sun/local investigation. PID63680, census791,
478timed frames, no camera abort.71diagnostically nominated Parts; source first
dynamic sun cascade had71mesh calls, later sun/local passes213mesh calls. Local
original draws574,486covered/root local mesh calls. No sharing or source/target
policy changed.

| Same-session CPU measurement | Median ms/frame |
| --- | ---: |
|Nominated mesh bodies across sun+local|3.374641|
|Later sun+local consumer mesh bodies|2.540650|
|Native driver draw calls inside those consumers|0.733178|
|Per-frame consumer body minus draw calls|1.808048|
|Both local consumers' nominated mesh bodies|1.696201|
|Native driver draw calls inside both local consumers|0.486113|
|Per-frame both-local body minus draw calls|1.210195|

Local-only later-light remainder0.592909ms again supports declining the previous
local-slot-only scope. Full later sun/local remainder1.808048ms justifies
further cross-pass design work but is NOT a completed-frame gain or strict
hardware-independent maximum. First-sun-to-local consumers have1.210195ms of
generous measured remainder before any new validation, transforms, target binds
or state restoration: only~0.21ms margin against the1ms acceptance target.
Individual medians need not subtract exactly; remainders are calculated per
frame before taking their median. CPU intervals include driver stalls/state
work and cannot be converted directly into promised GPU/frame savings.

The broader result does not certify shader/geometry/resource contracts or
engine-side visibility and material decisions. It does not justify taking over
sun publication without a recovery design. Recommended bounded candidate for
review: first native sun mesh preparation can fan out ONLY to private local
write targets for the verified rigid opaque non-creature contract, retaining
sun's original drawing/publication. Its margin is slim and it may fail the1ms
matched-frame criterion; decline rather than change quality or relax the target
if so. Including later sun consumers remains a larger, separately reviewed
publication/lifecycle scope. No shared renderer implemented. Log:
/tmp/nwn-caster-census/cross-game.log.
