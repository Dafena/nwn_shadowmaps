# Current task checkpoint

## 2026-08-15 (final) -- CONFIRMED WORKING ON BOTH PLATFORMS

Maintainer-confirmed on Windows AND Linux after this round.

### Fixed here

- **Shipping pins "Local shadow sources" to 3.** A stored value of 1 stuck with
  no control to change it (the combo is `#if !NWN_SHIP`), so only one of two
  visible torches cast. `cap=1` in the log was the tell.
- **Layer allocation is `lights + 1`.** The spare is where a dropped light lives
  while it fades; without it, lights popped instead of fading.
- **`capture_scene_color` drains `glGetError` and latches.** It was reading the
  ENGINE's errors as its own, toggling the self-illumination guard and pulsing
  shadow darkness.

### Priority ordering is OFF on Windows, on purpose

The read at `0x8c + kPartLightDelta` is wrong there -- the log shows `2 0 0` and
`0 0 0`, never a torch's `3`. Gated behind `g_localPriorityOffset != 0`, inert
until verified. `[prio-scan]` prints candidate offsets in `0x70..0xA8`; whichever
reads 3 for a torch is the answer, then set that and re-enable.

**It is Windows-only either way.** The maintainer asked to extend priority
ordering to Linux, then interrupted before it was applied. ASK FIRST.

### Still open

- **Lift saturation** -- `att` clamps across most of a lamp's radius. Real,
  deliberately unfixed, shared with Linux.
- **`kPartLightFadeOff` on Windows** -- `+4` by inheritance, still unverified.
  The priority field being wrong at the same inherited delta makes this MORE
  suspect, not less.
- **Outdoor area entered at night already holding a torch**, never having seen an
  Enable there: no directional shadow until something unambiguous happens.

## PLATFORM RULE (ABSOLUTE, NO EXCEPTIONS)

**If something works on Linux and does not work on Windows, the change is made
FOR WINDOWS ONLY. If the code is shared, you create a SEPARATE PATH for Windows.
You do not modify the shared path. Period.**

This is not a guideline, a default or a preference. It is the maintainer's
standing instruction and it overrides any judgement about elegance, duplication
or "it should be equivalent".

**Applies to shaders as much as to C++.** A GLSL expression used by both
platforms is shared code. Changing its output scale to fix a Windows symptom is
exactly the prohibited move.

**Why, in the maintainer's own words and in scars:**

- 2026-08-15, the sun-shadow lift. Windows: a torch blanked the sun's shadow
  across the whole area. The fix normalised `att` in the receiver shader --
  ONE expression, shared by both platforms and by every lamp -- and it broke
  Linux, which had been confirmed working twice that same day. Reverted.
- 2026-08-13, the local-light caster cull and the current-program lookup. Both
  were Windows cost cuts applied to shared code; both regressed Linux (flicker,
  then a hang). They now sit behind `NWN_WIN_LOCAL_FASTPATH`.
- 2026-08-15, "Light supported". Replaced by a live read of the engine's
  max-lights on BOTH platforms; Windows reported 64 against a shipping default
  of 8 and local lighting visibly regressed. Reverted.

Three times in three days, the same mistake: a Windows problem fixed in shared
code, breaking a working Linux.

**How to comply.** `NWN_WIN_LOCAL_FASTPATH` in `nwn_platform.h` already exists
for exactly this and is the pattern to follow -- a compile-time switch, Linux
keeping the behaviour it was confirmed with, Windows getting its own. For
shaders, branch on a uniform the host sets per platform, or compile a second
variant. Never "improve" the common expression.

**Before touching anything shared, ask: is Linux currently working?** If yes,
whatever you are about to do is Windows-only.

### 2026-08-15 -- torch deleting the sun's shadow on Windows: FIXED, MAINTAINER-CONFIRMED

Symptom: on Windows a torch wiped the sun's shadow across the whole area. Linux
fine. Unaffected by "Lift strength", so not the lamp lift. Started 08-14, which
is exactly when Windows area flags first worked -- the last good build was 08-11.

**Cause.** `CNWCArea::UpdateShadowingLights` has a branch for THE PLAYER CARRYING
A CREATURE LIGHT: at night it calls `AurDisableShadowing(area)` and passes that
light to `AurSetDynamicProjectionLight`. The engine is swapping the area's
directional shadowing for the torch's dynamic projection -- it is NOT saying the
area has no sun shadow.

Windows has only the OBSERVED path, so it saw the disable and dutifully killed
the sun shadow. Linux reads the flag FIELDS, which describe the area and know
nothing about what the player is holding, so it never saw it. That asymmetry is
the whole bug.

**Fix (confirmed in game on Windows).** In `AurSetDynProjLight_detour`: a disable followed by a NON-NULL
projection light is the carried-torch swap. Leave the policy untouched.

**Lesson.** The observed path recovers the engine's DECISION, which is strictly
more information than the flags -- including decisions that are not about area
policy at all. Anything derived from it has to ask WHY the engine decided, not
just what it decided.

Ruled out along the way, each with evidence: the lift maths (byte-identical to
pre-refactor), the lamp census (30 lamps, radii 36-43, all sane), the sun being
in the census (it is not), `kPartLightRadiusOff` (radii read correctly), and
lamp-lift saturation (real, but symptom-independent -- "Lift strength" changed
every other light and not the torch, which is what pointed here).

## START HERE (next session) -- Windows lift bug, leads in priority order

Symptom: on Windows, drawing a torch makes EVERY sun shadow vanish at any
distance, and the "Lights lift sun shadow" checkbox does not stop it. Linux is
fine. Deleting the ini changed nothing.

**Step 0. Confirm the revert took.** Tonight's max-lights change regressed
Windows lighting and was reverted: `lamp_upload_max()` no longer reads
`LightManager::m_nMaxLights` (it reported 64 against a shipping default of 8, so
the lift silently got 8x the lamps) and "Light supported" is back in the panel.
Check Windows lighting looks normal again BEFORE debugging the checkbox, or you
are chasing two faults at once.

**Step 1. LOG THE LAMP CENSUS ON WINDOWS.** Print key, pos, radius, emitter for
every `g_lampLights[i]`. This is the cheapest test and it discriminates all the
remaining theories.

The mechanism to look for: `lampLit` is clamped to 1 for any pixel inside a
lamp, and the shader rejects only `r2 <= 0`. **Nothing rejects an implausibly
LARGE radius.** One oversized entry therefore lifts the sun's shadow across the
whole screen, which is exactly the reported symptom.

**Prime suspect (maintainer's idea, and the best one): the SUN is in the
census.** It is filled from `SetLightGL`, the engine's GL light setup, and a
directional light would carry an enormous or meaningless radius. If one entry
has a radius in the hundreds/thousands or sits at the world origin, that is it.
Fix = reject it where the census packs entries.

NOTE: `lightpriority` cannot be doing this directly -- the injector never reads
it. The testable version of that instinct is the sun-in-census theory above.

**Step 2, only if the census is clean: `kPartLightRadiusOff`.** Every Windows
PartLight offset is `+ kPartLightDelta` (4) BY INHERITANCE, never verified;
`kPartLightFadeOff` is already on file as unconfirmed for the same reason. A
wrong radius offset produces garbage radii and the same global lift. Confirm by
comparing a torch's logged radius against Linux, then delta-scan as was done for
the fade field.

**Defensive fix worth doing whatever the cause:** reject implausible lamp radii
where the census packs them, so bad data can never produce a screen-wide lift.

**Process note.** Three rounds were lost adding gates on the assumption the
switch was broken. The symptom -- EVERYTHING vanishing at ANY distance -- was
already saying the DATA feeding the lift was wrong. Ask what shape the wrongness
is before gating anything.

Last known-good on both platforms: `savepoints/2026-08-15c-sunpath-only-and-face-ladder`.

## 2026-08-15 (late) -- lightpriority + area-policy ambiguity, BOTH CONFIRMED

**Maintainer-confirmed on Windows.** Full reasoning in `AGENTS.md`; summary:

- **`lightpriority` is at PartLight +0x8c.** `Gob::SetLightPriority` writes it
  into each of the Gob's lights rather than storing it on the Gob, so it is a
  plain read. Used on WINDOWS to order the shadow roster, which fixes the
  walking flicker: NWN's list order churns, priority does not.
- **Windows only, deliberately.** The maintainer asked to extend it to Linux and
  then interrupted before it was applied -- so it is Windows-only. ASK BEFORE
  EXTENDING IT.
- **"Disable + a light" is ambiguous** between the night-torch swap and an
  INTERIOR. Resolved by remembering what each area said when no light was
  carried; an area never seen to allow shadows applies the disable.
- **That memory keys on the area's CAurObject, not the Scene** -- the Scene
  pointer is reused across areas, which leaked an outdoor "allowed" into an
  interior and reproduced only on the SECOND entry.

### Still open

- **The lift saturation** (`att` clamps across most of a lamp's radius). Real,
  deliberately unfixed, shared with Linux. "Bright surfaces keep light" and the
  lift toggle are the knobs.
- **`kPartLightFadeOff` / `kPartLightPriorityOff` on Windows** are `+4` by
  inheritance and unverified. Priority is bounds-checked; if the `[prio]` log
  shows all zeros there, that is the offset, not the feature.
- **An outdoor area entered at night already holding a torch**, never having seen
  an Enable there, gets no directional shadow until something unambiguous
  happens (stow the torch, or dawn). Follows from the design; not observed yet.

## 2026-08-15 (night) -- SUN-PATH IS THE ONLY FILL + face ladder + panel cuts

**Linux and Windows both maintainer-confirmed** except the one bug at the end.

### The change that made everything else possible

Local light maps are now filled from INSIDE the engine's own
`Scene::RenderDrawBucket` pass, exactly where and when the sun's cascades are --
`replay_bucket_into_local_light_layers()`. The standalone re-invocation that
`capture_local_light_shadow()` used to do is gone; that function now only
computes and publishes slots and draws nothing.

Riding the engine's visible traversal is what fixes the casters by construction:
the stencil proxies never enter, and the "buckets 2/3 draw nothing cold" warm-up
is unnecessary. Maintainer: "this runs SO much better."

**Legacy / Emitter / High are DELETED.** ~700 lines. Keeping three superseded
fills alive to be picked wrongly was worse than having one.

### Faces: the fix for resolution, not reach

A perspective cone spreads texels by ANGLE, so ground footprint per texel grows
~1/cos^3 off-axis. At the 170 degrees needed for good reach the rim is ~1000x
coarser than straight down -- that was the "resolution is ass" report.

"Local shadow quality": **Low = 1 face (170 deg), Medium = 3 (140), High = 4
(110)**, default High. Faces tilt 45 deg down, spaced evenly in azimuth, offset
half a step so no seam lands on a world axis.

**A face is just another SLOT** -- same position, radius and fade as its light,
its own view/projection. The receiver's per-slot loop already skips slots the
fragment projects outside, so this needed **zero shader changes**.

**TWO FACES IS IMPOSSIBLE**: 180 degrees apart needs a FOV over 180; the limit
is 175. Do not add it.

Measured: 3 lights x 4 faces is ~40 fps on Windows and acceptable to the
maintainer. 1 light x 4 faces is ~1432 draws, about the sun's four cascades,
which measured free on this hardware.

### Panel

- **"Light supported" DELETED.** The lift count now reads NWN's own
  `LightManager::m_nMaxLights` live (the "Lighting Max Lights" video setting),
  exported on both platforms. Shipping our own copy could only ever disagree
  with the engine. `NWN_SHADOWMAP_MAX_LAMPS` still overrides; 0 = follow.
- **"Local shadow sources" is dev-only** (`#if !NWN_SHIP`). Shipping pins 3 and
  lets NWN's "Shadow Casting Lights" gate it.
- **"Local shadow update" DELETED.** Sun-path refills every frame from the
  bucket pass, so a cadence could only throttle slot re-computation.
- **"Lights lift sun shadow" ADDED**, in shipping builds too.

## OPEN BUG -- "Lights lift sun shadow" does not work on Windows

Works on Linux. On Windows the checkbox has no effect.

Two gates are in place and BOTH should independently disable it:
`nwnLocalLightLift` is uploaded as 0 when unchecked (the shader's whole lift
block is gated on `> 0.0`), and `nwnLampCount` is uploaded as 0.

**The first attempt gated only the lamp count. That worked on Linux and did
nothing on Windows** -- same source, same shader. That divergence is NOT
root-caused and is the actual lead: something about uniform upload differs
there, and it is the same family as the caster cull and the current-program
lookup that already needed `NWN_WIN_LOCAL_FASTPATH`. Suspect the uniform
location lookup or the upload site being skipped on that platform before
suspecting the flag.

Ruled out: `lamp_lift` IS registered outside `#if !NWN_SHIP`, so it persists on
Windows normally.

### 2026-08-15 LATE -- the Windows lift bug is probably a BAD PARTLIGHT OFFSET

New symptom, and it reframes everything above: on Windows, drawing a torch makes
**every sun shadow in the scene disappear, at any distance**. Deleting the ini
changed nothing.

That is not "the toggle is ignored". Global disappearance means `lampLit` is 1
for every pixel, which means the shader's `d2 >= r2` reject never fires, which
means the lamp RADIUS is enormous -- i.e. garbage.

**Prime suspect: `kPartLightRadiusOff`.** Every Windows PartLight offset is
`+ kPartLightDelta` (4) and that delta was INHERITED, never verified --
`kPartLightFadeOff` is already on file as unconfirmed for the same reason. If
the radius offset is wrong on Windows, the census packs a garbage radius into
`nwnLamps[].w` and the lift becomes global.

**Check this first, it is cheap:** log `g_lampLights[i].radius` on Windows and
compare against Linux for the same torch. A wildly large or denormal value
confirms it outright. Then delta-scan the struct for the float that behaves like
a radius, as was done for the fade field on Linux.

Note this ALSO explains the earlier confusion: gating `nwnLampCount` looked like
it did nothing on Windows partly because the underlying data was wrong, so
neither gate was addressing the real fault.

Defensive fix worth doing regardless of the cause: clamp/reject implausible lamp
radii where the census packs them, so bad data can never produce a screen-wide
lift.

## STILL NOT STARTED -- lightpriority

`Gob::SetLightPriority(int)` and `Gob::CollectLights(List<PartLight*>&)` are
exported. Never begun, because the question was never answered: is it about
WHICH LIGHT CASTS (selection, currently `GetShadowLights()` order) or about the
SUN-SHADOW LIFT? They are different subsystems and need different work.

## NEXT TASK (planned, NOT started) -- fill local light maps the way the SUN does

**Maintainer's framing, 2026-08-15: "the way to get the shadows should be the
sun's cheap way, the way to render them should be the current one."** Keep the
local light system as it is; replace ONLY how the depth map gets filled.

### The divergence this fixes

| | sun cascades | local lights (today) |
| --- | --- | --- |
| when the bucket is replayed | INSIDE the `Scene::RenderDrawBucket` hook, right after `CALL_ORIGINAL` | standalone re-invocation from `capture_local_light_shadow`, outside the engine's sequence |
| what the casters are | whatever the engine just rendered VISIBLY | whatever the engine has staged at that moment -- includes stencil/proxy content |
| warm-up hacks needed | none | `g_localLightWarmup`, `g_localLightWarmPerSlot`, clear-between-buckets |

The proxy contamination and the "buckets 2/3 draw nothing cold" workaround are
both symptoms of calling at the WRONG TIME, not of missing a filter. Filtering
proxies out was considered and rejected in favour of this: fix the cause.

### Keep unchanged (explicitly)

Light selection and the tiers; the per-light view/projection (downward cone, FOV,
near/far from the light radius); `g_localLightFaceVP[]`; the layer-per-light
depth array; and the ENTIRE receiver side -- slot loop, positions, radii, the
three fades, bias, PCF. Same maps, same look. Only the moment and the mechanism
of filling change.

### The change

Add `replay_bucket_into_local_light_layers(scene, bucket)`, modelled directly on
`replay_bucket_into_cascade_layers()` (shadow_replay.inc:673), and call it from
the same site in the bucket hook (shadow_trace_cascade.inc, beside the
`useBucketReplay` call). Per published slot: attach the layer with
`FramebufferTextureLayer`, memcpy that slot's view/proj into the engine's matrix
stack, the `SetMatrixMode`/`FlagDirty`/restore-mode dance, replay via
`guarded_render_bucket`, then byte-exact restore of the camera matrices.

Clear each layer once per frame on first touch (the `targetFrame != serial`
pattern the cascades use), and publish the generation when the bucket sequence
ends.

Then retire the standalone path: the warm-up latches, the dynamic-only
clear-between-buckets, and `capture_local_light_shadow`'s bucket loop.

### Phase 2, and the reason this gets CHEAPER not just cleaner

The cascades' static cache is what makes the sun affordable: a layer already
holding valid static depth is re-stamped fresh and the replay is skipped
entirely. That applies BETTER to local lights -- a wall lamp does not move, so
its static geometry needs capturing once. Emitter currently re-renders
everything on every capture.

### Known risks, decide during implementation

1. **Re-entrancy.** We would call `SceneRenderDrawBucket` from inside its own
   hook. The sun already does exactly this and guards with
   `g_cascadeReplayActive` + `guarded_render_bucket`; the local equivalent is
   `g_localLightPassActive`. Both must be set or our replay captures itself.
2. **The engine's traversal is CAMERA-culled.** Fine for the sun (near casters
   are on screen, and the code says so). For a local light the casters that
   matter are near the LIGHT -- usually on screen too, but a lamp just off the
   bottom of the screen may lose casters. NOT a regression: the standalone call
   has the same limitation.
3. **Cost is per light per bucket.** Engine-side draws with no per-caster CPU
   duplication, which is why Emitter is cheap and High is not -- but 3 lights
   multiplies the bucket set. The static cache is what pays for it.
4. **Byte-exact matrix restore** is the project's established rule here;
   reconstructing the camera afterwards caused the old corruption bugs.

### Open question

If this yields visible-mesh casters at Emitter cost, **High may become
redundant** -- its only remaining advantage would be `render 1 shadow 0` pieces
(a head, a cloak) that the engine excludes from shadowing. Do not remove High
until that is measured in game.

## 2026-08-15 (evening) -- local shadow reach + panel simplification (CONFIRMED)

**Maintainer-confirmed on Linux the same day.** Three changes, all in one run:

1. **Local shadow reach.** The capture cone, not the light radius, was limiting
   it. FOV 140 -> **160**, reach 2.75x height -> 5.67x. Confirmed "so much
   bigger". The shader's texel-size constant was hardcoded for 140 and is now the
   `nwnLocalTanHalfFov` uniform -- a latent acne bug that any FOV tuning would
   have triggered.
2. **"Local shadow range" deleted.** It was read by nothing; it fed the cube
   probe's eligibility test, removed in the overnight audit. Control, variable
   and config key gone.
3. **"World extent" is now four steps (32/64/128/256)** with labels stating the
   real trade. 256 is the correct ceiling -- it covers a 512-unit box and the
   largest NWN area is 320 units.

Full reasoning in `AGENTS.md`. Headroom on the FOV exists (175 is the hard
limit) but costs sharpness and distorts the cone edge past ~170; 160 is the
deliberate stopping point.

## 2026-08-15 (overnight) -- FULL AUDIT: cube probe + last diagnostics removed

**MAINTAINER-CONFIRMED IN GAME 2026-08-15.** Linux renders correctly after the
cube-probe removal, and Windows area shadow flags work -- the first time that
feature has ever functioned on that platform.

830 source lines gone, ~23 KB off each binary, all three rebuilt and installed.
Details and the full keep/remove table are in `AGENTS.md` under
"AUDIT 2026-08-14 (overnight)". Summary:

- the Phase 7a six-face **cube probe, entirely** -- CPU machinery, its GLSL in
  the receiver, the hot draw macro's branch, and its four env vars
- the `selfcheck` PGM block (~158 lines)
- 13 write-only globals, two unreachable bucket arrays

**New tool: `check_shaders.py`.** The receiver shader is assembled from C++
string literals and only compiled inside a live GL context -- a syntax error is
invisible until the game runs, and it is the pass that draws every shadow. This
extracts each shader as the C++ concatenates it and runs `glslangValidator`.
It caught a real half-finished edit during this pass. **Run it after any edit to
those strings.**

### Confirmed by that run

- the receiver still binds and shades correctly after the cube GLSL was cut out
- the per-draw macro still duplicates casters with its cube branch removed
- the observed `Aur*` policy path is correct -- it drives Windows outright, and
  on Linux it ran beside the field-reading hook without raising the disagreement
  warning

## 2026-08-14 (later) -- WINDOWS AREA SHADOW FLAGS SOLVED + DIAGNOSTICS STRIPPED

**Built on both platforms. NOT yet confirmed in game -- that is the next test.**

### Windows area shadow flags

Closed by observing the engine's DECISION instead of recovering the fields.
`nwmain.exe` exports nothing of `CNWCArea` and the function is not virtual, so it
was never hookable there. But `CNWCArea::UpdateShadowingLights` (Linux
`0x705830`) ends in three calls that ARE exported on Windows:
`AurEnableShadowing` / `AurDisableShadowing`, then a tail call to
`AurSetDynamicProjectionLight`. Every reference to that last one in the entire
binary is a tail jump from inside `UpdateShadowingLights`, so it disambiguates
the area's toggle from `CNWCVisualEffectOnObject::EnableHardCodedEffectShadow`.
Full reasoning in `AGENTS.md`.

**What to look for on the next Linux run:**
`[shadowmap][area] observed Aur* shadow-policy hooks active (cross-checked
against the flag fields)`. Linux runs BOTH paths; the observed one warns once if
it ever disagrees with the fields. No warning across a day/night transition means
the Windows path is proven, because Windows has no ground truth of its own.

**Gap:** `ShadowOpacity` (+0x104) is not recovered on Windows -- on/off policy
applies at unmodulated strength. `shadowalpha` was NOT substituted for it: it is
creature-driven and orthogonal to the flags, so the resemblance in the 2026-08-12
calibration is not evidence.

### Diagnostics stripped

Gone: the `[probe]` shadowalpha sampler and its `PartLight` field delta scan, the
`[probe][stencil]` logging and attached-shader dump, the `[trans]` trace,
`dump_part_light_struct` + `g_localLightStructDump`, the `[local-light][probe]`
ring-sample CPU mirror, `g_stencilDraws`, and the env vars
`NWN_SHADOWMAP_LOCAL_LIGHT_STRUCT` / `NWN_SHADOWMAP_PROBE_INTERVAL`.

**KEPT, and do not "finish the job" by removing it:** `g_stencilPrograms` /
`g_stencilProgramCount` reads as probe scaffolding but is load-bearing --
the draw interceptor learns the engine's stencil programs there and
`local_visible_draw_eligible` uses the list to exclude them from the
visible-caster duplication. Removing it puts proxy geometry back into High's maps.

Still present and deliberately untouched: the `selfcheck` block in
`shadow_local_lights.inc` (~180 lines, face coverage stats + `.pgm` dump) behind
`g_localLightTrace && g_dumpCapturePgm`. It predates the fade work and was not on
the strip list.

## NEXT SESSION, IN ORDER

Everything below 1 is blocked on 1: three separate pieces of work landed today
without a single in-game run, so the first job is finding out which of them are
actually true.

**1. CONFIRM TODAY'S WORK IN GAME (Linux first).** One session covers all three:
   - **Area flags.** Expect `[shadowmap][area] observed Aur* shadow-policy hooks
     active (cross-checked against the flag fields)` at startup. Then cross a
     day/night transition (`##dm_settime`) and confirm NO disagreement warning
     appears. That warning is the only validation the Windows path can ever get,
     because Windows has no ground truth of its own.
   - **Diagnostics strip.** Confirm local shadows still look right. The risk is
     not the removed prints, it is whether anything load-bearing went with them
     -- specifically that High's maps still exclude the engine's stencil proxies
     (`g_stencilPrograms` was deliberately KEPT for this).
   - **Tiers.** The stored index renumbered twice; re-pick a tier and confirm
     Legacy / Emitter / High still behave as expected.

**2. THEN THE SAME ON WINDOWS.** Area flags have never worked there, so this is
   the first time the feature can be seen at all. `bin/win32/version.dll` is
   already the current build.

**3. `ShadowOpacity` on Windows.** Not recovered by the observed path -- policy
   applies at unmodulated strength. Do NOT substitute `shadowalpha`: it is
   creature-driven and orthogonal to the flags, so the 2026-08-12 calibration
   resemblance is not evidence. Needs its own route or an explicit decision to
   ship without it.

**4. Tracy.** Enabled in both `settings.tml` copies, never run. The question it
   should answer first is the Windows multi-light cost (~3-4 fps per added
   light), which so far has only been measured through our own `[cost]` lines --
   i.e. blind to what the injector does to the ENGINE's zones. Use `capture` +
   `csvexport` to compare 1 vs 3 sources numerically rather than eyeballing the
   flame graph.

**5. `kPartLightFadeOff` on Windows.** Assumed `0x94` by inheriting
   `kPartLightDelta = 4`; never confirmed. First suspect if per-light fade-in
   misbehaves there while the day/night fade works.

**6. Optional cleanup.** The `selfcheck` block in `shadow_local_lights.inc`
   (~180 lines, face-coverage stats + `.pgm` dump, behind
   `g_localLightTrace && g_dumpCapturePgm`) survived the strip because it was not
   on the list. Remove it or keep it deliberately.

## STILL WIP / NOT DONE

- **`ShadowOpacity` on Windows** is still not recovered -- the on/off policy
  works there now, but at unmodulated strength. See item 3 below.
- **`kPartLightFadeOff` unverified on Windows.** Assumed `0x94` by inheriting
  `kPartLightDelta = 4`. First suspect if per-light fade-in misbehaves there
  while the day/night fade works.
- **`ShadowOpacity` on Windows** -- see the gap above.
- **Tracy is enabled but never run.** `settings.tml` now has
  `[instrumentation.tracy] enabled = true` in BOTH the Linux path and the Proton
  prefix (backups: `settings.tml.bak-preTracy`). Nobody has connected the
  profiler yet. This is the right instrument for the Windows multi-light cost
  question, which was last measured only through our own `[cost]` lines.
- Savepoint taken: `savepoints/2026-08-14-tiers-platform-split-windows-area-flags`
  (source + all three binaries + `MANIFEST.sha256`, verified). It is snapshotted
  as BUILT, not as confirmed -- see item 1 above.

## 2026-08-14 -- LOCAL SHADOW SOURCE TIERS + PLATFORM SPLIT (maintainer-confirmed)

**Both platforms rebuilt from current source and confirmed working in game.**
`bin/win32/version.dll` and the Linux `.so`/deploy `.so` all match this tree.

### The control

"Local shadow sources" selects a (method, light count) pair. The persisted
`local_cube_sources` value is the COMBO INDEX, decoded in `shadow_targets.inc`.

| index | tier | fill | count |
| --- | --- | --- | --- |
| 0 | Legacy | bucket replay, honours "moving casters only" | 3 |
| 1-3 | Emitter | bucket replay, all four buckets, nothing cleared | 1-3 |
| 4-6 | High | per-draw duplication of the visible meshes | 1-3 |

Default index 4 = **High, one light**. The tiers differ ONLY in fill: all three
take `GetShadowLights()` in NWN's order and stop at the game's three
shadow-casting lights.

**The index renumbered twice in one day** (census removal, then dropping the
4th Emitter slot). A config from an earlier build parses but means a different
tier. No migration was written; the maintainer re-picks. If it moves again,
consider storing a NAME instead of an index.

### What this cost, worth not repeating

1. **Emitter is the 2026-08-10 capture, reproduced literally.** The proxy meshes
   it cast came from `g_localLightDynamicOnly` -- a flag that does not exist in
   that savepoint (`grep -c` = 0) and defaults ON today. It erases the static
   buckets and keeps 2/3, where the stencil proxies live. The FILL was never
   wrong. Two rounds were lost re-pointing Emitter at different fills before
   reading the savepoint's capture function, which settled it in one look.
2. **The Linux flicker was a Windows optimisation running on Linux.** The
   per-light caster cull manufactures empty layers by design; Linux's publish
   rule required EVERY layer to have drawn, so one empty layer discarded the
   frame's capture and alternated with the bucket fallback.
3. **Loosening the publish rule for both platforms hung the game.** Root cause
   never established. Linux was REVERTED to savepoint semantics rather than
   patched forward, and the divergence was made explicit instead.
4. **Census selection was granted as an exception to the engine-authority rule,
   then withdrawn** the same day. Every tier takes `GetShadowLights()`.

### The rule that came out of it

`NWN_WIN_LOCAL_FASTPATH` (`nwn_platform.h`). **If a change helps one platform and
hurts the other, it gets a switch.** Three items ride it as ONE decision --
caster cull, current-program source, publish rule -- because the publish rule has
to match whether the cull is on.

Verify by preprocessing with the REAL cross-compiler, not `-D_WIN32` on the
native one (that silently yields empty output, no `windows.h`):
`x86_64-w64-mingw32-g++ -E ... | grep -A6 local_current_program`.

## 2026-08-13 -- LOCAL LIGHT SHADOW FADING (complete, maintainer-confirmed)


Three separate fades now exist. They are different mechanisms for different
causes and must not be merged:

| fade | driven by | when |
| --- | --- | --- |
| day/night | `shadowalpha`, an engine global | dawn/dusk; the whole local term |
| per-light on/off | `PartLight +0x90`, NWN's own light fade | a light turning on or off |
| selection in/out | our join table | NWN adding/dropping a light from its set |

### 1. Day/night: `shadowalpha` (engine global, float 0..1)

Ramps continuously with time of day. Across one dawn it traces a **V**: it falls
to ~0 as the moon's shadows go out, then climbs back to a steady 0.5 as the sun
takes over. One scalar, two different shadows.

We want only the falling half, so `update_engine_shadow_fade()` tracks a
**peak-relative ratio that can only decrease** while a light is held:

* the peak is learned before the fall begins, so absolute scale does not matter;
* once falling, a later rise cannot bring the shadow back -- that is what stops
  the sun's half of the V from re-lighting a local shadow;
* it resets to fully visible when a DIFFERENT light is selected, so a torch lit
  at noon still casts.

Deliberately NOT 1:1 with the engine. Matching it exactly would mean separating
static from dynamic casters per light; the maintainer explicitly accepted the
approximation.

### 2. Per-light: `PartLight +0x90` (float 0..1)

Found by delta-scanning the struct across a transition: it ramped
`0.1774 -> 1.0000` in even ~0.10 steps and then held exactly 1.0, with the
radius at `+0x70` growing alongside. Read into `PartLightInfo::fade`, carried
per slot, multiplied into that slot's term. Reads 1.0 whenever a light is
steady, so it only matters when one turns on or off.

### 3. Selection: the join table (`shadow_targets.inc`)

**Keyed by light IDENTITY, not slot index.** NWN reorders its priority list
constantly as the player moves; an index says nothing about whether this is the
same light as last frame. Every bug in this area came from ignoring that.

**One table drives both directions.** A light NWN has dropped keeps its entry
with a decaying level and is **still published as an ordinary active slot**
until it reaches zero. Fading out is therefore the same in-band mechanism as
fading in -- staged, filled and published like any other slot, with no gap to
survive.

**Cost, accepted:** a fading light still costs a capture, so 3 churning lights
can briefly mean 4 captures for up to `kLocalRetireFadeSeconds` (0.35 s).

### What was tried and did NOT work -- do not re-run these

| attempt | why it failed |
| --- | --- |
| Scale the local term by raw `shadowalpha` | It reads ~0.025 mid-transition. Multiplying raw **removed local shadows entirely**. The problem was NORMALISATION, not where it was sampled |
| Dismiss `shadowalpha` as a startup constant | The run never crossed a transition. A value that looks static may only be unobserved |
| Fade on `local_map_still_fresh()` going false | Walking out of a light's radius leaves the map fresh and the light selected, so the trigger never fired |
| Fade by the light's RGB | Binary: full colour or zeroed. Never ramps |
| Area shadow flags / opacity | Not involved. The maintainer ruled this out first and was right |
| Copy the dropped light's depth into a spare layer and publish it as an extra "retirement" slot | Worked, then broke three times in a row: it had to survive staging, `g_haveLocalLightVP` going false, and the light rejoining. Each fix exposed the next. **Superseded** by keeping the light in the slot list |

### Probing technique that actually found these

* **`[probe]`** -- 10 Hz, prints only on change plus a 5 s heartbeat:
  `shadowalpha`, the selected light's RGB and fade, `localSlots`, caster draw
  counts, `engineFade`.
* **Field delta scan** -- snapshots the selected `PartLight` and prints only the
  offsets whose float value MOVED. This is what found `+0x90` without
  disassembling anything.
* **Stencil-hook probe** -- reads `shadowalpha` either side of
  `Scene::RenderShadows`. **Requires "Hide the game's own shadows" OFF**: with
  it on we return before `CALL_ORIGINAL`, the engine pass never runs, and there
  is nothing to observe. The build says so in the log if you forget.
* **`[trans]`** -- per-frame state of every gate that can hide a local shadow
  (`haveVP`, `slots`, `ready`, `deselect`, join levels, radii), on change only.

All are `g_traceEnabled`-gated and read-only. **They are still in the build and
should be stripped before shipping.**

### Instrumentation lessons (this session added three)

* A value that looks static may simply be **unobserved** -- check the
  measurement window contains the event before concluding anything.
* **Suppressing the engine's pass stops the engine updating its state.** With
  "Hide the game's own shadows" on, `shadowalpha` cannot be read meaningfully.
* When both a value and a mechanism are candidates, print **both** (float and
  int views, in and out of a call) rather than picking one.



## 2026-08-13 -- local shadows now follow NWN's own day/night opacity

Reported: NWN's stencil creature shadows fade gradually from night to day
(opacity 100 -> 0, and back up toward night). Ours reproduced the effect but
INSTANTANEOUSLY -- the shadow survived at full strength and then vanished.

**Cause: `shadowalpha`, an engine global we never read.** It is a float 0..1 and
it ramps continuously with time of day. Our local term used a fixed
`g_localLightStrength`, so nothing faded; the only thing we saw was NWN finally
dropping the light from `GetShadowLights()`, which is a single frame.

Now `nwnLocalLightStrength = g_localLightStrength * shadowalpha`. Out-of-range
or unresolved means 1.0, so another engine build cannot silently erase local
shadows.

Also wired, from the same investigation: **`PartLight +0x90` is the per-light
fade, 0..1** (found by delta-scanning the struct across a transition -- it
ramped 0.1774 -> 1.0000 in even ~0.10 steps while the radius at +0x70 grew with
it). It is carried per slot as `nwnLocalSlotFade[]` and multiplies that slot's
term. It reads 1.0 whenever the light is steady, so it changes nothing outside a
light turning on or off.

### How this was found, and the mistake in the middle

The engine offers several plausible curves and only measurement separated them:

| candidate | verdict |
| --- | --- |
| `shadowalpha` | **the driver.** Ramps with time of day |
| `PartLight +0x90` | real per-light fade; 1.0 while a light is steady |
| light RGB | binary -- full colour or zeroed, never ramps |
| area shadow flags / opacity | not involved; the maintainer ruled this out first |
| `GetShadowLights()` membership | binary, and only the final drop |

**`shadowalpha` was wrongly dismissed once.** The first probe caught two samples
(0.5, then 1.0), and that was called a startup settle. The run simply never
crossed a real transition, and the probe printed only on change so it showed
endpoints. The lesson is the one this project keeps relearning: a value that
looks static may only be unobserved -- check that the measurement window
contains the event before concluding anything from it.

The probe that settled it prints, at 10 Hz on change: `shadowalpha`, the
selected light's RGB and fade, `localSlots`, area ambient/diffuse, plus a
per-field delta scan of the selected `PartLight`. It is read-only and gated on
`g_traceEnabled`.



## 2026-08-13 -- cadence flicker fix + Ultra level

**Flicker after the cadence gate landed: the receiver was rejecting the map.**

```c
g_localLightVPFrame != 0 && localAge <= 1   // age in FRAMES
```

The receiver only composited a local map published within the last **one
frame**. That is always true while the capture runs every frame, so the rule was
invisible until the cadence actually throttled captures -- then every frame
between publishes failed it, the local term was dropped, and the shadow blinked
at exactly the cadence rate. The maintainer's report was precise: it used to
"just update with a slower cadence" without flicker.

The rule guards something real ("anything older would mean the capture has
stopped"), but **a frame count cannot distinguish stopped from throttled**.
Freshness is now measured in SECONDS against the configured interval, via
`local_map_still_fresh()` in `shadow_overlay_runtime.inc`:

```c
age <= max(0.25, g_localCubeUpdateSeconds * 3.0)
```

The x3 tolerates a missed capture or a hitch without blinking; the 0.25 s floor
keeps fast cadences from being hair-triggered. The cube path always measured
freshness this way -- this makes the production path agree. The panel's
`localReady` readout uses the same helper, or it would say "not ready" while the
shadow was visibly on screen. **Maintainer-confirmed working.**

**New "Ultra (every frame)" level.** Cadence 0.0: the gate is
`now - last < interval`, so a zero interval never holds and the capture runs
every frame -- the pre-cadence behaviour, now as a deliberate choice rather than
a limitation. `kLocalCubeCadenceSeconds` grew a fifth entry and now drives
`kLocalCubeQualityCount` / `kLocalCubeQualityMax`; every clamp, the name table
(with a `static_assert` tying it to the cadence table) and the env quantiser
read from those, so adding a sixth level cannot leave a clamp behind. The
freshness floor of 0.25 s still rejects a genuinely stopped capture at Ultra.

## 2026-08-13 (late) -- three changes after the audit

1. **`local_cube_sources` now defaults to 3** (`shadow_targets.inc` and
   `settings_reset_defaults()`). Three is NWN's own ceiling for shadow-casting
   lights, and the real cost is governed by the game's `Shadow Casting Lights`
   slider, which decides how many candidates `GetShadowLights()` hands us.
   Consuming fewer than the engine offers only drops shadows without saving
   work. "Restore defaults" now gives three.

2. **The dead "Shadow light" picker is gone** -- widget, tooltip,
   `NwnOverlayState::localPick` and the `g_localLightPick` global. It was
   write-only, and its tooltip still described the pre-engine-selection failure.
   `localPickCount` stays: it is the live census readout.

3. **The cadence combo now works.** This was the reported bug.

   `g_localCubeUpdateSeconds` was only ever consumed by the diagnostic cube
   (`g_localCubeLastCaptureTime`). The production capture used a separate
   timestamp that nothing compared against it, so every quality behaved
   identically and the panel looked broken.

   The old code refilled every live area frame on purpose: staging clears the
   layers the receiver samples and unpublishes the map until the next dynamic
   stage refills them, so throttling the REFILL would show a gap. **Gating the
   STAGE has no such problem** -- skipping it clears nothing, and the previously
   published generation, matrices and slot data all stay live. The map simply
   goes up to one interval stale, which is what the setting asks for. No
   double-buffering needed.

   The gate holds only a published map (`g_haveLocalLightVP`), never before the
   first capture, and never across a policy-epoch change, so a settings edit
   applies on the next frame rather than after the old deadline.

   The panel tooltip claimed the control was "reserved ... for the future
   double-buffered local map"; it now describes what the setting does and says
   to lower it before lowering map size.

Both builds clean, `ldd -r` clean. **Needs an in-game check:** change "Local
shadow update" between Low and Extreme and confirm the local shadow visibly
lags/tightens, that nothing flickers at Low, and that the log's
`[shadowmap][local-light] policy:` line reports the new cadence.

## AUDIT 2026-08-13 (late) -- code read as source of truth, docs corrected

The docs had fallen behind the code. Verified by reading the source, not chat:

**Done and confirmed, previously listed as pending gates:**
- Visible-geometry local casters: in, and the acceptance test passed.
- Multi-source local lights: in and confirmed in game. The maintainer runs 3.
  `REFACTORING.md`'s "next gate is a two-source in-game confirmation" was stale.

**Findings that need a decision (nothing changed by the audit):**

1. **`local_cube_sources` ships as 1, not 3.** `shadow_targets.inc:104` and
   `settings_reset_defaults()` both say 1; the maintainer's three comes from a
   SAVED `.ini`. "Restore defaults" and every fresh install give one source.
2. **The "Shadow light" picker is a dead control.** `g_localLightPick` is bound
   to the panel and written by it, and **read by nothing** -- 2 references in the
   whole tree, the declaration and the binding. It is a leftover from the cube
   probe, when it chose which single light the cube followed. Its tooltip still
   describes that era ("in practice one light attached to the player ... nothing
   ever appeared"), which now contradicts the engine-authoritative design.
   Remove the control and the tooltip, or wire it to something.
3. **`win/version.dll` was a day stale** -- built 08-12 02:04, before the
   visible-caster and multi-source work. It still builds cleanly from the parent
   tree (rebuilt during the audit, 0 warnings), but `win/Makefile` reads
   `../nwn_shadowmap.cpp`, i.e. the pre-split monolith, while `refactored/` is
   the authoritative workspace. **The Windows port has no path to the authoritative
   tree.** Decide: point `win/` at `refactored/`, or accept that Windows tracks a
   frozen parent. The deployed `bin/win32/version.dll` was NOT touched.

**Verified healthy, no action needed:**
- development and shipping builds: 0 warnings, 0 errors
- `ldd -r`: no undefined symbols in either
- each of the ten `.inc` modules is included exactly once from the root TU
- shipping trim correct: the five development-only panel controls are absent
  from `libnwn_shadowmap_deploy.so` and present in the development build
- 33 persisted settings keys, all reachable from a panel control on their build


Updated: 2026-08-13

This is the authoritative recovery point for the active code change. Read this
file first after a context compaction. Do not reconstruct the task from chat.

## Objective

Activate the existing multi-source local-light depth-array path for two
simultaneous NWN-selected lights, while preserving the runtime-confirmed
single-light path when the source budget is set to one.

## Non-negotiable behavior

NWN remains authoritative for choosing local shadow lights. Consume
`LightManager::GetShadowLights()` order verbatim. Do not rank lights by camera
distance, visibility, inferred owner, actor distance, or any injector heuristic.

Caster truth table:

| MDL state | Local shadow-map caster |
| --- | --- |
| `render 1`, `shadow 0` | yes |
| `render 1`, `shadow 1` | yes |
| `render 0`, `shadow 1` | no — invisible stencil proxy |
| `render 0`, `shadow 0` | no |

Operationally, classify by the engine's real normal-colour submission rather
than attempting to recover MDL flags from memory. A draw with visible RGB output
in the normal dynamic stage is `render 1`; depth/stencil-only draws and learned
stencil programs are excluded.

## Confirmed current state

- Engine local-light selection is solved. Do not alter it.
- Point-light receiver controls work.
- The refactored single-light build passed an in-game runtime test with no
  behavior regression.
- Visible normal-renderer dynamic geometry supplies local casters: both
  `render 1 shadow 0` and `render 1 shadow 1` cast; invisible
  `render 0 shadow 1` stencil proxies do not.
- Sun CSM, alpha casters, camera stability, persisted settings, and the overlay
  remained working after the modular split.

## Frozen runtime checkpoint

- Post-refactor, pre-multilight savepoint:
  `../savepoints/2026-08-13-refactored-single-light-runtime-confirmed`.
- Working copy: this `refactored/` directory only. The original and savepoint
  stay untouched.
- Extracted separate objects: `shadow_config.cpp`, `shadow_math.cpp`.
- Extracted same-translation-unit modules: `shadow_gl_api.inc`,
  `shadow_engine_bindings.inc`, `shadow_targets.inc`,
  `shadow_diagnostics_settings.inc`, `shadow_replay.inc`,
  `shadow_shader_interposition.inc`, `shadow_fullscreen_receiver.inc`,
  `shadow_overlay_runtime.inc`, `shadow_trace_cascade.inc`, and
  `shadow_local_lights.inc`.
- `nwn_shadowmap.cpp` is now 3,024 lines instead of 12,152. The extracted
  modules remain in the same translation unit and preserve source order.
- Development and shipping builds pass.
- Exported ABI matches the confirmed savepoint and `ldd -r` is clean.
- The preprocessed source before/after the large split is byte-identical, with
  SHA-256 `2341d35523c4671c4f04485adb76975a99f777370a6f3423e82497247a4c5aa7`.
- The nested launcher path is fixed: `run-shadowmap-trace.sh` finds
  `../../nwmain-linux`, supports the legacy `../nwmain-linux` layout, and accepts
  `NWN_SHADOWMAP_GAME_DIR` as an explicit override.
- Full rationale and module map: `REFACTORING.md`.
- Runtime confirmation and exact library/source hashes are in the savepoint's
  `MANIFEST.md`.

## Current implementation: first multi-light slice

The regular local depth target, matrix arrays, receiver loop and visible-draw
duplication were already slot-aware. The remaining policy coupling was removed:

1. Persisted `local_cube_sources` (overlay: **Local shadow sources**) now owns
   the regular local slot budget, clamped to NWN's native 1--3 maximum.
2. Slots consume `g_engineShadowLights[]` in NWN's exact order. No injector
   ranking or ownership heuristic is allowed.
3. **Light supported** now controls only the cheap light/lift census and cannot
   change the expensive shadow source count.
4. The panel reports the actual live shadow source count.
5. Default remains one, preserving the frozen runtime-confirmed fallback.
6. A staged generation is published only when every requested slot records at
   least one visible dynamic draw. A partial set cannot expose a cleared layer.
7. The current local target is single-buffered. Its depth layers refresh every
   live area frame: throttling a clear/refill cycle exposes a blank map and
   flickers. The saved cadence selector is reserved for a future ping-pong
   target; it must not gate capture before then.

Both development and shipping libraries were force-rebuilt on 2026-08-13 and
pass `ldd -r`. Current hashes are:

```text
dcb50beb7a7ca76247f6e89f3b1c3fab52b74677af021354b23165c291a54f65  libnwn_shadowmap.so
6943cc93ecb9004d08872f67c88a3abdb4b176f4adbdce666aa68a02b1a3412c  libnwn_shadowmap_deploy.so
```

This implementation is compiled but not yet runtime-confirmed. First test the
saved one-source fallback, then select two sources. The injector is capped at
NWN's native three local shadow sources.

### 2026-08-13 — local source/cadence activation repair (awaiting runtime test)

The panel values had persisted correctly, but the production visible-dynamic
local-light bridge was still preparing a new target every area frame. Therefore
the **Local shadow update** selector (Low 66 ms / Medium 33 ms / High 25 ms /
Extreme 16 ms) was cosmetic, and a source-budget edit could leave a target staged
with the previous source count.

The current build fixes this without changing NWN selection:

1. `apply_local_cube_runtime_settings()` now treats quality, local-source budget,
   and range multiplier as one capture-policy change. It forces the next map
   generation while allowing an already-staged map to complete safely, retaining
   the last completed map for the receiver.
2. `capture_local_light_shadow()` schedules only a **new** visible-dynamic map at
   `g_localCubeUpdateSeconds`; a same-frame completion/fallback still gets
   through immediately.
3. `SceneRenderDynamicGeometry_detour()` timestamps only a fully completed
   visible map, so the cadence measures completed maps rather than a premature
   staging call.
4. `g_localLightSlotCount` is now published only after all selected layers
   complete. The overlay's “live shadow sources” means actual receiver-ready
   layers, never a merely requested count.
   It also displays the authoritative NWN candidate count beside that number,
   so a one-entry engine list cannot be mistaken for a failed two-source budget.

Expected one-time log after a setting edit:

```text
[shadowmap][local-light] policy: sources=2 update=Extreme (16 ms) range=0.25x from panel
```

The normal engine `GetShadowLights()` order remains the only authority for which
one or two lights fill those slots.

The receiver currently consumes the previous frame's local map by design because
capture runs after the receiver. Keep map matrices and depth contents from the
same generation. Do not overwrite the published matrix metadata with a newer
light generation before the receiver consumes the corresponding texture.

## Explicitly out of scope for this patch

- Changing light selection or priority.
- Sun CSM behavior.
- Receiver math, PCF, bias, attenuation, or UI controls.
- Static-world caster support unless required after dynamic visible geometry is
  proven. The immediate regression is visible character geometry.
- Alpha sorting.

## Acceptance test

Launch the regular development path and inspect the same character group:

- A visible head/body piece with `shadow 0 render 1` casts.
- A visible piece with `shadow 1 render 1` still casts.
- An invisible `shadow 1 render 0` stencil proxy does not become the silhouette.
- The selected light is exactly the same light NWN selected before this patch.
- Camera yaw/pitch does not detach or degenerate the local shadow.
- Sun shadows and menus do not regress.
- With **Local shadow sources = 1**, behavior matches the frozen savepoint.
- With **Local shadow sources = 2**, two NWN-selected local lights cast at the
  same time and the live source readout reaches 2 when two valid entries exist.
- Changing **Light supported** does not change the configured shadow-source
  budget. It may change only the cheap lift/census population.

## Workflow / compaction protocol

- Never dump this source broadly. Use `rg -n` and at most 40–120 lines around a
  known symbol.
- Record every completed patch, compile result, and test request in this file.
- Update `SYMBOL_INDEX.md` whenever relevant symbols move or new ones are added.
- One atomic renderer change per build/test cycle.
- `csm_claude` is not a Git repository. Do not spend turns retrying Git commands.
- After compaction: read `CURRENT_TASK.md`, then only the indexed source windows.

## Last action

Created the immutable savepoint
`../savepoints/2026-08-13-visible-local-casters-confirmed/` from the user-confirmed
single-local-light implementation. Its manifest records source and binary hashes,
the accepted caster policy, and the fact that multi-light support is the only
known missing feature.

Started the conservative refactor in this directory without changing renderer
policy:

- environment/default handling moved to `shadow_config.{h,cpp}`;
- pure matrix/quaternion helpers moved to `shadow_math.{h,cpp}`;
- the OpenGL typedef/constant/entry-point surface moved to `shadow_gl_api.inc`;
- engine symbol bindings and resolvers moved to `shadow_engine_bindings.inc`;
- `nwn_shadowmap.cpp` fell from about 12,843 to 12,152 lines;
- development and shipping builds pass;
- `ldd -r` is clean;
- the exported dynamic-symbol set is byte-for-byte identical to the frozen
  savepoint's ABI surface.

Next test: from this directory launch `./run-dev.sh` and repeat the confirmed
single-light acceptance scene. Verify light selection, visible-geometry caster
policy, alpha-card hair, camera orbit stability, and the existing overlay. Do not
raise the native three-light source ceiling.

Continued the mechanical split after the first checkpoint:

- fixed the nested launcher's game-directory lookup (the reported
  `csm_claude/nwmain-linux: No such file or directory` path was one directory too
  shallow);
- extracted eight renderer subsystems into same-translation-unit `.inc` files;
- reduced `nwn_shadowmap.cpp` from 12,152 to 3,024 lines;
- proved the preprocessor output is byte-identical before and after extraction;
- rebuilt and deployed development/shipping libraries successfully;
- verified both with `ldd -r` and matched the savepoint's exported ABI.

Current test remains `./run-dev.sh` from this directory. No rendering policy or
multi-light behavior was changed by this refactor.

## 2026-08-13 — saved local-light quality startup fix

User report: after saving local-light quality and restarting, the overlay showed
the saved quality (for example High), but the renderer behaved as Low until the
combo was changed manually.

Two independent staged/live values were involved and are now reconciled during
`load_settings()` before the first local-light capture:

- `local_cube_quality` now immediately derives `g_localCubeUpdateSeconds` and
  clears the previous capture deadline. The saved cadence is therefore active
  from capture #1 instead of waiting for a panel edit/frame reconciliation.
- `local_map` is persisted in `g_pendingLocalSize`, while the GL target is
  allocated from `g_localLightCaptureSize`. Startup now sanitizes the saved map
  size to 256/512/1024/2048/4096 and copies it into the live allocation size
  before any target exists. Runtime combo changes still remain staged until the
  Performance page's Apply button is pressed.

`run-dev.sh` does provide a default `NWN_SHADOWMAP_LOCAL_LIGHT_SIZE=256`, but
saved settings are loaded after launcher/environment defaults and now correctly
override it. The launcher itself did not need a behavioral change.

Verification completed:

- development `make -j2`: clean;
- shipping `make -B deploy -j2`: clean;
- rebuilt `libnwn_shadowmap.so` and `libnwn_shadowmap_deploy.so`.

Runtime confirmation still required. Launch `./run-dev.sh` with the saved local
update quality and local map quality left untouched. The startup log must show:

- `[shadowmap][settings] local shadow update High (25 ms) applied from saved settings`
  (or the saved cadence choice), and
- `[shadowmap][settings] local shadow map 256 -> <saved-size> applied from saved settings`
  when the saved map differs from the launcher's 256 default.
