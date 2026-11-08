# Current addition: explicit continuous-alpha layer module (2026-11-07)

The maintainer accepted the manually tuned Type-0 mesh-hint stack and authorized
integration as a module. See docs/LAYER_STACK.md. nwn_layer_stack.inc is included
in the normal Linux build and activates only with published shader metadata.
Unknown shaders, native water, shadow replays and Windows keep native behavior.
Development and shipping builds pass. Native full-Shadowmap held-Torch check
confirms per-fragment WorldMaker v6 lighting responds on imported floors with
identical camera matrices and unchanged priority/depth sequence. Full-map lighting
and Windows runtime parity are not certified. Existing A2C remains independent.

# Current task

## 2026-10-11: private rain checkpoint (runtime acceptance pending)

The user confirmed native hiding works, requested extreme rain budgets, and
reported missing rain in Storm Crown. The saved logarithmic budget slider and
numeric input now allow up to 262144 (default still 2048). Both parser and GPU
draw clamp use shared bounds. Storm Crown's authored area is exterior, 100%
rain, empty cc tileset, with non-static st_floor_tile floors; the shader's old
empty-height rejection suppressed rain in open-air cells. Falling streaks now
continue over empty cells, while splashes still require static-map collision.
Headless GL3.3 tests exercise open air, no fabricated splashes, max budget,
roofs, depth, alpha and state restoration. In-game Storm Crown confirmation
and high-budget performance testing remain required. No module edits were made.

weather_particles.inc adds shared GL3.3 rain/splashes and saved opt-in controls.
It uses authoritative weather and the completed static precipitation map.
The user confirmed visible private rain on Windows and explicitly requested
native rain hiding for comparison. A saved, default-on Hide native rain toggle
now suppresses only exact fxpa_rain01 particle draws while the enabled private
pass has a current/previous-frame completion and valid same-area shelter map.
The Windows shared-material adapter follows the installed 89.8193.37-17 emitter
binding path; it does not guess a Linux field offset. Lightning, snow,
wet-ground ripples, transparency routing and full-BSP submission are unchanged.
See [WEATHER_PARTICLES.md](WEATHER_PARTICLES.md)
for enabling, limits, headless tests and required in-game acceptance.
Offline tests do not establish NWN acceptance. Native hiding itself still
needs in-game validation; this is not a complete replacement or final acceptance.

Updated 2026-10-31. Linux remains the behavioural reference. The automatic
clear/rain/snow weather-effects system, including precipitation occlusion and
height-aware snow deformation, is accepted on Linux and ported to native
Windows. The shared renderer is intentionally identical; only engine discovery
and OpenGL interception differ by platform.

## Accepted Linux baseline

- Directional cascaded shadow maps and local-light shadow maps are working.
- Explicit material Mode 2 (`parameter int NWN_ALPHA_MODE 2`) selects the
  accepted alpha-to-coverage path when MSAA is available. Unmarked, unsupported,
  framebuffer-sampling, and volumetric materials remain native.
- A2C receives directional and local shadows and preserves opaque intersections.
  Its discrete sample mask and reduced visibility for emitters behind many
  overlapping layers are accepted limitations/TODOs.
- Material routing uses hashed concrete/shared identities, refreshes late MTR
  fields, and recycles entries through Linux destructor hooks. Native-area
  performance and native-to-authored area transitions passed the maintainer's
  runtime tests.
- Supported custom continuous-alpha shaders may publish final fragment alpha
  with `#define NWN_CONTINUOUS_ALPHA_SHADER 1` for the injector's Mode 2 path.

## OIT policy

Explicit Mode 3 weighted OIT is parked. `NWN_OIT_MODE3=1` and the
`NWN_OIT_MODE3_*_CENSUS` family are intentionally ignored; a marked Mode 3
material fails closed to native NWN rendering. The implementation and Linux
evidence remain in-tree for a deliberate future restart, but Mode 3 is not an
active development, production, shipping, or Windows-porting target.

The final Linux prototype did prove a native-pivot opaque core plus weighted
soft fringe, pre-water composition, stock fog/lighting retention, cross-mode
cutout occlusion, and same-bucket character occlusion. It is parked because the
remaining complexity and compromises are not currently worth productizing.
Historical experiments and commands remain in
[TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md), clearly marked as dormant.

## Resource-path invariant

Native Linux NWN intentionally follows aliases in
`/home/fede/.local/share/Neverwinter Nights/nwn.ini` into the Proton prefix, so
Linux and Proton share the same development resources. The `users/fede` and
`users/steamuser` development paths resolve to the same inode. Do not deploy
duplicate overrides into the unaliased home `development` directory.

## Accepted cross-platform weather-effects baseline

The automatic implementation lives in `weather_runtime.inc`:

- `CNWCArea::SetWeather(unsigned char, float)` is the authoritative automatic
  weather source. Linux disassembly confirms weather values 0 clear, 1 rain,
  2 snow and density in the 0..100 range. The injector normalizes density and
  maintains gradual wetness accumulation/drying without module configuration.
  Wetness is scoped to the current `CNWCArea`: area and save-load transitions
  reset inherited wetness immediately, while weather changes within one area
  retain the accepted fade. The stock `areaFlags` uniform is the authoritative
  interior test (`NWAREA_FLAG_INTERIOR`): interior draws suppress weather on
  their first frame and clear both surface reservoirs immediately, including
  when NWN reuses the same `CNWCArea` allocation across a load screen.
- The selected `g_areaScene` gate is mandatory. Static opaque bucket 0 receives
  the full effect. Static alpha/card bucket 1 receives orientation-aware wet
  noise and normal impacts without puddles. Dynamic body/hair buckets,
  characters, native water, and unknown/out-of-bucket draws are excluded.
  The current native bucket is authoritative per draw; cached `skinmesh`
  uploads are diagnostic only because NWN reuses programs and does not reset
  that uniform before every static draw. This prevents character movement and
  the resulting draw-order changes from making entire static tile sections
  lose or regain their snow layer.
- The Linux weather hook uses the established remove/call/reinstall fallback
  because this executable's function prologue does not yield a Subhook
  trampoline. Native Linux runtime testing confirmed clear, snow, and rain
  transitions, area changes, and save loading.
- Windows resolves private active-area `StartWeather`/`StopWeather` calls from
  the validated client network-decoder path around exported
  `CNWMessage::ReadBYTE`/`ReadBOOL`. At scene start it reads the newly active
  client area's stored weather byte and reapplies that state. This area-load
  synchronization handles weather changed while an interior had no weather
  surface and is client-side, so it works for local and multiplayer clients.
- Eligible opaque surfaces gain gradual wet film and compact world-anchored
  procedural puddles. Standing water is limited by geometric and smoothed
  surface flatness; steep geometry only becomes wet. Puddles reuse NWN's
  framebuffer and water-noise inputs for bounded screen-space reflection,
  refraction, and animated normal ripples. Non-puddle wet surfaces use moving
  micro-normal noise and smaller normal-only rain impacts.
- Rain contributions fade through NWN fog. Mode 2 A2C remains enabled by
  default and was kept outside the rain shader's RGB-only static-alpha work.
- Rain and snow share a separate world-anchored top-down precipitation depth
  map. It follows the configured static-world extent and resolution (including
  the 16K maximum), requests NWN's complete static BSP only when stale, and is
  reused until the area changes or the camera crosses 60% of its extent. The
  stable `CNWCArea::SetWeather` pointer keys the cache; transient torch/shadow
  objects cannot trigger repeated captures. Static opaque bucket 0 and static
  alpha/card bucket 1 accumulate into one capture. Stock alpha-discard holes
  remain open, while solid transparent-mesh fragments block precipitation;
  characters never enter the blocker map. Receiver-height depth comparison
  leaves rooftops exposed while sheltering geometry below them. A stationary
  16-sample world-space disk with geometry-anchored dithering softens blocker
  borders without camera shimmer, discrete bands, or displaced roof edges.
  Every rain contribution and snow coverage is multiplied by this exposure.
- Clear, rain, and snow are one authoritative state machine. Falling
  precipitation remains mutually exclusive, while accumulated rain and snow
  are independent surface reservoirs: the old layer fades as the new layer
  builds, matching clear-weather drying/melting instead of popping off. Rain
  and snow use the same switch-away/clear fade rate.
- The snow receiver adds world-anchored high-frequency accumulation on upward
  static opaque geometry, NWN fog fading, normal variation, and a bounded
  view-dependent raised parallax height walk. Bucket-2 creature transforms
  feed 16 bounded world-space character histories: resolved ground-contact
  movement carves up to 64 continuous rounded trail segments per character
  into a 512x512 RGBA32F deformation texture. Its first two channels store
  deformation depth and contacted world height, preventing a creature below a
  roof or bridge from deforming snow above it. Each character's
  oldest segment enters a two-second retirement fade at capacity; the texture
  is refreshed at 10 Hz with gradual refill and reset on area transitions.
  Slot 0 and a separate retirement pool are reserved for the camera-target
  player, so NPC pressure cannot evict that trail. Snow shaders sample this
  texture instead of evaluating segment uniform arrays per fragment. The
  occlusion fringe suppresses parallax and relief normals until the surface is
  nearly fully exposed, so a soft shelter transition cannot become a raised
  snow bank. Snow is a neutral material relit from NWN's decoded area light
  and complete enabled local-light census (up to the engine's 128-light
  setting), rather than inheriting the covered tile's albedo. Snow
  settlement trusts geometric slope and only mildly modulates with the
  material normal, while an albedo-independent illumination floor keeps the
  layer visible on dark tilesets that do not already use snow textures. Near
  full accumulation a continuous minimum blanket and 97% snow composite stop
  bright local lights from revealing the base texture as a lifted-snow halo.
The feature requires no environment variable, module edit, hak, shader
override, or user-authored material marker.

The maintainer confirmed the final Linux regression set on 2026-09-03:
clear/rain/snow transitions, save and area changes, immediate interior reset,
fog, opaque and static-transparent shelter, torch lighting, shadows, A2C,
native-water exclusion, crowded creature trails, player-trail priority, and
the 8192x8192 occlusion target all behaved correctly with no noticed
performance regression. A 16K depth target is supported by the configured
path but consumes about 1 GiB of VRAM and was not part of that runtime pass.
Native Windows confirmation covers rain optics without flicker, snow
accumulation and trails, lighting, precipitation occlusion (including static
transparent blockers), interior reset, and weather changed indoors being
adopted on the next exterior load. Linux separately reconfirmed the same
interior-to-exterior transition using the clean full-effects launcher.

## Next work

Linux sun-shadow fixes (2026-10-31): the caster-toggle isolation test exposed
an independent replay bug: disabled groups returned after changing GL state,
and their stale layers then blocked the complete receiver. The development
build now clears/stamps disabled layers and restores the full saved state;
the world-map term is disabled with Fixed casters. The actual replay function
passes `bash tests/run-caster-toggle-regression.sh` with distinct read/draw FBOs,
both world-map configurations, zero dynamic layers, and byte-exact matrix
restoration. The maintainer confirmed after restarting that disabling Moving
casters preserves the character and red sun diagnostic. Runtime reference:
`launch-linux.sh` -> `run-dev.sh`, native Linux NWN 89.8193.37-17 (26c6e573),
RTX 3060 Ti / NVIDIA 615.78.08, OpenGL 3.3 core, 1920x1006; native geometry
uses 8x MSAA in FBO 4, followed by the single-sample fullscreen receiver;
development library loaded from this checkout, settings/output directory is
the game's `bin/linux-x86`, `NWN_SHADOWMAP_TRACE=1`, settings skipping unset.
Receiver debug 6 isolates raw sun sources before
lighting/fog: red=world static, green=near static, blue=moving. This is a
diagnostic addition, not a confirmed fix for the original camera-dependent
shadow marks. Bias defaults and custom material shaders have not been changed.
With the corrected toggles, the maintainer confirmed that Moving casters OFF
removes the unwanted character marks while building shadows still cover the
character correctly and exposed character pixels are black in raw debug 6.
Moving casters ON / Fixed casters OFF instead shows blue coverage across the
character and its floor shadow. The remaining problem is isolated to moving
directional-shadow reception; the accepted creature-category filtering below
at that checkpoint covered local lights, not directional cascades. Do not infer that
static world-map resolution or custom shaders are the established cause.

An independent fixed-shadow head-darkening bug is now fixed and confirmed in
the same Linux scene: the creature's Mode 2 A2C shader applied directional
shadowing before the fullscreen composite applied it again. Engine-classified
creature A2C draws now defer directional reception to the fullscreen pass,
matching their opaque bodies. Non-creature/unknown A2C materials retain the
existing inline path; coverage, culling, depth writes, local reception and
custom shader resources are unchanged. The script
`bash tests/run-a2c-creature-receiver-regression.sh` exercises the actual receiver
uniform uploads for all three owner classifications. Debug mode 7 restores
the old duplicate creature path for a normal-colour A/B. After restart, the
maintainer confirmed mode 0 removes the extra head darkening and mode 7
brings it back with Moving casters OFF / Fixed casters ON. Leave debug 0
selected for normal use. This was independent of moving sun self-shadowing.

Moving sun self-shadowing is now fixed and confirmed in that Linux scene.
The complete moving map still includes creatures for scenery/placeable
receivers. A parallel cascade array includes only non-creature/unknown draws
using the existing engine object classification, so moving placeables remain
casters on creatures. `shadow_receiver_tags.inc` writes an injector-owned
integer receiver tag alongside the real native material draw, borrowing only
an unused colour attachment 7 and restoring the attachment/draw buffers after
each draw. Native depth tests, discard and MSAA/A2C determine tag coverage;
there is no camera-depth replay, skinmesh test, bucket receiver guess, or
change to the engine's visible geometry. The completed receiver selects the
filtered cascade only for depth-matched engine-classified creature samples.
Missing/unsupported tags retain the ordinary receiver; occupied attachments,
the default framebuffer and fewer than eight draw buffers fail closed.

The first native test revealed patchy classification on sloped MSAA surfaces:
per-fragment centre depth differs from the rasterizer's per-sample depth.
The tag now carries the actual primitive depth-plane span, computed before
discard, along with packed depth/owner type in RG32UI. The regression suite
`bash tests/run-sun-creature-filter-regression.sh` reproduces the sloped failure
with the earlier implementation and passes with the correction. It also runs
the actual receiver GLSL on a software GL context for creature exclusion,
placeable/scenery reception, object shadows on creatures, building shadows,
stale-tag rejection and native multisample A2C coverage, plus draw-state tests.
The maintainer then confirmed a solid green creature in debug 8 and correct
normal shadows without sparkling/self-shadowing, with ground and building
shadows retained and both caster groups ON. Debug 6 remains raw-map evidence;
debug 8 shows receiver classes (green creature, yellow native highlight,
red other, blue unknown).
Debug 0 is the normal-use mode. The diagnostic scene-depth readback now binds
the depth texture explicitly instead of accidentally reading scene colour.
The additional resources are one four-layer moving depth array plus an
8-byte-per-sample receiver tag image; accounting is included in the panel.
The new directional filter has not yet had Windows runtime validation.

Native Linux hover highlights are also confirmed after this change. The
post-scene shadow composite had been darkening NWN's highlighted appearance.
`current_draw_is_highlighted()` reads the engine-owned highlight Gob list,
whose Scene offsets (array +0x80/count +0x88) are established by disassembly
of AddHighlightGob, RemoveHighlightGob and DoHighlightGob. Shared external
owner identity covers the root and attached body parts. The receiver tag
carries an additional native-highlight bit, so the composite preserves the
highlighted appearance without a brightness heuristic; normal shadows return
as soon as NWN removes the object from its highlight list. The maintainer
confirmed consistent hover rendering inside/outside a building shadow and
normal shadow reception after moving the cursor away. Native-list ownership
and highlight removal have CPU tests; actual receiver GLSL has a GPU hover
regression. The Linux Scene layout read is explicitly disabled on Windows
pending separate layout evidence.

Local contact-shadow regression (2026-10-31): the maintainer found that the
character's torch shadow disappeared with Contact-safe local shadows ON and
returned (with unwanted body self-shadowing) with it OFF. The pre-character
depth helper was reusing the completed-scene READ source. Correct replay state
restoration now preserves independent READ/DRAW bindings, so the pre-character
capture must explicitly read the live DRAW target containing this frame's
floor, then restore READ. The completed-scene capture must keep using NWN's
READ target. `capture_pre_dynamic_depth()` now implements that stage-specific
source selection. `bash tests/run-local-contact-depth-regression.sh` reproduced
READ=0/DRAW=4 selecting the wrong source before the fix, and now verifies both
stage sources, default-framebuffer rendering, exact binding restoration and
both blit/copy paths. Sun, hover and A2C GPU regressions still pass. After
restart the maintainer confirmed that Contact-safe local shadows ON restores
the ground shadow without character self-shadowing, with Characters shadow
other characters OFF. Keep that contact-safe configuration; OFF was only the
diagnostic isolation step.

The Windows proxy/artifact, directional-shadow, and local-light parity
checkpoints are now confirmed in game. Directional sun/moon shadows render,
local lights cast visible shadows, and torch-lit surfaces retain their local
lighting through the directional composite without camera-distance changes.
Windows uses a `0` default for the `Bright surfaces keep light` start threshold;
Linux retains its validated `0.85` default.

Windows product settings/logging are also confirmed. The read-only Windows
material transport now has runtime proof for creation, shared-field parsing,
per-draw binding, texture-name lookup, and initialization-time pointer-reuse
reset. The exported MSVC destructors are deliberately refused because their
nominal Subhook trampolines crash when returning to the original function;
Windows resets reused shared identities at `SharedMaterial::Init` instead.
The authored-material census passed a native-area-first transition on
2026-08-26: the same base texture produced distinct stable routes for native
Mode 0, authored Mode 2, and parked Mode 3 materials. Visible Windows Mode 2
A2C is also confirmed: Mode 2 alone selected A2C with live MSAA, while Mode 0
and parked Mode 3 remained native. Directional/local A2C reception, stable
camera motion, engine-budget sun lifting across camera zoom, and selectable
local-shadow refresh are confirmed. The weather port is complete and uses the
accepted Linux shader/runtime behavior with Windows-only bindings. Final
cross-platform regression/performance and a crowded Windows multiplayer trail
recheck remain. Keep Windows-only mechanics behind Windows
paths and do not alter working Linux behaviour to solve a Windows symptom.
Mode 3 OIT is explicitly outside the Windows scope. The staged plan and exit
criteria are in
[WINDOWS_IMPLEMENTATION_PLAN.md](WINDOWS_IMPLEMENTATION_PLAN.md).

As of 2026-10-07, local character reception is back on the accepted
creature-category filter. The pre-character receiver samples the complete local
map, so creature shadows remain attached to floors and scenery. The completed
character-only receiver samples a parallel map that omits engine-classified
creature draws, so creatures neither self-shadow nor shadow one another by
default. Placeables, doors, moving trees, and other non-creature geometry remain
casters on characters. Classification comes from NWN's external object type,
which covers both skinned and rigid/part-based creature appearances. The
development option `Characters shadow other characters` can restore the common
map on character receivers, but defaults off. Contact separation remains zero.
The failed per-owner identity maps and camera-depth duplication are no longer in
the live path. Local bucket replay restores the complete GL state required by
later NWN/A2C draws, including depth/blend/sample state and separate framebuffer
bindings.

The shipping panel now exposes local-shadow refresh as Low (25 ms), Medium
(16 ms), or Ultra (every rendered frame). Low preserves the established cost;
Ultra deliberately matches the sun-shadow update cadence. This cadence does
not change the separate three-light local shadow-map budget or the ordinary
light list used for sun-shadow lifting.

## Parked volumetric fog lighting

The injector now has a sun-lit volumetric fog pass selectable between quarter,
half, and full resolution (quarter by default). It inherits NWN's authored fog
range and color, reconstructs each camera ray from completed scene depth,
marches through the visible medium, and samples the directional static/dynamic
cascade maps at each step. Opaque and alpha-tested casters therefore shape the
lit volume in world space; projected sun position is diagnostic-only and normal
rendering never smears from a clamped screen edge. Every area uses a separate
adjustable near-camera atmospheric density (default `0.32`), combined with any
authored distant fog, so canopy and character occlusion can shape shafts before
the area's fog ramp begins. During visual validation NWN still owns the base fog
extinction and this pass replaces only the old fake shaft extraction with
shadowed in-scattering, avoiding double fog and any shader change to UI/special
passes. This prototype is now parked until its dedicated work resumes. A hard
feature gate prevents the render pass from running, the settings panel does not
expose it, saved values are ignored, its environment variables are ignored,
and the development launcher no longer supplies them. The implementation and
shader checks remain in-tree for compile coverage only; it is not an active
development, production, shipping, or Windows-testing feature.

A native Linux startup check on 2026-09-25 deliberately supplied
`NWN_SHADOWMAP_GODRAYS=1` with settings disabled. Build 8193.37 on an NVIDIA
GeForce RTX 3060 Ti using driver 615.71.09 still reported
`godrays=parked/off`; no godray target, shader, or draw path initialized.

Windows planning must account for:

- proxy-DLL installation and OpenGL/IAT hook parity;
- engine symbol/field evidence rather than Linux offsets;
- directional and local shadow-map parity;
- material identity lifecycle and MTR mode routing;
- MSAA detection and Mode 2 A2C state restoration;
- settings/logging parity and matched Linux/Windows regression scenes;
- the still-unverified Windows `lightpriority` field, which remains disabled.

## Local shadow transition follow-up (2026-10-31)

The maintainer confirmed the contact-safe floor-depth fix: local ground shadows
work without creature self-shadowing. A subsequent report identified frozen
fade-outs without a torch and abrupt fade-ins. Local capture now continues
retiring existing sources even when NWN selects zero lights, so moving casters
are recaptured throughout retirement. Newly admitted sources start at zero
rather than inheriting elapsed idle time; both receiver paths preserve zero
slot fade. Empty native lists with null storage are valid, and area resets
clear join history. Selection remains exclusively NWN's ordered list.

`tests/run-local-fade-regression.sh` exercises the production transition helper
with deterministic idle/admit/retire timestamps and zero fade values. It failed
on idle admission before the fix. Native runtime verification remains pending:
restart `launch-linux.sh`, contact-safe ON and character-on-character OFF, torch
unequipped; walk into/out of local-light range and check gradual admission and
an animated ground silhouette until retirement completes.

## Linux AABB debug renderer guard (2026-11-02)

The WorldMaker Balamb copy-copy export reproduces heap corruption when native
`renderaabb 1` is enabled, including plain NWN without this injector and with
Mesa software rendering. Independent WOK/MDL validation passes. Linux
89.8193.37-17 `PartAABB::Draw` sizes its scratch vertex stream using indexed
vertices, but expands three vertices per face in an uninterrupted color batch.

`shadow_aabb_guard.inc` runs once at first scene entry, after executable globals
initialize and before any AABB scratch allocation. Two adjacent RIP-relative
instruction signatures identify the stream and capacity. If the stream is still
null and capacity is a plausible power of two, reserve 65,536 vertices (enough
for the supported 21,845-triangle limit). Existing allocations and unknown
signatures are left unchanged and logged. No game executable, WOK, surface type,
triangle winding or gameplay collision is modified. Windows is unchanged.

Root Linux build passed. Runtime tests in an isolated authenticated virtual X
server loaded both the original export and the new packed FF8 export. The original
export's colored AABB overlay remains live with the production injector; the plain
client reproduced double-free corruption before the guard. Test profiles and local
assets are private temporary fixtures, not redistributable source artifacts.

## WorldMaker material weather contract (2026-11-04)

The live Linux log showed static WorldMaker scenery predominantly in bucket 1
(e.g. 412 draws versus one in bucket 0). The former alpha-card treatment accounts
for visible rain noise without snow/puddles even in exterior areas. The shader
patch now honors NWN_WORLDMAKER_WEATHER + the material's wmWeatherOpaque uniform:
only an already-eligible static class 2 can become full class 1. Excluded/dynamic
class 0 remains excluded, unmarked stock shaders retain their previous behavior,
and marked cutout/water materials set the uniform to zero. Eligibility still
comes from the engine; this is explicit authored opacity within the static gate.
No per-draw GL query, resource allocation, setting, hook or state restoration
change was added. The custom WorldMaker layered shader also had compact final
output syntax that the strict source patch skipped; its exported output is now
normalized to the native output anchor.

The active WorldMaker checkout's scripts/check_weather_compat.py compiles this
actual patch function, expands locally retained stock includes and uses EGL to
render opaque bucket-1 snow/puddles with cutout/dynamic/interior exclusion checks
at quality 0–3, gamma on/off. --reference-no-promotion reproduces the missing snow.
All checks pass, as do development/shipping Linux builds. WorldMaker provides a
reversible material/shader-only compatibility pack for old exports and a native
wrapper/tag for new ones. Game geometry, textures and edited area flags were not
changed. No game was launched/restarted; native runtime verification remains
pending after the maintainer restarts with the rebuilt plugin and asset patch.

WorldMaker retrofit follow-up (2026-11-04): the first MTR retrofit introduced a
native foliage punchthrough regression by assigning the opaque weather wrapper
to masked native materials as well. All first-patch resources were reverted from
verified backups. The corrected WorldMaker exporter leaves native masked,
transparent and water shader paths intact, including diffuse-alpha legacy cases.
The v2 retrofit emits no native coverage MTR overrides. Existing custom shaders
retain their family with a zero receiver-opacity tag where masked. The shared
plugin change is unchanged; stock/unmarked materials keep their accepted path.
The real patch/exclusion GPU checks still pass, and the new adapter regression
rejects the original wrapping behavior. Native confirmation awaits game restart;
no game was launched or restarted by the correction.
