# Restricted shared-draw path: proposed design

Date: 2026-11-20. Status: initial local-only prototype declined after measured gateA; see
SHARED_CASTER_FEASIBILITY.md. No shared renderer implemented. Linux reference only, separate diagnostic build. Evidence:
[feasibility report](SHARED_CASTER_FEASIBILITY.md) and
[measurement plan](INJECTOR_OVERHEAD_PLAN.md).

## Goal and first scope

Reduce repeated native preparation/submission for verified non-creature meshes
while preserving all depth results and current appearance. Require >=1ms
repeatable completed-frame improvement in matched uncapped test2, no worse
stutter/visuals. Remain experimental until broader checks.

First proposed producer is the FIRST LOCAL LIGHT's existing engine replay;
reuse an eligible mesh's prepared draw for subsequent local lights in that same
bucket. Sun rendering remains an independent consumer for a later milestone.
This is an intentional narrowing of the cross-sun/local idea: first prove the
shared geometry mechanism within one capture phase, where material policy and
publication rules already agree. Two local sources were selected in the census.
If fewer than two are selected, retain existing rendering for the entire path.

Do not merge finished depth maps. Main and creature-filtered maps remain
separate, projection-specific representations at their existing resolutions.
No light ranking, source count, update cadence, caster filtering, coverage,
bias, sample count or user-facing defaults change.

## Work it must actually remove

Current local replay invokes the engine bucket once per selected light. The
existing filtered duplicate already avoids another engine bucket traversal,
but still requires its own depth draw.

The proposed path prepares an eligible mesh through NWN once, then submits its
geometry to each required local common/filtered target from that preparation.
During subsequent local-light engine traversals, a verified private per-mesh
draw adapter substitutes a completed shared draw for the original mesh draw.
The expected saving is inside native per-mesh preparation and redundant driver
setup. The target-specific depth draws still happen. The engine's bucket
iteration may still happen for fallback meshes; do not count it as removed.

Merely issuing the same geometry earlier and suppressing its later GL draw is
NOT sufficient: it leaves prior native mesh/material setup running. A measured
adapter must bypass an actual costly preparation boundary. If preparation is
outside the existing mesh hook, this design has no demonstrated benefit and
must be reconsidered before prototype work grows.

## Existing seams and missing evidence

- `nwn_layer_stack.inc::part_draw_detour` wraps Linux PartTriMesh::Draw and
  already scopes current Part identity. Its existing normal layer scheduling
  must remain unchanged. A diagnostic observer/substitution can be designed at
  this seam; its coverage and internal side effects are not yet certified.
- Draw-item owner classification follows the engine's Part/AurObject external
  type. A draw-item pointer or program number from a census is not a persistent
  cache key or eligibility certificate.
- Draw wrappers cover indexed/range/base-vertex/multi-draw/instanced variants.
  The first prototype may support only one VERIFIED transport; unsupported
  variants must keep their original path, not be reinterpreted.
- Existing transform helpers do not capture a complete draw contract. Programs
  expose view/inverse-view, projection, model-view and bone inputs. Native
  unchanged static uniforms may not upload again; interception alone is unsafe.
- The previous driver source queries returned length0. Use existing explicit
  vertex/fragment source dumps at compilation, with isolated output, or a new
  capture. Establish exact linked-source ownership and program generation;
  do not whitelist by transient numeric ID or uniform names.

## Eligibility contract

Admission requires ALL of these, otherwise original rendering:

1. Engine-positive non-creature identity; no ownership inference from mesh
   appearance, proximity, shader family or bucket number.
2. A verified rigid, opaque source/material contract. Establish actual per-draw
   skinning state; a non-creature may still animate. Exclude cutouts, continuous
   alpha, framebuffer sampling, water, particles, special depth outputs,
   keyholing and unknown shaders for the initial proof. Bucket2 alone is not an
   opacity certificate.
3. A single fully observed supported mesh draw with bounded command arguments,
   correct program generation and complete relevant native state.
4. Verified target-transform recipe for the exact shader contract. Cover every
   position/clip/coverage input affected by switching local projections; include
   view/inverse-view and clip parameters when relevant. Preserve deformation
   inputs and restore every changed uniform byte-for-byte afterward.
5. No untracked nested draw or resource mutation within the mesh scope. The
   source geometry/material resources must remain valid until immediate fanout
   completes. Unknown context/program/resource ownership is a fallback.
6. Existing local capture generation, scene, policy, source slots and target
   resources remain valid. No sharing across frames, areas, contexts, program
   relinks, target reallocations, or source-policy changes.

A source hash identifies only a verified contract, not arbitrary equivalent
GLSL. Relinking/deleting/reusing program names invalidates admission. Resource
identity includes context and observed lifetime; numeric GL names alone do not
prove unchanged content. Required mutation/interposition coverage is a go/no-go
gate, not an assumed property of the current hooks.

## Proposed per-mesh transaction

1. Enter the existing engine mesh scope on the first local projection.
2. Observe the native call without altering its geometry/material decisions.
   Form one typed draw description and verify eligibility/state completeness.
   A partial observation never authorizes suppressing later native work.
3. Only after a complete eligible description exists, issue its immediate
   target-specific copies for remaining local slots, using the same live
   geometry/material data and verified transform recipe.
4. Capture filtered depth only for the same non-creature draws as today.
   Initialize each write layer exactly once through the existing generation
   bookkeeping. Do not clear earlier shared contributions in a later pass.
5. Restore ALL touched GL/uniform state before returning to NWN. No cached
   assumption survives the mesh scope as unvalidated live GL state.
6. Retain only a same-frame completion ledger identifying which engine mesh,
   draw occurrence, generation and target slots were completely rendered.
   Retain no borrowed pointers as future-frame rendering authority.
7. At a later local projection, bypass the verified native mesh preparation
   only if the authoritative item/occurrence, generation, material/geometry
   identity and required target completion all match. Every other item invokes
   its original mesh path. Do not rewrite bucket lists or insert hidden meshes.

Whether a mesh has multiple draws, repeated occurrences, material changes or
side effects must be established before admission. Exact state equality at the
source is not proof of future stability. If safe later substitution requires a
broader retained command/resource snapshot, stop and review that additional
architecture rather than silently expanding this design.

## Failure and publication

Use the existing private write generation; receivers consume only completed
read generations. Maintain current receiver/staging ordering and publish only
at the established bucket3 completion point.

A completed ledger entry may suppress original work only after all required
common/filtered copies succeeded under verified preconditions. Do not treat
issued draw calls or zero GL errors as proof of correctness. Missing input
means original rendering before any substitution.

If a generation becomes suspect AFTER shared writes/substitution, invalidate
it and prohibit publication. Never repair it by blending old and partial
metadata. Retain the last complete generation under the existing receiver
contract, disable the candidate, and rebuild using the original path at the
next safe complete-generation boundary. Such a failure aborts the benchmark
and disqualifies that run; any visible lag/flash/stale result fails acceptance.
Do not promise invisible recovery until runtime evidence establishes it.

All engine bucket callbacks still use guarded_render_bucket and replay flags.
Preserve matrix stacks byte-for-byte, matrix mode and complete touched GL state.
Source selection continues to use GetShadowLights in engine order.

## Phased implementation gates

### A. Boundary and contract proof: read-only

Collect exact linked sources and per-draw skinning/coverage inputs for candidate
non-creature meshes. Measure native PartTriMesh::Draw preparation coverage and
CPU cost, separately from downstream GL submission. Verify complete lifetime
of borrowed resources within a source mesh scope and check skipped-call side
effects. Census overhead is not an accepted frame-time measurement.

Go/no-go: identify at least one real compatible subset with a defensible route
to >=1ms saving. If the native preparation to bypass is outside the adapter,
or candidates require incomplete state capture, return the finding before
adding a renderer. Do not implement a large generic GL recorder as a shortcut.

### B. Diagnostic prototype: explicit approval after A

Implement one supported mesh/draw/material contract with immediate local-slot
fanout, bounded per-frame ledger, exact fallback and fail-closed publication.
Use CPU mocks only where they exercise real lifecycle/transaction seams. Never
promote from a state-model unit test without runtime map/visual evidence.

### C. Correctness before timing acceptance

Check common AND filtered map depth against the reference under the same camera
and animation state where reproducible. For opaque eligible geometry, compare
coverage/depth and explain numerical tolerances; final-scene similarity alone
is insufficient. Verify creatures remain excluded from filtered maps, native
fallback meshes render, camera movement/highlights remain correct, and toggles,
area changes, source changes and resource rebuilds do not publish partial data.
No appearance or resolution reductions are an optimization result.

### D. Matched completed-frame A/B/A

Hold camera/settings fixed in one test2 session; alternate baseline/candidate,
exclude warmup, guard focus/VSync/source settings and log eligibility/fallback
counts. Compare median and tail frame times. Retain only repeatable >=1ms
saving without worse stutter/visuals. Reject zero-coverage or failure-guard runs.
Keep diagnostic-only until broader acceptance is separately completed.

## Alternatives deferred or rejected

- Cross-sun/local sharing immediately: adds distinct capture policies and
  scheduling before the smaller local-slot mechanism is proven.
- Persistent cached geometry: requires resource mutation and engine lifecycle
  authority not established by the current code.
- Layered geometry-shader fanout/combined map storage: larger shader-interface
  and target-layout redesign, not the first prototype.
- Attachment reuse: already failed the1ms threshold.
- Simple opaque fragment stripping: capture-only test did not establish a
  useful completed-frame gain; unknown/native coverage must remain intact.
- Reduced cadence, fewer sources or lower resolution: visual/quality tradeoffs,
  outside this unchanged-appearance design.

## Open review decisions

Approve or revise the first-source choice (local slot0 rather than a sun
cascade), the read-only boundary proof, and the prototype gate. This document
selects no final architecture and is not an ADR. No shared renderer should be
implemented until its stated missing contracts and review gates are resolved.


## GateA disposition — 2026-11-20

Read-only measurements found0.869539ms later-light mesh-body time for the initial
subset, including0.247356ms native draw submission; the per-frame non-submission
remainder median was0.621844ms before any new sharing overhead. This does not
justify a prototype against the1ms completed-frame acceptance target. PhaseB is
not approved or started. Broader cross-sun/local sharing remains a possible
separate scope, not an automatic extension or a demonstrated saving.

## Cross-sun/local investigation scope — 2026-11-20

Maintainer requested investigation of sun/local sharing after the local-only
headroom gate failed. This is a proposed broader design, not an implemented or
approved renderer prototype.

Producer: the first active dynamic sun cascade for each native bucket2/3.
Consumers: later active sun cascades and selected local-light slots. The initial
material/geometry subset remains verified rigid opaque non-creatures. If the
producer or any required ownership/coverage evidence is unavailable, all meshes
continue through their original paths. Do not silently substitute a different
producer or enlarge the caster set.

The read-only timing extension measures nominated mesh body and native draw
submission across those actual passes in one frame. It reports first-sun source
call count and later-consumer call count. Require nonzero, consistent producer
coverage; zero-source runs cannot support this design. The per-frame remainder
of consumer-body minus driver-draw time is generous potential headroom, not a
completed-frame saving or proof of safe state capture.

A future prototype would remove later native mesh preparation ONLY after a
complete immediate producer mesh transaction has prepared all applicable
consumer targets and a valid per-frame completion ledger exists. Target draws
and filtered duplication remain necessary. Engine list iteration and unsupported
mesh paths remain; no savings should be attributed to them without measurement.

Additional contracts before implementation:

- Verify the original PartTriMesh::Draw visibility/renderability predicates and
  side effects at EVERY consumer. The engine's destination-pass decision must
  still govern; producer visibility alone does not authorize a consumer draw.
- Keep sun/local bias, native material culling, viewport, depth/test/write state
  and map targeting separate. A common geometry packet is not a common pass
  policy. Preserve the exact source shader inputs that affect position/coverage.
- Do not assume shader state remains the same between passes: validate source
  contract, uniform changes, immutable-within-scope resource lifetime and the
  exact transport. Programs/Parts observed in the census are not a whitelist.
- Schedule early local fanout only when existing local preparation is valid for
  the generation the baseline would consume; do not advance source selection,
  derive new sources from the sun pass, or publish an early partial map.
- Coordinate layer initialization with existing frame stamps so later baseline
  consumers do not clear successful contributions. A shared write that becomes
  uncertain makes the write generation unpublishable; do not repair corruption
  by drawing an opaque duplicate over it.
- The sun dynamic maps currently publish in-place, while local maps swap full
  generations at bucket3. The initial producer sun draw can remain original,
  but sharing into later sun layers must not expose incomplete or suspect data.
  If safe failure recovery needs private sun generations, that is an additional
  target-lifecycle redesign to review, not something to add implicitly.

Preferred risk-limited first consumer, if measurements justify a prototype:
local-light write generations only, produced from the existing first sun pass.
This avoids claiming that later in-place sun layers can already be substituted
atomically. Measure this consumer subset separately before selecting it: the
aggregate sun+local headroom is not its individual headroom. Expansion to later
sun layers requires its own publication/recovery contract.

No scope here changes sun/local source authority, map quality or caster rules.
A design review must select the exact consumer subset, failure recovery and
hook contracts before a renderer prototype is implemented.


### Cross-pass gate result and review proposal

Joint probe PID63680 found1.808048ms median later sun+local CPU-body remainder,
with71first-sun producer calls and213later calls. First-sun-to-both-local subset
alone has1.210195ms remainder before new fanout overhead. These are headroom
estimates, not measured gains or certified eligibility.

Propose the first-sun-to-private-local-write-target consumer subset for the
bounded diagnostic prototype review. Preserve original sun draws and avoid
changing sun's in-place publication. This selects cross-sun/local reuse (the
producer is sun), not the previously rejected local-slot-only reuse. Admit no
mesh until exact source, rigidity/coverage, native destination renderability,
transport/state completeness and immediate resource lifetime are established.
Uncertified meshes retain their original rendering. Keep all existing phaseA
contract gates; timing evidence alone does not authorize suppression.

Budget: the measured local-consumer remainder exceeds1ms by only~0.21ms; the new
path may have too much overhead to qualify. Do not broaden classes, lower
quality or relax acceptance to rescue it. If the contracts require a general
recorder, delayed resource retention, or additional sun generations, return
that broader design before implementation. A partial or uncertain local write
generation must never publish; failures abort performance validation.

This prototype proposal is pending maintainer review. Native original settings
and normal artifacts are unchanged. No rendering prototype has been built.


### Bounded prototype authorized with contract preconditions

Maintainer selected “Yes—proceed with the bounded prototype” on2026-11-20,
conditional on remaining shader/state/lifetime checks passing. Authorization is
for first-sun producer to private local write consumers, existing sun rendering,
unchanged quality and>=1ms matched-frame acceptance. Broader recording or sun
publication redesign remains outside this authorization.

Native executable inspection identified authoritative exported queries:
RenderInterface::GetModelMatrix (reference/pointer to current16float model),
PartTriMesh::GetIsOpaque and PartTriMesh::IsPartRenderable. GetModelMatrix forwards
to GLRender's stack matrix; use the export rather than a new offset read.
Scene::RenderDrawBucket calls SetModelMatrix before per-mesh dispatch. Actual
runtime correspondence and shader binding at mesh-entry versus draw still need
verification before substitution. IsPartRenderable is a read-only predicate in
the inspected native build, not permission to omit other unverified Draw side
effects. The read-only census now records those values at mesh-entry and draw.
No draw is suppressed or copied by these probes. Resource mutation/ownership
and exact program lifetime gates are still required; matching engine matrices
alone does not certify them.
