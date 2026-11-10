# Drawing capability probe

This is an opt-in diagnostic, **not** a working collaborative drawing screen.
It does not consume input. The current local-preview build updates only its
dedicated diagnostic `eom_draw_pad` texture. Production work stays behind the
two confirmed capability gates; no authored world texture is replaced.

## Enable for an isolated native test

- Server: deploy the rebuilt EOM plugin with `EOM_DRAW_PROBE=1`, and ensure the
  NWNX Player plugin is installed (the per-player token route depends on it).
- Renderer: install the matching built artifact and launch the client with
  `NWN_DRAW_PROBE=1`. For Windows also use `NWN_SHADOWMAP_LOG=1` so diagnostics
  reach `nwn_shadowmap_win.log`. Linux uses the normal injector launch log.
  Windows can use the guarded launcher below; it rejects a second running client
  and an installed DLL that differs from the build in this checkout:

  ```powershell
  ./win/run-drawing-probe.ps1 -GameExe 'E:/SteamLibrary/steamapps/common/Neverwinter Nights/bin/win32/nwmain.exe' -UserDirectory 'D:/Main/Neverwinter Nights'
  ```

  These paths are examples; both are explicit parameters. The launcher does not
  install files, stop the client/server or provide credentials. Launching normally
  from Steam does not retain these diagnostic flags; use the launcher again after
  closing the game. Native server events alone cannot replace a missing client trace.
- The reserved diagnostic custom-token number is **910001**. Check for module
  conflicts before enabling the probe. Neither side changes this token normally.
- Generate the dedicated client resource once with
  `python3 tests/make_drawing_pad.py /chosen/output/eom_draw_pad.tga`, then copy
  it to the test client's active development directory. The generator refuses
  to overwrite an existing file. Archive/remove this diagnostic resource when
  disabling the probe. It is an uncompressed 1024×1024 RGBA TGA with tiny
  diagnostic signatures in opposite rows; ordinary images are never admitted.
- While logged in, as DM run `dm_runscript eom_draw_probe` on the controlled PC,
  or use a test placeable whose OnUsed is `eom_draw_probe` (14 characters).
  It opens a native NUI canvas containing a visible cross. Click once without
  moving to establish alignment, then drag slowly. Reset clears local ink and
  starts a fresh recording. Re-align after moving/resizing, changing GUI scale
  or losing focus. Cover Test opens a second native NUI window over the canvas.
- Move/resize the window, change UI scale, click/drag across the canvas, release
  outside it, cover it with another NUI window, change area, then disconnect.
  Open it independently on a second client to check player-only delivery.

Expected server evidence: `Drawing probe event` entries with the actual native
event type and element. Unknown event strings are read with `NuiGetEventType`,
not silently classified as movement. Geometry watches publish separately.

Expected client evidence: `[drawing-probe] pointer` raw OS coordinates and
`[drawing-probe] native` native canvas/geometry events. The observer must report
successful token-hook installation. If subhook cannot produce a safe trampoline,
it reports unavailable and installs no hook. No temporary unhook/re-hook is used.

Native event coordinates and raw mouse samples are recorded separately. The local
preview requires a native canvas down uniquely correlated with a recent raw down;
unowned mouse-ups never create ink. A stationary click calibrates alignment from
native coordinates and NWN's GUI scale. This still uses the server round trip,
so it does not prove immediate local native input ownership. The preview's clipping,
orientation and alignment must pass native tests. Diagnostic native sizes 400×300
describe the nominal canvas, not its final pixel bounds after layout/UI scaling.

The 256-bit random per-window nonce is transported only to the owning player and
redacted from client logs. It is a correlation/protocol test value; receiving it
does not establish a verified network authentication bootstrap. Never use the
diagnostic observer as a production authorization implementation.

## Evidence required before continuing

Record platform, exact game build, artifact hash, GPU/driver, UI scale and native
window geometry, plus matching client/server traces. The user explicitly changed
validation order to Windows first on 3 November 2026; Linux runtime validation
remains pending. Confirm no pointer events are lost and that covering the canvas
prevents native canvas events. Verify both clients receive only their own nonce
stream. Credentials/tokens must not be pasted into reports.

Once canvas mapping and session transport are proven, implement the authenticated
endpoint, authoritative stroke model and opt-in shader. Keep production writes
disabled until those gates pass.

## Build-only verification already performed

```sh
g++ -std=c++17 -Wall -Wextra tests/drawing_probe_test.cpp -o /tmp/drawing_probe_test
/tmp/drawing_probe_test
g++ -std=c++17 -Wall -Wextra tests/drawing_preview_test.cpp -o /tmp/drawing_preview_test
/tmp/drawing_preview_test
make -j2
make deploy -j2
make -C win -j2
python3 check_shaders.py
```

EOM: `dotnet build eom.sln --no-restore -t:Rebuild -c Debug -p:DeployAfterBuild=false`.
These checks do not certify native hook behavior or multiplayer synchronization.

The separate EOM `tools/DrawingGate.Tests` console harness tests the planned
one-use bootstrap-ticket registry against cross-character redemption, replay,
expiry, disconnect revocation, supersession, concurrent redemption, malformed
input and capacity exhaustion. It passes, but the registry is not yet wired to
the diagnostic nonce or a public network endpoint. Its trusted binding callback
must run on EOM's game thread and revalidate the actual game session/area/board.

Windows artifact checks against installed NWN 89.8193.37-17 passed: 17/17 proxy
exports and 56/56 mapped engine symbols. Presence of those symbols still does
not establish safe native hook execution. Initial artifacts were staged only;
the installation and native results are recorded below.

## First native Windows results and updated pass

The Windows token observer installed and delivered the native open, mouse-down,
mouse-up and geometry messages to the owning client. One observed pass contained
56 native events, including 22 downs, 21 ups and 12 geometry messages. No pointer
samples were present because the existing Windows WndProc installation depended
on the ImGui settings panel being initialized. This is not a completed input gate.
The redacted evidence is in `docs/evidence/drawing-windows-20261103.json`.

The updated build installs its diagnostic WndProc independently of the settings
panel; Linux diagnostics poll the existing SDL input API independently as well.
Sampling arms only when a probe window opens, is bounded to 4,096 records, and
includes monotonic timestamps. Windows also reports focus loss/cancel events.
No diagnostic input is swallowed. The updated native button explicitly measures
400×300 logical units, matching the cross rather than the button's intrinsic
150-unit width, and unrelated clicks no longer masquerade as geometry messages.

For the verified Windows token function prologue only, a guarded Windows-specific
trampoline expands its initial conditional branch into an absolute jump. A native
Windows executable test validated both branch outcomes and rejection of a mismatched
prologue before installing it. Linux hooking behavior is unchanged.

The updated renderer and EOM plugin were installed with hash checks after client
and test server shutdown. Their prior versions are recoverable under
`D:/Main/Neverwinter Nights/asset_backups/drawing-pointer-probe-20261103-143052`.
The task's server runs with the temporary `tmp/drawing-probe-compose.yml` override;
the normal server env file is unchanged. Native coordinate mapping, overlapping
window ownership, cross-player isolation and authenticated network bootstrap still
need verification; no production board writes are enabled.

## Continuous pointer pass — 3 November 2026

The updated installed Windows renderer (`93aaa1f7698983e8ff4d058c3006d5e8599d870e705b747a6b54f3523ed5ff6f`)
recorded 3,435 raw samples in one window session: 25 downs, 25 ups and two
focus/cancel notifications. Native messages included 17 canvas downs, eight
canvas ups and seven geometry changes. The different native/raw counts are
expected when releases occur outside the canvas; raw input alone does not prove
canvas ownership. Counts do not establish that every OS movement was retained.

Sixteen stable-geometry downs could be correlated without reusing a raw down.
The measured common scale was 1.00378, with 1.11-pixel RMS and 2.77-pixel maximum
residual. This is a correlation fit affected by mouse movement during event
delivery, **not an authoritative canvas rectangle or a scale calibration**.
The trace did not record the engine GUI scale, and occlusion, clipping and
multiple-client isolation remain unproven. The gate remains open.

The redacted report is `docs/evidence/drawing-windows-pointer-20261103.json`.
It can be regenerated without third-party Python packages:

```sh
python tests/analyze_drawing_probe.py /path/to/nwn_shadowmap_win.log --output report.json
python -m unittest discover -s tests -p test_analyze_drawing_probe.py
```

The analyzer separates open/close sessions, rejects ambiguous/reused/stale down
matches and ignores unrelated log text. Never publish the original client log
as part of a diagnostic report.

The updated renderer records NWN's own optional `CAurora::GetGUIScale` result
when it changes. Its Windows export signature and static float getter were
checked against the installed executable. Native runtime verification of that
new measurement is pending a connected probe session. The build also stops diagnostic recording
after ten minutes and rejects out-of-range protocol sequence numbers. No live
renderer DLL was replaced while a client was running.

The user authorized installation after closing the client. The updated DLL
(`c7698744ae5de7891af7980d14ffd67d65652189f8320eedae86855b6c53aae9`)
was installed with a matching hash on 3 November at 15:00 America/Chicago. The
prior DLL and original client trace are recoverable under
`D:/Main/Neverwinter Nights/asset_backups/drawing-scale-probe-20261103-145956`.
The guarded launcher started a new diagnostic client, and the new client log
confirmed that the token observer installed. The server remained running with
`EOM_DRAW_PROBE=1`. The preceding UI-scale/occlusion retest reached the server,
but its manually restarted client had produced no corresponding diagnostic
trace, so that retest cannot validate the input gate.

For this updated build, parser tests, trace-analyzer tests and Linux development,
Linux shipping and Windows builds pass. The Windows artifact verifier confirms
17/17 proxy exports and 57/57 mapped engine symbols for NWN 89.8193.37-17. The
full EOM rebuild passes with seven existing warnings and zero errors.

## Controlled GUI-scale pass — 3 November 2026

The engine getter ran safely and recorded UI scale changes from 1.0 to 1.5.
The initial user test exhausted the old 4,096-record allowance on hover/motion
before later button events; its missing raw samples cannot validate those later
gestures. Its buffered log also arrived after the first inspection. Both are
recorder limitations, not evidence that NWN dropped the user's input.

After reopening the probe, six controlled stationary clicks were correlated:
three at scale 1.5 and three at scale 1.0, each group with zero measured residual.
Raw client pixels equal native window origin plus native mouse position plus a
canvas inset for that layout. Observed insets were (8, 89.5) at scale 1.5 and
(8, 81) after changing to scale 1.0 with a clipped/resized layout. Do not hard-code
these insets: layout and clipping affect them. At scale 1.5, a valid native canvas
X was 510 even though the nominal logical width is 400; mouse payload coordinates
are scaled pixels and must be converted appropriately for logical ink coordinates.

Opening native game options hid the canvas; a click where it had been visible
did not produce a canvas down. This checks modal/hidden input, not arbitrary
overlapping NUI ownership. The automated drag lasted about 27 ms and produced
no native down at its start, so it does not validate a normal continuous drag.
The user's earlier physical drag/release evidence remains separate. The original
UI scale of 1.5 was restored and the game returned to its normal view.

The redacted report is `docs/evidence/drawing-windows-scale-20261103.json`.
The analyzer now fits each recorded GUI scale separately rather than pooling
incompatible layouts. Exact clipping, arbitrary NUI overlap, cross-client
isolation and a verified live ink preview remain open; gate 1 is not complete.

The next build samples held movement at at most 120 Hz, ignores hover movement,
and reserves a separate allowance for down/up/focus events. Reaching the movement
limit no longer suppresses releases. Logs flush on transitions and scale changes.
Parser/input-journal tests include hover flooding, motion exhaustion, release
retention, duplicate transitions and focus cancellation. The trace-analyzer tests
also cover mixed GUI scales.

The next EOM probe adds **Reset recording** and **Cover Test** buttons below its
canvas. Reset rotates the diagnostic nonce and restarts recording even after
expiry. Cover Test opens a second clickable NUI window over the canvas. A down
on its button should produce a diagnostic `B` event and no `D` event for the
canvas underneath. Close the covering window and compare an exposed canvas click.
These controls are capability diagnostics, not multiplayer drawing tools.

The updated installed Windows renderer has SHA256
`a11100a7a10bb141463e134f02ba5eae9b40f3a502eacccfd148196719d1f216`.
Linux development/shipping and Windows builds, input/parser tests, four analyzer
tests, and the full EOM rebuild pass (seven existing warnings, zero errors).
The Windows verifier passes 17/17 proxy exports and 57/57 mapped engine symbols.
On 3 November at 15:30 America/Chicago, after the client closed and the test server
stopped, all three deployed files (renderer DLL and EOM DLL/PDB) were copied and
their hashes checked. The previous files and original trace are recoverable under
`D:/Main/Neverwinter Nights/asset_backups/drawing-controls-probe-20261103-153017`.
The EOM DLL SHA256 is
`d866ef73c54de50d58ea2419b8e8abdc7f39a38d23ef9e5331162a624f38f7fb`.
The restarted server confirmed module load, service registration and the
`eom_draw_probe` handler; the diagnostic client confirmed token-hook installation.
Native runtime validation of the new sampling policy and Reset/Cover controls
remains pending a connected player session. No live ink preview or board writes
are enabled.

## Probe layout regression and server repair — 3 November 2026

The first Reset/Cover build produced only window controls instead of the probe
body. The installed DLL hash matched and saved geometry was valid. Native inline
reference windows reproduced `Error constructing window from json: The constraint
can not be satisfied.` This is a layout failure, not missing draw-list support.
The new button row imposed height 28 while its buttons required height 26 plus
native spacing. The fixed row now lets NUI determine its height. Geometry watches
also enlarge obsolete saved sizes to at least 440×450, with an idempotent correction,
so the extra controls have space. Geometry is serialized from a typed NuiRect.

The full EOM rebuild passed with seven existing warnings and no errors. Actual
Anvil serialization confirmed the row no longer imposes a fixed height. The
server-only repair was installed with hash checks and a test-server restart;
the client renderer did not require replacement. The new server DLL SHA256 is
`54d5725ba15a9ee0357b7efbd61ee82a90d376f7bb23400dfa75aac7ef1657a7`.
Previous files and the temporary native reference script are backed up under
`D:/Main/Neverwinter Nights/asset_backups/drawing-layout-fix-20261103-154651`.
The temporary `eom_dref.ncs` was moved out of the active development directory.
The repaired body and both controls were then confirmed in the running client.
Reset produced a fresh nonce/sequence. The same screen position yielded a canvas
down while exposed, only a cover-button `B` down while covered, and a canvas down
again after closing the cover. Closing the covering window also produced an
unowned canvas mouse-up. Therefore only an owned down may start or commit ink;
an up by itself is insufficient. The last session in
`docs/evidence/drawing-windows-cover-20261103.json` contains two matched exposed
downs, one cover down, no recorder exhaustion and no sequence errors. A controlled
slow freehand preview and cross-player isolation are still required.

## Local NUI texture preview — 4 November 2026

The diagnostic canvas now draws `eom_draw_pad` through `NuiDrawListImage` with
Stretch aspect, beneath the existing native cross. CPU ink uses logical canvas
coordinates. NWN retains control over the image quad,
window placement, scale, clipping and window layering.

The preview accepts only a matching nonce and increasing sequence after open.
Raw downs alone cannot own a stroke. Native acknowledgement must match a unique
raw down within 250 ms; ambiguous rapid downs are refused. A stationary initial
click establishes alignment without hard-coded DPI, margins or title-bar sizes.
Movement is buffered until the native down arrives. Raw release ends an owned
gesture even outside the canvas; cover downs, focus loss, close and geometry/scale
changes cancel unfinished ink. Segment clipping rejects outside parallel lines
instead of clamping them onto the canvas border.

These are deliberately smaller diagnostic limits: 128 strokes, 8,192 points,
1,024 points per gesture, and 8,192 raster sampling steps over the whole image.
Exceeding a limit refuses the unfinished gesture and preserves committed ink;
Reset clears the local board. No stroke is transmitted to another client, saved
or sent to a world texture. No production stroke ownership is granted.

The first implementation tried exported `BinaryFindTexture` and
`AurTextureGetTID`. Disassembly validated the TID's GL meaning, but the running
probe displayed the static image without ever finding it in that named cache.
The executable contains a separate Nuklear/stbi image loader. The redacted native
trace is `docs/evidence/drawing-windows-preview-cache-20261104.json`; this pass
does **not** validate live ink. The unused engine bindings were removed.

The revised build observes native `glTexImage2D` uploads without changing them.
It admits only the dedicated asset's exact signatures in opposite rows of a
contiguous 1024×1024 RGBA/BGRA byte upload, with no bound unpack PBO or row skips.
The signature also identifies whether the incoming image rows are top-down or
bottom-up, which the ink raster follows. Captured names are scoped to the actual
GL context and invalidated on deletion or reallocation. Windows capture stays
disabled unless both upload and deletion adapters install. Missing data is a
no-op. No generic texture-bind hook or engine object offsets are used.

Before writing, the uploader additionally requires a live GL texture, 1024×1024
uncompressed RGBA format, and all needed driver functions. It never creates,
releases or deletes the engine's texture. The marker is a resource identity
check, not an authentication token; network authorization remains a separate gate.

Uploads occur at most 30 Hz, only on revision/resource changes, with the render
context current. They restore active texture unit, unit-zero texture binding,
pixel-unpack buffer binding, alignment, row length and row/pixel skips. Existing
mip filters receive regenerated mip levels. No framebuffer, shader, blend, depth,
scissor or viewport state is changed. Mock-driver tests verify restoration with
non-default unpack state and a bound PBO, including validation-failure paths.
Preview tests cover scaled coordinates, continuous samples around acknowledgement,
unowned ups, cover/focus cancellation, replay, ambiguous downs, geometry changes,
line clipping and capacity refusal without old-stroke eviction.

Full EOM rebuild: seven existing warnings, zero errors. Linux development/shipping
and Windows renderer builds and the preview/parser/journal/analyzer tests pass.
The first artifact verification passed 17/17 proxy exports and 59/59 mapped
symbols; after removing the unused texture getters it passes 17/17 and 57/57.
Native Linux runtime remains unverified. Marker tests cover channel/row ordering,
signature mutation and mismatched data; driver tests cover top-down raster output.

The preview was installed with verified hashes while the client was stopped.
Previous DLL/PDB files and the previous client trace are recoverable under
`D:/Main/Neverwinter Nights/asset_backups/drawing-preview-20261104-060528`.
The server was restarted with `EOM_DRAW_PROBE=1`; the diagnostic client was launched
with the guarded launcher. The first renderer SHA256 was:
`cdfcbf553b49402d113c929cb98cfabbeced2d023d50e7a6b96aa5a74f757d54`;
EOM DLL SHA256:
`83fc6ef6ce7c1e2a28d4080099bfc02dd661b4c73144bedd1a18435542bc1149`.
The subsequent signature-observer build has SHA256
`1134e0dd46d5eddbbe654cd257c66c438d23c007decca7141f7cccdb333f2eaa`.
It and the marked TGA were installed after the user closed NWN, with backup and
verified hashes under
`D:/Main/Neverwinter Nights/asset_backups/drawing-nui-image-20261104-061421`.
The existing server build stayed running; the diagnostic client was relaunched.
Its startup confirms both image and token observers installed. Native preview
validation is pending reconnection. Neither capability gate is complete; do not
treat successful compilation as a rendered-ink result.
