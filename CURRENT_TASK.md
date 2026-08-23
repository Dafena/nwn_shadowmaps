# Current task checkpoint

Updated 2026-08-23. The active work is a Linux-first, material-selectable
transparency system. Documentation has been reconciled before the next code
change. The canonical evidence and roadmap are in
[TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md).

## Immediate checkpoint: productionize accepted Mode 3

The non-invasive stock-path selector and strict Linux router are proven. With
`NWN_ALPHA_MODE_ROUTING=1`, explicitly marked Mode 2 ordinary alpha foliage
uses A2C while Mode 0 and excluded materials remain native. Explicit Mode 3
has now passed its diagnostic hybrid visual checkpoint: a native-pivot opaque
core provides exact depth while a weighted OIT fringe preserves soft edges.

The next implementation removes census/readback work from the normal Mode 3
path and makes private depth/MRT/resolve work lazy. Areas without visible Mode
3 materials must pay no Mode 3 frame cost. Performance must be compared using
the clean shadowmap launch, not `run-dev.sh`, which enables all diagnostics.

```mtr
parameter int NWN_ALPHA_MODE 2
```

The census was intended to demonstrate:

1. an unmarked material reads as mode `0`;
2. at least two marked materials retain distinct integer values;
3. the value is joined to the correct live program/draw without persisting GL
   object IDs;
4. framebuffer-sampling, volumetric, emitter, water, UI, replay, and
   injector-owned draws remain excluded;
5. no draw is suppressed, redirected, replayed, or visibly modified.

Runtime propagation is now proven, but the mechanism is rejected for normal
materials. Do not begin A2C routing until mode ownership can be transported
without changing NWN's engine-selected shader variant.

Enable only this census with `NWN_ALPHA_MODE_CENSUS=1`. Valid modes are
non-negative. Negative values are reserved by the dormant shader guard and must
not be authored.

The runtime proof needs three ordinary alpha materials in one area, preferably
sharing the same stock shader program:

- one with no `NWN_ALPHA_MODE` parameter;
- one with `parameter int NWN_ALPHA_MODE 2`;
- one with `parameter int NWN_ALPHA_MODE 3`.

This also tests whether NWN resets an unmarked material to zero or leaks the
previous marked material's program-uniform value. Expected log values are
`0`, `2`, and `3`, with a non-negative uniform location.

The first two runtime attempts on 2026-08-21 reported only
`location=-1 mode=0`. Two faults were found. First, the stock shader template
contains multiple preprocessor-selected `main` definitions, so the uniform is
now declared unconditionally after `#version` and guarded in every candidate
`main`. Second, and decisively, the final shader-source handoff omitted the new
`materialModeInjected` flag and submitted NWN's original source despite logging
the successful edit. The handoff now submits the patched string. The rebuilt
Linux library awaits the same three-material rerun.

The third attempt proved the corrected injector: ordinary alpha program 152
exposed `NWN_ALPHA_MODE` at live location 2. Every draw still read zero,
isolating the remaining issue to NWN's material upload. Inspection of the test
MTRs found that modes 2 and 3 correctly used `parameter int`, but did not name
`customshadervs` and `customshaderfs`. NWN's documented parametric-input path
requires explicit shaders even when selecting standard shaders. The unmarked
control also still had `volumetric 1` from the previous census and was not an
ordinary-alpha control.

The fourth run explicitly selected `vslit_sm` / `fslit_sm`. It proved parameter
propagation on program 188: the ordinary-alpha candidates reported modes `0`,
`2`, and `3` at live location 2, and the unmarked material returned to zero.
There was no uniform leakage between the three materials.

That run also visibly replaced the intended transparent appearance with an
environment/specular-looking result. The census itself is read-only; the
regression came from forcing a shader pair that is not equivalent to the
engine-selected foliage variant. Therefore explicit `customshadervs` /
`customshaderfs` selection is useful as proof of NWN's custom-parameter upload,
but is rejected as the final mode-selection requirement.

The next checkpoint now has a read-only Linux implementation awaiting runtime
evidence. `NWN_ALPHA_IDENTITY_CENSUS=1` hooks
`Material::BindAllStandardTextures()` only when subhook provides a real
trampoline, reads texture 0's resource name through the engine's own
`Material::GetTexture()` / `CAurTexture::GetName()` accessors, and joins it to
the source-classified alpha draw. It does not replace shaders or intercept
`CAurTexture::BindInUnit`; that older hook remains rejected because it produced
camera-dependent texture/controller corruption.

The first runtime run installed the safe trampoline and preserved native
rendering. It reported three distinct `Material*` values, but all three MTRs
correctly resolved to the same texture resource and GL object:
`tcm02_leaves04`, texture 86. Texture identity alone therefore cannot select
between these materials.

The second identity run proved the full name association while retaining native
rendering. It reported `tcm02_leaves04`, `tcm02_leaves04_1`, and
`tcm02_leaves04_2` as separate MTR/shared-material identities even though all
three shared program 152, texture resource `tcm02_leaves04`, and GL texture 86.

The third identity run completed the proof. With no explicit custom shaders,
the stock program 152 and texture 86 remained shared while the draw-time census
reported:

```text
tcm02_leaves04   mode=0
tcm02_leaves04_1 mode=2
tcm02_leaves04_2 mode=3
```

All three safe hooks installed without trampoline warnings and native rendering
remained intact. The non-invasive selector transport is therefore accepted on
Linux. Pointer, program, and GL IDs remain process-local and must never be
persisted. Unknown, missing, stale, invalid, or cross-bucket identity remains
native.

This implementation is fail-closed. Mode 0, mode 1, mode 3, mode 4, unknown
materials, non-foliage shader families, non-alpha buckets, framebuffer-sampling
materials, volumetrics, inactive alpha-discard variants, unusual blend modes,
and draws without live MSAA remain native. The legacy global
`NWN_A2C_FOLIAGE` switch is ignored when strict routing is enabled. Emitter and
OIT behaviour is intentionally unchanged at this checkpoint.

The 8x-MSAA runtime proof reported all three materials on stock program 152:
mode 0 remained native, mode 2 selected `a2c-mode-2`, and mode 3 remained
native. The visual result independently matched the log: two native controls
and one A2C material. Evidence is preserved in `census-mode-routing.txt`.

Strict routing and A2C opt-in are therefore accepted.

The routing run already proved the code-side directional handoff. On the first
frame after cascade allocation, the explicitly routed mode-2 draw activated
the direct per-fragment CSM receiver on stock program 152. The static cascade
also continued to report source-classified alpha/card casters from bucket 1,
so routing did not remove alpha-aware caster capture. This evidence is saved in
`census-mode2-shadow-routing.txt`.

The focused follow-up confirmed local-light reception as well: program 152
reported `direct per-fragment local receiver active` with one live slot, and
the shadow was visible on mode-2 foliage. The shadowed foliage exposed the same
discrete coverage pattern seen around emitters. This is an inherent A2C
representation limit: covered samples are opaque and uncovered samples retain
the background. It is not a failed shadow lookup and will not be hidden by
further shadow-equation tuning.

Checkpoint 4 is accepted as functionally complete with that documented visual
limitation. The active work is checkpoint 5: accumulate mode-2 foliage
transmittance privately, prove it without touching the visible frame, then use
it for a depth-aware late emitter path. Native emitters remain authoritative
until the private proof is complete.

The first checkpoint-5 implementation is now built behind
`NWN_A2C_TRANSMITTANCE_CENSUS=1`. Immediately after each native mode-2 draw it
duplicates the still-live geometry/material into the injector's private MRT
and accumulates `product(1-alpha)`. It performs at most eight bounded readbacks
and reports covered/fractional texels plus minimum transmittance. The scene and
emitters remain untouched. This first proof is intentionally depthless because
the live scene depth is 8x multisampled and the old OIT proof target is
single-sample; depth-aware emitter ordering follows only after the product is
proven.

Runtime proof succeeded at 1920x1006: two mode-2 draws produced 59,273
covered pixels, 15,816 fractional pixels, and minimum transmittance 0. Mode 0
and mode 3 remained native. Evidence is preserved in
`census-mode2-transmittance.txt`. The next private slice captures eligible
emitters against multisampled opaque-only depth while retaining the native
emitter on screen; no transmittance composite is enabled yet.

That emitter-capture slice is now built behind
`NWN_A2C_EMITTER_CENSUS=1` (and requires the transmittance census). It
duplicates only source-classified blended scene particles into a private
RGBA16F multisample target. On the current Linux build the torch draw is
bucketless and flushed after the engine's numbered area buckets; bucket 6
remains accepted for builds/materials that use it. The target shares the accepted
opaque-only depth image, so opaque level/character geometry can reject the
private emitter while A2C foliage cannot. The private color is resolved and
read back only for bounded proof; the native emitter and visible framebuffer
remain authoritative and unchanged. Census proved the current Linux torch is
not in bucket 6: program IDs are process-local, but its stable draw signature
is a bucketless draw to the latched scene FBO, source-classified scene color,
no live alpha-discard uniform, depth test on, depth write off, and either
source-over or additive blending. The private classifier now uses that
signature and reads back at frame completion.

The first clean run with that signature captured two emitter draws, but the
opaque-only depth test rejected essentially all color (`lit=0`, then two
near-zero pixels). The next build automatically follows an empty opaque-depth
proof with a private `GL_ALWAYS` capture on the following frame. This is
diagnostic only and still leaves native color/screen untouched; it separates a
depth disagreement from missing emitter shader/texture output.

That staged proof succeeded: `GL_ALWAYS` captured 9,489 lit pixels with
`maxRgb=0.9438`, and far-cleared depth plus dynamic opaque duplicates captured
695 lit pixels with `maxRgb=0.2976`. Therefore emitter state/color and dynamic
opaque depth are valid; the pre-foliage scene-depth blit is the contaminating
step. The next private build constructs opaque-only depth from immediate
depth-only duplicates of bucket 0 and bucket 2, with no scene-depth import.
That proof is accepted: after an empty animation frame, seven emitter draws
produced 8,675 lit pixels with `maxRgb=0.9531` and `maxA=0.4478` under
`depth=duplicate-static-plus-dynamic`. Private mode-2 transmittance and private
opaque-occluded emitter color are therefore both proven. The first visible
replacement is now implemented locally behind `NWN_A2C_EMITTER_VISIBLE=1`.
It retains both private paths, suppresses only the strictly classified native
emitter draws after all replacement inputs are ready, and composites captured
emitter color through mode-2 transmittance before overlays. Unknown signatures
and unavailable inputs remain native. The corrected Linux path composites from
scene FBO 2 into the shadow receiver's output FBO 0 and is accepted as the A2C
compromise baseline.

**PINNED TODO — A2C emitter fidelity:** source-over and additive particles
currently share one private RGBA16F target and one approximate late composite.
This is adequate for the accepted compromise, but exact blend-family
reconstruction, stronger failure recovery after suppression, and pathological
many-layer coverage remain deferred. Do not block mode-3 work on this TODO and
do not widen the emitter classifier as a shortcut.

## Verified native MTR routing

The four Linux census runs completed on 2026-08-21:

| Material configuration | Observed route |
| --- | --- |
| `transparency 1`, `twosided 1` | bucket 1, regular source-over, cull off |
| plus `sample_framebuffer 1` | bucket 5, regular source-over GL state |
| plus `sample_framebuffer 2` | bucket 6, different program; visually erases emitters behind it |
| `volumetric 1` | two passes in buckets 1 and 5 with culling enabled |

All four transparent paths had depth testing and depth writes enabled. This is
important for A2C: multiple layers consume MSAA samples and can eliminate
later emitters. Disabling emitter depth is not an acceptable general fix; the
planned solution records foliage transmittance and applies it during a late
emitter composite.

The observed area bucket order was `0, 1, 7, 2, 3, 5, 8, 4`. Bucket numbers
and program IDs are evidence for this Linux build only; program IDs are
process-local.

## Current experimental implementation

The tree contains opt-in A2C, particle-transmittance, per-fragment shadow, and
hybrid Mode 3 work. Mode 2 is an accepted foliage baseline with documented
coverage limits. Its many-layer emitter fidelity remains pinned. Mode 3 is an
accepted diagnostic visual prototype, not yet a production or shipping path.

Accepted runtime observations:

- A2C avoids the camera-angle disappearance seen in the earlier weighted-OIT
  replay experiment.
- 2x MSAA is visibly coarse; 4x is improved; 8x was the preferred result.
- A2C preserves useful depth intersection with characters and static meshes.
- directional/local shadows work per covered sample; their discrete A2C mask
  is visible in dark regions and is an accepted mode-2 limitation.
- late emitters use private transmittance and opaque-only depth. The result is
  accepted as a compromise; exact mixed-blend reconstruction remains pinned.
- the earlier weighted-OIT path blended smoothly but could disappear with
  camera angle and disturbed UI, fog, water, textures, and opaque geometry.
  It is not the active baseline.

## Ordered roadmap

After the read-only material census (steps 1–4 complete/accepted):

1. enforce strict material/pass exclusions;
2. place the existing A2C path behind explicit mode `2` with MSAA detection and
   a native fallback;
3. integrate directional/local shadow reception into the selected material
   draw and preserve alpha-aware shadow casting;
4. add foliage transmittance for late emitters — accepted compromise, with the
   fidelity refinement pinned above;
5. implement weighted OIT as a separate explicit mode `3`, initially private
   and non-suppressing — private accumulation, resolve, and camera stability
   and private opaque-depth reconstruction are accepted; fog/water/final-scene
   ordering is the current private checkpoint;
6. expose stable controls and run the full Linux regression/performance matrix.

The current mode-3 ordering gate is read-only. With the marked material,
visible water, and area fog in one view, it records one complete frame of live
fog uniforms, late bucket program hints, FBO transitions, and the final
post-shadow-receiver target. It never composites to the screen or suppresses
the native draw.

Fog and FBO timing passed on 2026-08-23. Water ordering remains open because
bucket 6 is shared with `sample_framebuffer 2`; material correlation then
identified the live area water as `TTR01_water01` in bucket 6. The accepted
insertion point is scene FBO 2 after mode-3 bucket 3 and before water bucket 6.
The active gate composites there while retaining native mode 3, intentionally
producing a double layer until water/fog/shadow/UI ordering is visually proven.
That first visible run exposed insufficient private alpha (`minT=0.4600`). The
active follow-up normalizes cutout foliage around its native discard pivot so
solid coverage becomes opaque and only the sub-pivot fringe remains smooth;
native suppression is still deferred.

Normalization reached opaque alpha but exposed weighted color averaging across
two opaque physical cards (`maxSum=2.0`). The active foliage checkpoint is now
hybrid: a native-pivot opaque core writes scene/private depth and resets farther
OIT at covered pixels; only the normalized sub-pivot fringe uses weighted OIT.

The first hybrid visual pass fixed Mode-3-on-Mode-3 card overlap and established
the desired overall look. It also exposed a separate interoperability gap:
native Mode 0/2 cutout cards were absent from Mode 3's private depth, allowing a
farther Mode 3 fringe to remain visible across their rectangular/card borders.
The active checkpoint now duplicates only the stock alpha-tested core of strict
Mode 0/2 cutouts into Mode 3 private depth and resets the OIT MRT identity there.
Their visible routing remains native/A2C; their color never enters Mode 3 OIT.

The cross-mode follow-up passed visually and in the private proof: Mode 0 and
Mode 2 reported occluder-only participation, and Mode 3 remained a core-plus-
fringe composite before water. The diagnostic Mode 3 prototype is accepted.
The next checkpoint is productionization: remove census/readback work from the
runtime path, activate lazily only after a visible Mode 3 material is known,
and compare it against the clean shadowmaps-only baseline. `run-dev.sh` is not
a performance baseline because it intentionally enables all diagnostics.

`sample_framebuffer 1`, `sample_framebuffer 2`, and `volumetric 1` keep their
native semantics and are never used as surrogate mode selectors.

## Deferred shadow issue: second-story tile flicker

NWN's “Hide second story tiles = Auto” setting removes and restores tiles from
the visible draw list. The current shadow map follows that list and can strobe
at the boundary.

Rejected approaches remain rejected:

- injecting the full unculled BSP into NWN's live draw stream;
- holding a generation based only on draw-count changes.

The deferred design uses two injector-owned static generations. Complete the
replacement privately, keep the old generation published, then cross-fade
after the new generation is coherent. Do not infer tile state from draw count.

## Other unresolved shadow limitations

- Local lights use one downward contact cone, not full point-light cube maps.
- Normal camera culling limits the sun and local caster set.
- Windows `lightpriority` remains unverified and disabled.
- Legacy stencil suppression and final lighting integration need broader
  runtime coverage.

## Working rules

- Linux is the priority for every feature until the injector works there.
  Windows work requires an explicit request.
- Use source and new runtime evidence as authority; remove stale phase claims.
- Preserve engine authority, replay guards, matrix-stack restoration, complete
  GL-state restoration, and coherent target publication.
- Unknown or unmarked materials render natively.
- Do not suppress an engine draw until its private replacement is proven.
- Add settings, table entries, reset defaults, and version updates together.
- Run `python3 check_shaders.py` after shader-string changes.
- Build the actual Linux artifact and record runtime evidence.
- Create local savepoints at agreed checkpoints. Do not push without explicit
  maintainer direction.

## Useful commands

```bash
git status --short --branch
rg -n "nwn_oit_needs_|nwn_oit_bucket_|census_observe_draw" nwn_oit.cpp
rg -n "my_shader_source|draw wrappers|nwn_oit" \
  shadow_shader_interposition.inc nwn_shadowmap.cpp
python3 check_shaders.py
make
```
