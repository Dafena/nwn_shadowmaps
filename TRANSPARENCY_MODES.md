# Transparency modes and Linux evidence

Updated 2026-08-22. This is the canonical record for the transparency work.
It separates observed NWN behaviour from injector experiments and future
design. The Linux client is the only runtime authority until the maintainer
explicitly requests Windows work.

## Goal

Support several explicitly selected material treatments instead of globally
converting every alpha-looking draw. Foliage is the first test family; hair can
be considered only after foliage, shadows, emitters, fog, water, and camera
motion are stable.

The planned material selector is:

```mtr
parameter int NWN_ALPHA_MODE 0
```

The provisional values are:

| Value | Intended treatment |
| ---: | --- |
| `0` | Preserve native NWN rendering |
| `1` | Native-style alpha cutoff |
| `2` | Alpha-to-coverage; requires multisampling |
| `3` | Weighted blended OIT |
| `4` | Conventional source-over blending |

The stock-path parser/bind census proved a non-invasive selector transport. A
strict Linux router is now built for runtime testing: mode 2 alone may select
A2C, while every other, unknown, or excluded case stays native. Do not infer a
requested mode from texture names, camera angle, or bucket alone.

## Current experimental baseline

The accepted visible foliage baseline is explicit material Mode 2 using
alpha-to-coverage (A2C). It is stable under camera rotation and was visually
preferred at 8x MSAA. At 2x MSAA the small sample set is conspicuously
stippled; 4x is better. With no MSAA, A2C cannot provide fractional coverage
and must fall back to native rendering or cutoff.

Explicit Mode 3 is an accepted Linux visual prototype using a native-pivot
opaque core plus a weighted, order-independent soft fringe. It remains behind
diagnostic gates until private target work is lazy and all census/readback
instrumentation is removed from its production path.

The accepted A2C baseline has these architectural limits:

- directional and local shadow reception must remain correct on covered
  samples;
- alpha geometry must continue to cast alpha-aware shadows;
- multiple A2C layers progressively consume samples, so later particles can
  disappear even when the source texture is only partly opaque;
- emitter visibility uses explicit transmittance and opaque-only depth rather
  than merely disabling depth testing;
- UI, water, framebuffer-sampling materials, volumetric materials, and
  unmarked draws must remain native.

The earlier weighted-OIT experiment demonstrated smooth blending but was not
accepted: its replay/classification path could disappear at particular camera
angles and had ordering problems with fog, water, lighting, textures, and UI.
Weighted OIT is now the active private checkpoint for explicit mode 3, never a
global solution.

## Native MTR census

Four one-material Linux runs were captured on 2026-08-21 with the ordinary OIT
census only. Trace logs append rather than truncate, so use a fresh `/tmp` log
name for each run. The original runs were preserved separately as
`census-test1.txt` through `census-test4.txt`.

The common opaque programs in buckets 0 and 2 are scene context, not the test
material. The relevant material results were:

| Test | MTR flags | Observed draw |
| --- | --- | --- |
| 1 | `transparency 1`, `twosided 1` | bucket 1, program 152, source-over blend, depth test/write on, cull off |
| 2 | test 1 plus `sample_framebuffer 1` | bucket 5, program 152, source-over blend, depth test/write on, cull off |
| 3 | test 1 plus `sample_framebuffer 2` | bucket 6, program 155, source-over GL state, depth test/write on, cull off |
| 4 | `transparency 1`, `twosided 1`, `volumetric 1` | two draws: bucket 1 and bucket 5, program 152, source-over blend, depth test/write on, cull on |

The observed area bucket order was:

```text
0 -> 1 -> 7 -> 2 -> 3 -> 5 -> 8 -> 4
```

Bucket numbers are runtime evidence for the tested Linux build, not universal
semantic names. Program IDs are process-local and must never be persisted or
used as material identifiers.

### Visual evidence not visible in GL state

`sample_framebuffer 2` erased emitters behind the material, including through
nominally transparent texels. It was the only one of the four tests with this
behaviour. It also changed from program 152 to program 155 and occupied bucket
6, where visible emitter draws have also been observed.

`blend=ON` is therefore insufficient to classify framebuffer-2 as ordinary
transparency. The shader can sample or precompose a framebuffer image that does
not contain the emitter and then replace it. Bucket 6 is heterogeneous and
must not be treated wholesale as an emitter bucket.

`volumetric 1` is a genuine two-pass front/back technique. Its culling is
enabled per pass despite `twosided 1`; it must remain native and must not be
collapsed into an ordinary transparent draw.

## Classification rules

An injector mode may run only when all of the following are true:

1. the material explicitly requests a supported `NWN_ALPHA_MODE`;
2. the draw belongs to the source-classified stock alpha shader family or a
   separately supported custom shader family;
3. it is not a framebuffer-sampling, volumetric, emitter, water, UI, shadow,
   replay, or injector-owned pass;
4. the required technique is available, such as multisampling for A2C;
5. every touched GL state and shader uniform can be restored exactly.

`sample_framebuffer 1`, `sample_framebuffer 2`, and `volumetric 1` retain their
native meanings. They are not surrogate selectors for injector modes.

## Planned implementation checkpoints

1. **Material census:** complete. Injection was read-only; explicit custom
   shaders propagated default `0` and marked values `2` and `3` without
   leakage. The forced standard shader pair visibly changed the material, so
   that transport is evidence only and is not accepted for production.
2. **Strict routing:** complete. Regular
   mode-2 alpha may enter A2C. Framebuffer 1/2, volumetric, non-foliage,
   non-alpha buckets, unusual blends, and unknown identities remain native.
3. **A2C opt-in:** complete for the initial foliage proof. At 8x MSAA, mode 0
   and mode 3 remained native while mode 2 alone selected A2C, all on stock
   program 152. The live sample count must be at least 2 or the draw remains
   native. Evidence is preserved in `census-mode-routing.txt`.
4. **A2C shadows:** functionally complete. Directional and one-slot local-light
   receivers both activated on stock program 152, shadows were visible on
   mode-2 foliage, and source-classified alpha/card casters remained present.
   Dark shadows expose A2C's discrete sample mask; this is documented as a
   mode-2 limitation rather than treated as a shadow failure. Evidence:
   `census-mode2-shadow-routing.txt` and the focused visual test.
5. **Emitter transmittance:** accepted compromise. Mode-2 foliage
   transmittance and opaque-occluded private emitter color were independently
   proven, then joined after the shadow receiver and before overlays. Do not
   make particles unconditionally depthless.

   The private product proof passed at 1920x1006: two mode-2 draws yielded
   59,273 covered pixels, 15,816 fractional pixels, and `minT=0`. Modes 0 and
   3 remained native and the screen was untouched. Evidence:
   `census-mode2-transmittance.txt`. Exact source-over/additive separation and
   many-layer emitter fidelity are a pinned TODO, not a blocker for mode 3.
6. **Weighted OIT opt-in:** active private checkpoint. Accumulate only mode-3
   draws while keeping their native draw visible. Resolve, suppression, camera
   stability, fog, and water ordering remain gated on private proof.
7. **Product surface:** add stable controls and MTR documentation, then run the
   complete Linux regression and performance matrix.

Create local savepoints after checkpoints 1, 4, 5, and 6. Do not push unless
the maintainer explicitly requests it.

## Regression matrix

Every visible checkpoint must cover:

- MSAA 0x, 2x, 4x, and 8x;
- one and several overlapping alpha layers;
- opaque static geometry and characters crossing alpha geometry;
- directional and local shadow casting and receiving;
- torch/fire emitters behind one and several layers;
- water, fog, and post-processing borders;
- `sample_framebuffer 1`, `sample_framebuffer 2`, and `volumetric 1` unchanged;
- UI completely untouched;
- camera rotation, zoom, area transition, pause, and menus;
- GPU cost against the native baseline.

## Census commands

Run the read-only native census with behavioural OIT/A2C modes disabled:

```bash
cd "/path/to/nwn-shadowmap"
env -u NWN_OIT \
  -u NWN_A2C_FOLIAGE \
  -u NWN_OIT_FOLIAGE_VISIBLE \
  -u NWN_OIT_FOLIAGE_CENSUS \
  NWN_OIT_CENSUS=1 \
  NWN_DEV_NO_BUILD=1 \
  ./run-dev.sh
```

`run-shadowmap-trace.sh` executes `: > "$LOG"` before launch, so
`shadowmap-phase1.log` is overwritten each session. Preserve only the useful
lines before the next launch:

```bash
rg '\[oit\]\[(foliage-)?census\]' shadowmap-phase1.log \
  | sort -u \
  > census-testN.txt
```

Run the custom material-mode census separately:

```bash
cd "/path/to/nwn-shadowmap"
env -u NWN_OIT \
  -u NWN_A2C_FOLIAGE \
  -u NWN_OIT_FOLIAGE_VISIBLE \
  -u NWN_OIT_FOLIAGE_CENSUS \
  -u NWN_OIT_CENSUS \
  NWN_ALPHA_MODE_CENSUS=1 \
  NWN_DEV_NO_BUILD=1 \
  ./run-dev.sh
```

Extract only the result:

```bash
rg '\[oit\]\[material-mode(-census)?\]' shadowmap-phase1.log \
  | sort -u \
  > census-material-mode.txt
```

Test one unmarked ordinary alpha material plus modes 2 and 3. A non-negative
uniform location and values `0`, `2`, and `3` prove propagation. Because
uniform state belongs to a program, the unmarked draw must still return to zero
after marked draws; otherwise the engine leaks a previous material value and
the selector design needs an injector-owned upload/reset.

The first two runtime attempts produced `location=-1 mode=0` for every observed
program. The stock template does contain several preprocessor-selected `main`
definitions, so the declaration and guard were made unconditional across those
variants. The decisive second fault was in the final `glShaderSource` handoff:
it did not include `materialModeInjected` in the condition selecting the
patched string, so it logged the edit and then submitted NWN's original source.
The handoff is corrected and the rebuilt library reached a live uniform.

The third run exposed the uniform successfully (`location=2`) but all materials
remained at `mode=0`. The two marked MTRs had correct `parameter int` syntax but
did not explicitly select shaders. NWN's documented parametric-input path
requires `customshadervs` and `customshaderfs`, even when they name standard
shaders. The unmarked control also retained `volumetric 1` from the earlier
census, so it was not a clean ordinary-alpha control.

The fourth run selected `vslit_sm` / `fslit_sm` explicitly. Program 188 then
reported `location=2` and modes `0`, `2`, and `3`; the unmarked draw returned to
zero, proving both parameter propagation and reset behaviour. The same run
visibly replaced the foliage transparency with an environment/specular-looking
material. Because `NWN_ALPHA_MODE_CENSUS` does not alter valid non-negative
modes, that visual change is attributable to overriding NWN's normal shader
selection, not to the census guard.

Conclusion: arbitrary MTR parameters work through NWN's documented custom
shader path, but forcing a nominal standard pair is not rendering-equivalent
for this foliage. The final mode transport must preserve the engine-selected
shader. The next investigation is a stable material/resource identity hook or
injector-side MTR metadata table joined to the draw; unknown or ambiguous
identity must remain native.

### Stock-path material identity census

The first non-invasive identity probe is implemented on Linux behind:

```text
NWN_ALPHA_IDENTITY_CENSUS=1
```

It hooks `Material::BindAllStandardTextures()` immediately before NWN's normal
shader bind/draw sequence and records the `Material*` plus texture 0 resource
name from the engine's own accessors. Installation requires a real subhook
trampoline; there is deliberately no remove/call/reinstall fallback on this hot
path. The known-bad `CAurTexture::BindInUnit` hook is not enabled.

At the source-classified ordinary-alpha draw, the census reports the current
material, resource name, and live GL texture-0 object. All addresses and GL IDs
are diagnostic and process-local. A future mode table may use resource names or
parsed MTR metadata, never persisted pointers/programs/texture IDs.

The first run proved the safe bind-to-draw handoff: three distinct material
pointers were reported without visual corruption. All three intentionally use
the same `tcm02_leaves04` texture and therefore also shared one GL texture
object; texture identity is insufficient. The implementation now additionally
captures `Material::InitSharedMaterial(name)` so the next run can verify the
actual base, `_1`, and `_2` shared-material/MTR names at those same draws.

The second run succeeded: the stock program and texture remained shared while
the draw-time identities were correctly reported as `tcm02_leaves04`, `_1`,
and `_2`. The census now observes the engine's low-frequency
`SharedMaterial::ParseField()` path and recognizes only
`parameter int NWN_ALPHA_MODE` values 0 through 4. This uses NWN's resource
loading path, so it does not depend on MTR files being loose on disk and does
not require a custom shader.

The third run completed the proof: the base material reported mode 0, `_1`
reported mode 2, and `_2` reported mode 3 while all three retained stock program
152 and texture 86. Every required safe trampoline installed, no custom shader
was present, and native rendering remained intact. The Linux stock-path mode
transport is accepted. Strict fail-closed routing is the next checkpoint.

Run it with every behavioural transparency option disabled:

```bash
cd "/path/to/nwn-shadowmap"
env -u NWN_OIT \
  -u NWN_A2C_FOLIAGE \
  -u NWN_OIT_FOLIAGE_VISIBLE \
  -u NWN_OIT_FOLIAGE_CENSUS \
  -u NWN_OIT_TEXTURE_CENSUS \
  -u NWN_ALPHA_MODE_CENSUS \
  NWN_ALPHA_IDENTITY_CENSUS=1 \
  NWN_DEV_NO_BUILD=1 \
  ./run-dev.sh
```

Extract the bounded evidence before the next launch:

```bash
rg '\[oit\]\[material-identity' shadowmap-phase1.log \
  | sort -u \
  > census-material-identity.txt
```

### Strict mode-routing proof

Run this checkpoint with 8x MSAA and every legacy transparency experiment
disabled:

```bash
cd "/path/to/nwn-shadowmap"
env -u NWN_OIT \
  -u NWN_A2C_FOLIAGE \
  -u NWN_OIT_FOLIAGE_VISIBLE \
  -u NWN_OIT_FOLIAGE_CENSUS \
  -u NWN_OIT_TEXTURE_CENSUS \
  -u NWN_ALPHA_MODE_CENSUS \
  -u NWN_ALPHA_IDENTITY_CENSUS \
  NWN_ALPHA_MODE_ROUTING=1 \
  NWN_DEV_NO_BUILD=1 \
  ./run-dev.sh
```

Expected decisions for the three stock-shader test materials are:

```text
tcm02_leaves04   mode=0 action=native-non-a2c-mode
tcm02_leaves04_1 mode=2 action=a2c-mode-2
tcm02_leaves04_2 mode=3 action=native-non-a2c-mode
```

Visually, only `_1` should gain A2C. The base control and `_2` must retain
their native appearance; UI, water, emitters, and unrelated geometry must be
unchanged. Preserve the decisions before another launch:

```bash
rg '\[oit\]\[mode-route\]' shadowmap-phase1.log \
  | sort -u \
  > census-mode-routing.txt
```

### Mode-2 shadow checkpoint

Use the same strict-routing launch at 8x MSAA. In the test area, inspect only
the mode-2 material under these conditions:

1. a character or opaque level object casts the directional shadow onto it;
2. the mode-2 foliage casts its cutout-shaped directional shadow onto opaque
   ground;
3. a known working local shadow light casts onto the mode-2 material;
4. rotate and zoom the camera while checking all three;
5. verify the mode-0 and mode-3 controls remain native.

The receiver must darken the covered A2C samples rather than darkening the
resolved background behind transparent texels. Its caster silhouette may use
the stock alpha-discard threshold; it must not become a solid card/quad.

After the run, preserve the relevant bounded log evidence:

```bash
rg '\[a2c\]\[shadow\]|\[oit\]\[mode-route\]|\[shadowmap\]\[static\].*alpha' \
  shadowmap-phase1.log \
  | sort -u \
  > census-mode2-shadow-runtime.txt
```

Directional success should include `direct per-fragment CSM receiver active`.
A valid local-light scene should additionally include
`direct per-fragment local receiver active`. Absence of the latter means the
local checkpoint was not exercised, not that it passed.

### Private mode-2 transmittance proof

The first checkpoint-5 slice duplicates only already accepted mode-2 A2C draws
into a private target and measures `product(1-alpha)`. It does not composite,
alter native emitters, or touch the visible framebuffer. This proof is
deliberately depthless: the scene depth is multisampled and the legacy OIT
proof target is single-sample. A proper multisample/depth-aware emitter design
comes after this value is proven.

Run at 8x MSAA with the same three-material test:

```bash
cd "/path/to/nwn-shadowmap"
env -u NWN_OIT \
  -u NWN_A2C_FOLIAGE \
  -u NWN_OIT_FOLIAGE_VISIBLE \
  -u NWN_OIT_FOLIAGE_CENSUS \
  -u NWN_OIT_TEXTURE_CENSUS \
  -u NWN_ALPHA_MODE_CENSUS \
  -u NWN_ALPHA_IDENTITY_CENSUS \
  NWN_ALPHA_MODE_ROUTING=1 \
  NWN_A2C_TRANSMITTANCE_CENSUS=1 \
  NWN_DEV_NO_BUILD=1 \
  ./run-dev.sh
```

The visible result must be identical to the proven strict-routing run. A valid
private proof reports at least one draw, non-zero `covered` and `fractional`
counts, `minT` below 1, `screen=untouched`, and `emitters=native`:

```bash
rg '\[a2c\]\[transmittance\]|\[oit\]\[mode-route\]' \
  shadowmap-phase1.log \
  | sort -u \
  > census-mode2-transmittance.txt
```

### Private emitter-color proof

This second checkpoint-5 slice retains the native emitter on screen and
duplicates only measured, source-classified particle draws into an
injector-owned multisampled RGBA16F target. Current Linux evidence places the
torch after the numbered buckets and after the engine's `Scene::Render`
trampoline returns. Its stable signature is the latched scene FBO, no live
alpha-discard uniform, depth test on, depth write off, and source-over or
additive blending. UI is excluded by its disabled depth test, foliage by its
live discard uniform, and opaque geometry by depth writes. Bucket 6 remains an
accepted path for builds/materials that use it. The target uses the private
opaque-only multisample depth assembled before foliage and extended with
dynamic opaque bucket 2. This is still a proof, not the final emitter fix:
there is no transmittance composite and the visible frame must not change.

Run at 8x MSAA in the same test area, with a torch/fire emitter behind both a
single mode-2 card and several overlapping mode-2 cards:

```bash
cd "/path/to/nwn-shadowmap"
env -u NWN_OIT \
  -u NWN_A2C_FOLIAGE \
  -u NWN_OIT_FOLIAGE_VISIBLE \
  -u NWN_OIT_FOLIAGE_CENSUS \
  -u NWN_OIT_TEXTURE_CENSUS \
  -u NWN_ALPHA_MODE_CENSUS \
  -u NWN_ALPHA_IDENTITY_CENSUS \
  NWN_ALPHA_MODE_ROUTING=1 \
  NWN_A2C_TRANSMITTANCE_CENSUS=1 \
  NWN_A2C_EMITTER_CENSUS=1 \
  NWN_DEV_NO_BUILD=1 \
  ./run-dev.sh
```

The existing emitter occlusion remains visible during this private test; that
is expected because native rendering is retained. A successful private proof
reports at least one draw, a non-zero `lit` count, positive `maxRgb`,
`depth=duplicate-static-plus-dynamic`, `screen=untouched`, and
`native=retained`:

```bash
rg '\[a2c\]\[emitter-census\]|\[a2c\]\[transmittance\]|\[oit\]\[mode-route\]' \
  shadowmap-phase1.log \
  | sort -u \
  > census-mode2-emitter-private.txt
```

Accepted Linux evidence refined the depth construction. Importing scene depth
at bucket 1 rejected essentially all private emitter color. A depth-disabled
duplicate captured 9,489 lit pixels at `maxRgb=0.9438`; far-cleared depth plus
bucket-2 dynamic opaque duplicates also retained emitter color. This proved
the dynamic path valid and the scene-depth import contaminating.

The accepted replacement imports no scene depth. It clears private depth to
far before bucket 0, then immediately duplicates only depth-writing,
non-blended draws from opaque bucket 0 and dynamic opaque bucket 2. Runtime
proof reported seven emitter draws, 8,675 lit pixels, `maxRgb=0.9531`, and
`maxA=0.4478` with:

```text
depth=duplicate-static-plus-dynamic screen=untouched native=retained
```

Checkpoint 5 now has two independently proven private inputs:

- mode-2 foliage `product(1-alpha)` with fractional transmittance;
- source-over/additive emitter color occluded only by duplicated opaque
  bucket-0/bucket-2 geometry.

The first visible checkpoint must remain separately opt-in. It may suppress
only the same strict emitter signature after both private targets are ready,
then composite captured emitter color through mode-2 transmittance before the
overlay. Missing targets, unknown signatures, UI, water/framebuffer-sampling
materials, and non-mode-2 transparency remain native.

### Visible emitter-through-foliage checkpoint

This checkpoint replaces only the already proven late particle signature. It
is intentionally separate from mode routing and from both private census
switches. Start it with a fresh log at 8x MSAA:

```bash
cd "/path/to/nwn-shadowmap"
env -u NWN_OIT \
  -u NWN_A2C_FOLIAGE \
  -u NWN_OIT_FOLIAGE_VISIBLE \
  -u NWN_OIT_FOLIAGE_CENSUS \
  -u NWN_OIT_TEXTURE_CENSUS \
  -u NWN_ALPHA_MODE_CENSUS \
  -u NWN_ALPHA_IDENTITY_CENSUS \
  NWN_ALPHA_MODE_ROUTING=1 \
  NWN_A2C_TRANSMITTANCE_CENSUS=1 \
  NWN_A2C_EMITTER_CENSUS=1 \
  NWN_A2C_EMITTER_VISIBLE=1 \
  NWN_DEV_NO_BUILD=1 \
  NWN_SHADOWMAP_LOG=/tmp/nwn-emitter-visible.log \
  ./run-dev.sh
```

The first accepted frame should report both the private proof and:

```text
[a2c][emitter-visible] first replacement composite: ...
timing=after-shadow-receiver-before-overlays
```

Visually verify the same torch/fire emitter through one mode-2 card and then
several overlapping cards. It should remain visible but be attenuated by each
foliage layer. Opaque bucket-0/bucket-2 geometry must still occlude it. UI,
water, mode-0/mode-3 controls, and materials using `sample_framebuffer` or
`volumetric` must remain native. This first visible version stores source-over
and additive particles in one private color target; if those blend families
behave differently at runtime, split targets are the next refinement rather
than widening the classifier.

The engine scene FBO and final composite FBO are not expected to match on the
Linux shadow path. The shadow receiver resolves scene FBO 2 into output FBO 0
before this composite. Screen-space viewport and extent equality are the valid
compatibility checks; requiring framebuffer identity suppresses native
particles and then incorrectly skips their replacement.

### Private mode-3 weighted-OIT proof

`NWN_OIT_MODE3_CENSUS=1` is the first checkpoint for explicit mode 3. It
duplicates only strictly eligible mode-3 stock alpha draws into the weighted
OIT MRT while retaining the native draw. It performs no visible resolve and no
suppression. Mode 0, mode 2, unknown materials, framebuffer-sampling and
volumetric materials, unusual blends, emitters, water, UI, and injector-owned
draws remain native.

The mode-3 proof currently shares its diagnostic MRT with the private mode-2
transmittance census, so those two census switches are intentionally mutually
exclusive. This does not prevent normal mode-2 A2C and private mode-3 OIT from
being present in the same scene.

Run the three-material test with a fresh log name:

```bash
cd "/path/to/nwn-shadowmap"
env -u NWN_OIT \
  -u NWN_OIT_CENSUS \
  -u NWN_A2C_FOLIAGE \
  -u NWN_OIT_FOLIAGE_VISIBLE \
  -u NWN_OIT_FOLIAGE_CENSUS \
  -u NWN_OIT_TEXTURE_CENSUS \
  -u NWN_ALPHA_MODE_CENSUS \
  -u NWN_ALPHA_IDENTITY_CENSUS \
  -u NWN_A2C_TRANSMITTANCE_CENSUS \
  -u NWN_A2C_EMITTER_CENSUS \
  -u NWN_A2C_EMITTER_VISIBLE \
  NWN_ALPHA_MODE_ROUTING=1 \
  NWN_OIT_MODE3_CENSUS=1 \
  NWN_DEV_NO_BUILD=1 \
  NWN_SHADOWMAP_LOG=/tmp/nwn-mode3-private.log \
  ./run-dev.sh
```

The visible result must remain native. A successful proof reports the mode-3
route plus non-zero private color, alpha sum, and transmittance coverage:

```text
[oit][mode-route] ... mode=3 ... action=oit-mode-3-private
[oit][mode3-private] proof draws=... color=... sum=... trans=...
depth=disabled-private-proof screen=untouched native=retained
```

The first proof deliberately uses `GL_ALWAYS` in the private single-sample MRT
because the live scene depth is multisampled. This proves material identity,
shader output, equation, and immediate draw-state retention only. A compatible
depth/order design is required before any visible resolve or native
suppression.

The Linux proof passed on 2026-08-23 at 1920x1006. One mode-3 draw produced
56,172 color pixels, 56,198 alpha-sum/transmittance pixels, 28,477 fractional
sum pixels, `maxSum=1.3721`, 56,198 fractional-transmittance pixels, and
`minT=0.0983`. Mode 0 remained native, mode 2 remained A2C, and the visible
mode-3 draw was retained. Evidence is preserved in
`census-mode3-private.txt`.

Private accumulation is therefore accepted. The next slice resolves these
same buffers into an injector-owned texture and reads that texture back. It
must still perform no screen composite and no native suppression. That slice
is now built: the same `NWN_OIT_MODE3_CENSUS=1` command should additionally
report non-zero `resolved`, `fractionalResolved`, and `resolvedMaxRgb` values.

The private resolve passed on 2026-08-23. It produced 55,756 resolved pixels,
all fractional, with `resolvedMaxRgb=0.1025` and `resolvedMinT=0.4553`; that
minimum exactly matched the accumulated transmittance. The screen remained
untouched and the native mode-3 draw was retained. Evidence is preserved in
`census-mode3-resolve.txt`.

The next gate is a bounded private stability census while the camera rotates
and zooms. It must report no sampled frame where an eligible mode-3 draw was
present but the private resolved coverage became zero.

Enable it by adding `NWN_OIT_MODE3_STABILITY_CENSUS=1` to the private mode-3
command. Keep the mode-3 test material in view and rotate/zoom continuously for
at least 120 rendered eligible frames. The census samples every fifth frame to
limit synchronous readback cost and finishes after 24 samples:

```text
[oit][mode3-stability] complete eligibleFrames=120 samples=24 disappeared=0
```

Any `DISAPPEARED` line fails this gate. Large coverage variation is expected
when zooming or viewing cards edge-on; zero resolved coverage while the marked
material remains visibly in frame is not. The census remains private:
`screen=untouched native=retained`.

The stability gate passed on 2026-08-23: 24 samples across 120 eligible frames
reported `disappeared=0`, coverage range 16,275–16,313, and non-zero resolved
color throughout. Evidence is preserved in `census-mode3-stability.txt`.

The next private gate reconstructs single-sample opaque depth by immediately
duplicating only depth-writing, non-blended bucket-0 and bucket-2 draws into
the mode-3 MRT depth attachment. It must not import scene depth: that approach
was already proven contaminated during the A2C emitter investigation. Mode-3
accumulation will test this private opaque depth while its native draw remains
visible and unchanged.

Enable this gate with `NWN_OIT_MODE3_DEPTH_CENSUS=1` alongside
`NWN_OIT_MODE3_CENSUS=1`. The ordinary private proof should remain non-zero and
change its depth label to:

```text
opaqueDepthDraws=... depth=duplicate-static-plus-dynamic
screen=untouched native=retained
```

The duplicate count must be non-zero. Inspect the mode-3 material while opaque
static geometry and a character cross in front of it, then rotate and zoom.
The visible result is still NWN's native mode-3 draw; this checkpoint proves
only that the private weighted-OIT contribution survives a reconstructed
opaque depth test without importing contaminated scene depth.

The private-depth gate passed on Linux on 2026-08-23. The injector duplicated
362 depth-writing, non-blended bucket-0/bucket-2 draws. One marked mode-3 draw
then produced 55,742 color pixels and 55,770 fractional alpha-sum,
transmittance, and resolved pixels through `GL_LEQUAL`; `resolvedMinT=0.4468`
matched the accumulated transmittance. The screen remained untouched and the
native draw was retained. Evidence is preserved in `census-mode3-depth.txt`.

Private opaque depth is therefore accepted. The next gate is ordering-only:
prove from live state that the duplicated stock color already contains NWN's
fog, and locate the real water/final-scene boundary before attempting any
visible composite. Bucket numbers alone are not semantic proof.

Enable the read-only ordering gate with `NWN_OIT_MODE3_ORDER_CENSUS=1` while
keeping the private accumulation and depth switches enabled. Keep the marked
mode-3 material and visible water in the same view, in an area with fog enabled.
After the first eligible frame, the census observes one complete following
frame. It reports:

- the live `fogParams` reaching the marked stock shader; the injected private
  branch runs after NWN has produced its stock main color;
- each distinct program/state signature used by every numbered bucket,
  including active uniform names containing `water`, `frame`, `screen`,
  `refract`, or `fog`;
- each bucket-complete FBO/viewport and the final post-shadow-receiver target.

The census does not suppress, reorder, resolve to the screen, or composite any
mode-3 draw. The native result remains the only visible result. Preserve the
bounded `[oit][mode3-order]` lines as evidence; do not infer water solely from
a bucket number when no water/framebuffer uniform signature corroborates it.

The first ordering run proved fog and framebuffer timing. The mode-3 program
received enabled fog parameters (`start=30`, `end=45`) before its final stock
color entered private accumulation. Numbered buckets rendered into FBO 2 and
the shadow receiver produced FBO 0 before overlays. Bucket 6 remained
ambiguous: its program exposed only fog uniforms, while prior evidence already
places `sample_framebuffer 2` in the same bucket. Evidence and this unresolved
qualification are preserved in `census-mode3-order.txt`.

The follow-up build additionally emits `[oit][mode3-order] material` lines with
the bound MTR name, mode, `sample_framebuffer`, `volumetric`, transparency, and
texture-0 resource name. Use those fields—not the process-local program ID—to
identify the late draw in the same water scene.

The follow-up passed: the marked mode-3 material was `tcm02_leaves04_2` in
bucket 3, while the stable MTR and texture identity of the bucket-6 draw was
`TTR01_water01`. The first valid visible insertion point is therefore after
bucket 3 and before bucket 6, into scene FBO 2. Compositing after the shadow
receiver into FBO 0 is rejected because it would draw mode 3 over water.

The first visible gate is deliberately non-suppressing. Enable
`NWN_OIT_MODE3_VISIBLE_CENSUS=1` with private mode 3 and reconstructed depth.
It adds the private resolve to scene FBO 2 at bucket-3 completion, before water,
but retains NWN's native mode-3 draw. The marked material will therefore look
too strong/double-layered during this diagnostic. Judge only ordering: water
must render over it where appropriate, fog color must match, shadows and the
later shadow receiver must remain present, and UI must remain untouched. Native
suppression is allowed only after this visible insertion point passes.

The first visible run reached the correct FBO/timing but failed opacity. Its
strongest pixel had `minT=0.4600`, so the private layer never exceeded roughly
54% opacity; removing the retained native draw at that point would only make
the failure more obvious. Evidence is preserved in `census-mode3-visible.txt`.

For cutout-authored foliage, enable `NWN_OIT_MODE3_ALPHA_NORMALIZE=1` for the
next gate. The shader divides raw alpha by the material's live
`fAlphaDiscardValue` pivot and clamps it: coverage that NWN's native cutout
would retain becomes opaque, while sub-pivot coverage remains a continuous
soft fringe. This is foliage-specific alpha shaping, not a change to the
weighted resolve equation. Native mode 3 remains retained for this gate.

The normalized run proved full opacity (`minT=0`) but still showed the farther
physical card through the nearer one. `maxSum=2.0` identified two overlapping
opaque layers: weighted OIT was averaging their colors even though resolved
alpha was opaque. This is a weighted-color limitation, not insufficient alpha.

Enable `NWN_OIT_MODE3_HYBRID_CENSUS=1` for the corrective foliage gate. It
changes only the strictly routed mode-3 draw:

1. alpha at/above the live native cutoff renders as an opaque, depth-writing
   scene core;
2. the same core immediately writes private depth and resets accumulated
   color/sum/transmittance to their identity values at those pixels, erasing
   any farther fringe submitted earlier;
3. only normalized alpha below the pivot accumulates as weighted OIT fringe;
4. the private fringe composites after bucket 3 and water still renders later
   in bucket 6.

This gives foliage exact nearest-surface behavior for solid coverage while
retaining smooth order-independent edges. It is an explicit foliage hybrid,
not pure weighted OIT and not the final policy for glass/hair modes.

### Hybrid interoperability with native cutouts

The first hybrid test passed Mode-3-on-Mode-3 overlap but showed card borders
through intervening Mode 0/2 objects. Those objects were rendered correctly by
their own native paths, but they were not represented in Mode 3's private depth
image. The weighted fringe therefore had no evidence that they occluded it.

The current checkpoint keeps Mode 0 and Mode 2 visible rendering unchanged and
adds only a private occluder duplicate for strictly classified cutout materials:

- `transparency 1`, `sample_framebuffer 0`, and non-volumetric;
- stock alpha-discard shader with a live cutoff and ordinary source-over/cutout
  blend signature;
- explicit material mode 0 or strictly accepted A2C mode 2;
- bucket 1 or 3 only.

Surviving stock-cutoff pixels write Mode 3 private depth and reset its MRTs to
the accumulation identity, removing any farther Mode 3 fringe at those pixels.
For Mode 2, the private duplicate temporarily disables the shader A2C path so
the occluder uses the authored alpha cutoff rather than an MSAA coverage mask.
No Mode 0/2 color is accumulated by Mode 3 and no visible mode routing changes.

The Linux follow-up passed. Both native control modes emitted the bounded
occluder-only proof, the visible Mode 3 path remained `core-plus-fringe`, and
the intervening card-border artifact disappeared. The hybrid visual prototype
is accepted; evidence is appended to `census-mode3-visible.txt`.

This does not make the census path production-ready. Its private opaque-depth
reconstruction duplicates broad bucket-0/2 work and the development launcher
enables additional expensive diagnostics. Production Mode 3 must remove
readbacks/order instrumentation, allocate and clear its targets lazily, and do
no private depth/MRT/resolve work in frames or areas without visible Mode 3
materials. Performance comparisons must use the same clean shadowmap launch;
`run-dev.sh` is intentionally not that baseline.
