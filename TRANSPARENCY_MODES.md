# Transparency modes and Linux evidence

Updated 2026-08-21. This is the canonical record for the transparency work.
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

These rendering values are not routed yet. A read-only uniform census behind
`NWN_ALPHA_MODE_CENSUS=1` proved that NWN uploads distinct values when an MTR
explicitly selects custom shaders. Requiring that selection changed the
foliage's rendering, however, so the final selector still needs a non-invasive
transport on the engine-selected stock path. Do not infer a requested mode
from texture names, camera angle, or bucket alone.

## Current experimental baseline

The local branch contains four unpushed transparency commits after
`origin/main`:

```text
249ddff Savepoint: working Linux A2C foliage baseline
af01bc0 A2C: receive directional cascade shadows per fragment
bca4c63 A2C: receive local-light shadows per fragment
147ffe0 Savepoint: A2C particles and local-shadow ping-pong
```

They are local savepoints, not a claim that every interaction is solved. The
observed A2C result is stable under camera rotation and was visually preferred
at 8x MSAA. At 2x MSAA the small sample set is conspicuously stippled; 4x is
better. With no MSAA, A2C cannot provide fractional coverage and must fall back
to native rendering or cutoff.

The outstanding A2C problems are architectural:

- directional and local shadow reception must remain correct on covered
  samples;
- alpha geometry must continue to cast alpha-aware shadows;
- multiple A2C layers progressively consume samples, so later particles can
  disappear even when the source texture is only partly opaque;
- emitter visibility therefore needs explicit transmittance information, not
  merely a disabled depth test;
- UI, water, framebuffer-sampling materials, volumetric materials, and
  unmarked draws must remain native.

The earlier weighted-OIT experiment demonstrated smooth blending but was not
accepted: its replay/classification path could disappear at particular camera
angles and had ordering problems with fog, water, lighting, textures, and UI.
Weighted OIT remains a future explicit mode, not the current global solution.

## Native MTR census

Four one-material Linux runs were captured on 2026-08-21 with the ordinary OIT
census only. The launcher truncates `shadowmap-phase1.log` at startup; each run
was preserved separately as `census-test1.txt` through `census-test4.txt`.

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
2. **Strict routing:** distinguish regular alpha, framebuffer 1/2, volumetric,
   emitters, water, UI, replays, and injector passes. Unknown means native.
3. **A2C opt-in:** move the existing foliage A2C path behind mode 2, detect the
   live MSAA sample count, and provide a native fallback.
4. **A2C shadows:** receive directional and local shadows in the material draw
   and preserve alpha-aware caster capture.
5. **Emitter transmittance:** record foliage transmittance and apply it to a
   late emitter composite. Do not make particles unconditionally depthless.
6. **Weighted OIT opt-in:** accumulate and resolve only mode-3 draws. Keep the
   native draw visible until private accumulation, resolve, camera stability,
   fog, and water ordering are proven.
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
cd "/run/media/fede/SSD_SATA/Games/nwn-shadowmap"
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
cd "/run/media/fede/SSD_SATA/Games/nwn-shadowmap"
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
cd "/run/media/fede/SSD_SATA/Games/nwn-shadowmap"
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
