# Focused added-overhead audit — 2026-11-20

Current scope: maintainer redirected work from geometry sharing to auditing
added tagging/classification/draw-hook overhead. Acceptance is now at least
0.6ms repeatable completed-frame improvement, unchanged visuals and no worse
stutter. Validate combined savings rather than summing isolated improvements.
Test2 first; candidates remain experimental until broader checks.

## Historical evidence and exclusions

The b47f980→0cc705d comparison and tagging-bypass runs isolate an added regression;
current attachment batching, buffer-selection leases and inactive-weather skips
have already recovered significant cost. Do not treat the old regression as
proof that every remaining cost is tagging, or repeat already-rejected texture,
capability, validation, scalar-upload or attachment-cache experiments. Their
measured gains remain below0.6ms as well as the former1ms threshold.

Program-state sharing within one exact draw was inspected as a possibility.
Production OIT already returns before GL queries for unmarked draws; weather
uses its existing observed program. A broad shared-state cache is not justified
from this source inspection and is not implemented.

## Strong lead: trampoline-less native callbacks

Current runtime logs explicitly report NO trampoline for:

- PartTriMesh::Draw layer-stack scope;
- DrawBucketManager::GetNextItem.

Both hot callbacks consequently invoke subhook_remove, call the original and
subhook_install on every item. Source diff b47f980→0cc705d includes this machinery.
This rewrites executable instructions per invocation. Vendor source confirms
remove copies saved bytes and install writes a new jump; mprotect is performed
at setup, not on every invocation. Do not claim recurring mprotect syscalls.

Earlier reports called PartTriMesh scope measurements native mesh-body costs.
Those intervals ALSO include this remove/call/reinstall fallback. The numbers
remain valid inclusive measurements, but the attribution to unavoidable engine
preparation was incomplete. They are not limits on a hook-mechanics optimization:
that optimization affects producer/consumer/native calls and iterator overhead,
not just the later-local subset.

## Reversible diagnostic candidate

Keep installed detours/classification/scheduling, but call original native code
through verified x86-64 gateways. Do not reproduce iterator engine decisions or
read new engine structure offsets.

- Iterator gateway copies the exact17-byte verified prologue (no PC-relative
  operands), then jumps to the original continuation with a register-neutral
  absolute indirect jump.
- Mesh gateway copies the exact12-byte prefix; verifies the next relative CALL
  targets exported IsPartRenderable; relocates that call through an absolute
  indirect call/skip-literal sequence; jumps to the original continuation.
- Original stack/flags/register effects and engine body remain authoritative.
  Unknown prefix/callee refuses the candidate and retains the existing path.
- Code storage is writable only during construction, then read/execute; teardown
  occurs after detours are removed. No native text is rewritten per candidate
  call. Linux diagnostic-only; normal/Windows behavior unchanged.

Tests execute matching synthetic assembly fixtures through actual installed
Subhook detours. They verify predicate execution, iterator results/cursor/end,
return behavior, unchanged patched bytes across1000calls, restoration and
mismatched-prefix/callee rejection. Dedicated diagnostic build and normal syntax
check pass. No performance/visual claim before game runtime verification.

Benchmark variant6:9→10→9→10→9, five8second phases with2second warmup each. Both
phases retain current accepted lease/weather path and local-demand behavior;
only gateway dispatch changes. Report actual completed-frame medians/tails,
unchanged draw counts, per-world gateway mesh/iterator call counts and existing
focus/camera/settings guards. Require both live gateways to verify before run.
Current normal artifact remains45e060b5...ecb1314f.

Runtime comparison pending. Original launcher/settings/resources untouched.


## Matched runtime result: gateway candidate passes initial timing gate

PID68965, native Linux diagnostic library verified; startup confirmed both exact
prefix/callee gateways. Variant6 completed all5phases with no guard abort or
lease gaps, actual tag target8samples,3959original draw calls/world throughout.
Master/sun/local shadows and full-resolution fog remainON, VSyncOFF requested
and benchmark verified. No effect quality or routing settings changed.

|Phase|Path|Frame median ms|Frame p95 ms|Frames|
|---|---|---:|---:|---:|
|0|Existing hook fallback|20.0720|21.0269|296|
|1|Verified gateways|18.7767|20.6309|310|
|2|Existing hook fallback|20.0473|22.2216|290|
|3|Verified gateways|18.6945|20.6163|310|
|4|Existing hook fallback|19.8933|21.9962|292|

Adjacent-baseline mean savings1.28295ms and1.2758ms, passing both current0.6ms
and former1ms criteria in this view. Candidate gateway counts3654mesh and3926
iterator calls/world; baseline gateway counts0. Draw/lease counts match; no
geometry or tag data was omitted. Scene CPU~18.2–18.4→16.9ms. Supplementary GPU
timings are not summed with CPU/frame gains. FPS equivalents~50→53, not a
claim to reach60 or a cross-session comparison with earlier47FPS figures.

Preview4 requested after automatic stop to hold candidateON for visual checks.
Normal artifact unchanged. This is initial diagnostic timing acceptance, not
production promotion or broader visual/regression acceptance. Log:
/tmp/nwn-caster-census/audit-game.log. User visual check pending.


## Test2 visual check passed

Maintainer checked the continuous preview after timing and selected “Looks
correct” for camera movement, character hover, sun/local shadows, foliage and
transparency. Test2 timing and requested visual gates therefore pass for this
diagnostic candidate. Broader scene/transition checks and clean normal-artifact
integration have not been performed; remain experimental as previously agreed.
Normal library/defaults unchanged, no checkpoint commit or push requested.


## Broader preview and normal integration

Maintainer reported~150FPS in Balamb and “everything seems okay so far” after
being asked to check rainy wet effects, area changes, shadows, transparency and
highlights. This is broader user visual acceptance, not a matched Balamb timing
measurement or an attribution of its FPS to this fix alone.

Verified gateway dispatch is now enabled for normal Linux x86-64 source;
unsupported prologues/callee matches retain original fallback independently.
Diagnostic builds retain toggles/counters; normal builds compile the gateway
counters and caster/benchmark probes out. Windows gateway stubs remain unchanged.
Module renamed shadow_native_gateways.inc / native_hook_gateway for integration.
No generic Subhook change, native algorithm replacement or renderer policy change.
Unused-local-capture demand gating remains diagnostic-only; this integration
promotes only the separately validated native gateway fix.

Normal and shipping builds completed without warnings; native gateway regression,
shader checks and diff check passed. Artifacts:

- Development libnwn_shadowmap.so SHA25603b0794f55b7a23aa82fde036982e2f57e0eb939de71d6f47f3043f5de3d3aab.
- Shipping libnwn_shadowmap_deploy.so SHA256d51c5d09e27e4773f7b2a4090e438272dea9f006c58da333e9193c36c08c4f63.

Clean normal-development runtime launch prepared at/tmp/nwn-audit-normal/launch.sh,
using an isolated copy of preview settings and no benchmark/source-dump flags.
Exact normal runtime and shipping runtime acceptance pending. Original user
launcher/settings/resources remain untouched. No new checkpoint or push.


## Clean normal runtime check failed — investigation active

PID79501 verified development SHA03b0794f...3d3aab, both gateways verified,
no profiling flags. Maintainer reported both performance and visual differences
in rainy Balamb: local-light character shadow appears detached after loading,
and reported loading-view FPS~320→220. This is user regression evidence, not a
matched completed-frame measurement. Current game-window snapshot saved under
/tmp/nwn-audit-normal/regression.png. Preview/normal INI files compared equal;
all relevant effects/local receiving/casting were enabled. Do NOT mark normal
integration accepted or shipping runtime validated.

Known runtime-path difference: validated diagnostic preview4 also enables the
separate local-capture demand/area-reset experiment; normal currently keeps its
stub/original behavior. That may be relevant but is NOT an established cause.
Normal also initializes selection leasing at scene entry rather than through
the diagnostic controller. Source audit found no other conditional differences
in the gateway instruction bodies. Game/setting/load differences and the
observed shadow symptom still require isolation; do not tune bias or quality
as a workaround.

Prepared diagnostic preview mode5: gatewaysON, accepted lease/weatherON, local
lifecycle experimentOFF, matching the relevant normal feature choices. Existing
preview4: same gateways/lease/weatherON plus local lifecycleON. Mode2 retains
lease/weather but gateways/lifecycleOFF for subsequent gateway isolation if
needed. Logs now explicitly report mode/gateway/lifecycle state. Isolated launch
copies current normal settings. No new normal artifact/source behavior change
made during regression triage. New diagnostic build/diff checks pass. Same-session
comparison and fresh area-load behavior pending; UI settings/resources unchanged.


## Detached-shadow live isolation — unchanged without the optimization

PID80367 diagnostic isolation session, same Balamb view/settings. Mode5
(gatewaysON/local-lifecycleOFF) reproduced the detached local shadow. User noted
it may have existed before. Mode4 (gatewaysON/lifecycleON, validated preview)
looked the same. Mode3 (gatewaysOFF/lifecycleON) also looked the same after10s.
Thus neither the gateway fix nor this lifecycle toggle explains the observed
visual detachment in this test. Treat it as a separate existing local-shadow
issue, not evidence for a bias/quality workaround or a proven integration
regression. Mode4 requested restored after comparison.

The reported320→220loading-view FPS difference remains unconfirmed by matched
settled-frame measurements. This isolation does not retroactively certify
normal integration performance; that concern still needs a controlled check.
Do not describe the entire normal integration as passed based only on visual
isolation, nor claim the gateway caused the detached shadow.


## Settled Balamb gateway comparison — no gateway slowdown reproduced

First run request refused for lost focus; no timings accepted. Focus then
verified via target PID80367 X11 window before variant6 re-request. Completed
five8s phases, camera/settings/focus guards passed,1016draws/world throughout,
actual8sample tag target,0lease gaps. Local-lifecycle experimentON in both paths.

|Phase|Gateway|Median frame ms|p95 ms|Frames|
|---|---|---:|---:|---:|
|0|OFF|6.7655|7.1610|880|
|1|ON|6.4054|6.9102|925|
|2|OFF|6.8038|7.1336|877|
|3|ON|6.4277|6.7283|928|
|4|OFF|6.7384|7.1368|882|

Paired adjacent-baseline mean savings0.37925/0.3434ms, approximately148→156FPS.
Candidate handled937mesh+1052iterator calls/world. This view's gain is below
0.6ms but causes no repeatable slowdown; test2 still passes1.28ms, satisfying
useful gain in one tested view without worsening another. Do not infer the
reported320→220loading-counter drop from these different measurements.

This confirms gateway benefit in the controlled preview session, not a matched
normal-macro-vs-diagnostic-macro comparison. Normal integration performance
concern remains unresolved rather than automatically accepted. Source/INI checks
and lifecycle live toggles are separate evidence. Detached local-light shadow
was identical in gatewayOFF/ON and lifecycleOFF/ON tests, so it remains a separate
existing issue. Preview4requested restored after automatic stop. No bias/quality
changes, no new artifact build.


Rain coverage limitation: this settled Balamb run START explicitly reports
rain=0.000 while wet-effect settings are enabled. It therefore did not validate
active wet-film/puddle rain cost or a wet-weather transition. Do not label it
rainy runtime acceptance solely from area name or enabled controls. Actual wet
state needs confirming before that coverage can be claimed.
