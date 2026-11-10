# Local drawing on the dormitory screen

On 4 November 2026 the user prioritized proving that NUI pen strokes appear on
an in-world texture. This is a single-client visual proof, before multiplayer
bootstrap, board identity and persistence. It does not enable network drawing.

## What is installed

- The native `eom_draw_probe` canvas and continuous pen preview.
- `eom_draw_world.tga`: a separate, horizontally corrected 2048-square copy of
  the existing dormitory workspace artwork, with a tiny resource signature on
  the outer atlas edge.
- A development-only override of `wdor9c805300b0ea.mtr` that changes only its
  emission texture (slot 5) to `eom_draw_world`.
- `eom_draw_room.ncs`: jumps the controlled PC to the existing `wm_dor_arr`
  waypoint, then opens the NUI. Its resref is 13 characters.

The original DDS, MTR in the repository HAK sources, terrain mesh and UVs are
preserved. The existing material shader displays the updated emission texture;
this proof does not require a new global shader or a texture-bind hook.

## Native test

Launch the diagnostic renderer with `win/run-drawing-probe.ps1`, with the
matching rebuilt DLL installed while NWN is stopped. The EOM server needs
`EOM_DRAW_PROBE=1` and its existing per-player NWNX Player plugin.

After connecting, run:

```nwscript
dm_runscript eom_draw_room
```

Draw a large zigzag or recognizable symbol. Close the NUI, look at the dormitory
screen, and verify it is present inside the sketch workspace. Reopen the probe
and use Reset to return to the original artwork. Opening a fresh probe session
also starts a fresh local drawing.

The test override applies to uses of this dedicated dormitory material on this
client. This is not per-instance board routing. Other clients retain their normal
assets. Permanent saves, area-wide synchronization and authored board anchors are
separate work.

## How the texture receives ink

At the engine's existing `Material::BindAllStandardTextures` boundary, the
renderer checks slot 5 through the exported material/texture accessors. Only the
exact dedicated resource `eom_draw_world` is admitted. Exported
`AurTextureGetTID` supplies its actual GL name; no object offsets or cache guesses
are used. The Windows export signature and TID's path to imported `glBindTexture`
were checked against NWN 89.8193.37-17.

With the GL context current, a bounded readback verifies the texture's dimensions,
format and the dedicated pixel signature. That signature identifies row order.
The renderer retains an immutable copy of the original uploaded image. Each pen
revision composites transparent ink from the same stroke model used by NUI over
that copy, then updates the dedicated texture and its mip levels at most 30 Hz.
No artwork file is rewritten while drawing.

The writable atlas inset is `(420,640,856,992)` in the canonical 2048-square source. This
fits a roughly 4:3 pen canvas inside the physical panel's sketch workspace, clear
of its header, notes, schedule, frame and toolbar. Export flips the texture's
actual pixels horizontally to correct the observed in-game mirror. New ink uses
the corresponding mirrored atlas coordinates, so the base is not flipped again
while drawing. All pixels outside that stored inset,
and the original image alpha, are preserved. Reset composites an empty ink layer
over the immutable base, removing previous strokes without accumulating ghosts.
Completed strokes remain in-world after closing the NUI; an unfinished stroke
is cancelled. Texture deletion/reallocation invalidates the captured resource.

GPU readback restores active texture unit, unit-zero binding, pack PBO, alignment,
row length and skips. Upload restores corresponding unpack state. There is no
framebuffer, shader, blend, depth, viewport or scissor change. Missing bindings,
mismatching resources or unsupported formats produce no texture write.

## Reproduce the test assets

Use Python with Pillow and the original EOM workspace PNG and MTR:

```sh
python tests/make_drawing_world.py --art /path/to/wt9a294ff7f9394c.png --material /path/to/wdor9c805300b0ea.mtr --output /new/test-output
```

The script refuses to overwrite existing output files. Install both output files
in the test client's active development directory. Compile the helper with
`nwnsc`; no module repack or HAK rebuild is needed for this reversible test.

## Build and deployment evidence

Linux development/shipping and Windows builds pass. World-composite tests verify
exact base restoration, unchanged pixels outside the inset, original alpha,
row-order handling and signature mismatch rejection. Preview/input and GL state
tests pass. Full EOM rebuild passes with seven existing warnings and zero errors.
Windows verification passes 17/17 proxy exports and 58/58 mapped symbols.
Native Linux execution remains unverified.

Installed Windows renderer SHA256:
`d0da6ef2d578858fbf30095bf670aa8128a258e42955df92a81153d42a84a2d4`.
The prior DLL and original client trace are backed up under
`D:/Main/Neverwinter Nights/asset_backups/drawing-world-proof-20261104-065821`.
That directory's deployment manifest records every target, hash and whether a
file previously existed. Installation occurred after the user closed NWN; the
test server stayed running. The connected room test confirmed actual NUI pen
strokes appearing on the in-world screen. The first image was horizontally
mirrored, including the lettering. The redacted trace is
`docs/evidence/drawing-windows-world-mirror-20261104.json`.

The user requested flipping the texture itself and reading the texture
documentation. The exporter now writes an exact horizontal pixel flip, as a
2048×2048 uncompressed 32-bit RGBA TGA with left-origin encoding. A byte-level
comparison verifies that all non-signature rows equal the horizontally flipped
source. Stroke mapping follows that export orientation, and tests verify that
Reset never double-flips the already-corrected artwork.

References read: [Textures](https://nwn.wiki/spaces/NWN1/pages/38174958/Textures),
[TGA](https://nwn.wiki/spaces/NWN1/pages/53672779/TGA),
[DDS](https://nwn.wiki/spaces/NWN1/pages/3473496/DDS), and
[Content Load Order](https://nwn.wiki/spaces/NWN1/pages/38174823/Content+Load+Order).
TGA supports the chosen RGBA/power-of-two format. The DDS documentation's Y-axis
conventions are distinct from the horizontal mirror observed on this screen.
The dedicated resref avoids DDS/TGA shadowing; the original HAK DDS is preserved.

The corrected renderer SHA256 is
`db389b39027fe043d74ae975a701f76c865c8ad50beef6991f05120d10e23dcb`.
Its Linux development/shipping and Windows builds, world-composite tests and
17/17 plus 58/58 export checks pass. After the user closed NWN, the flipped asset
and matching renderer were installed with verified hashes. The previous files
and client trace are backed up under
`D:/Main/Neverwinter Nights/asset_backups/drawing-world-flipped-20261104-072559`.
The installed TGA SHA256 is
`eca85c999a7374b7a3aee9ae445e6b14e09d8959318a31b9bb7734d4299082d8`.
The diagnostic client was relaunched; the server and existing material/helper
script stayed unchanged. The connected room test confirmed five strokes uploaded
to the world texture, followed by native close event sequence 14. With the NUI
closed, the screen visibly retained a readable handwritten "hello". The authored
Balamb Garden title, sketch heading, notes and schedule also read correctly.
The redacted trace and explicit visual-test scope are recorded in
`docs/evidence/drawing-windows-world-corrected-20261104.json`. This verifies the
requested single-client drawing-to-world-texture path. Multiplayer delivery,
per-board/area-instance routing, permanent storage and native Linux runtime
validation remain outside this proof.
