# Collaborative drawing — design in progress

This records the GrillMe interview and implementation. The user confirmed the
complete design on 3 November 2026 and authorized implementation.

## Confirmed by the user — 3 November 2026

- Each physical screen in each area instance has an independent canvas.
- Permanent-area boards persist across departures and server restarts.
- Dungeon-instance boards reset when their owning instance is destroyed.
- Multiple players can draw concurrently; all players in the area should see updates.
- Players draw, undo and erase their own strokes; DMs may erase anything or clear.
- First-version tools: freehand pen, small color palette, adjustable thickness,
  own-stroke eraser and undo. Typed text and shapes are deferred.
- Live world-screen presentation requires the renderer plugin. Other clients
  retain the authored static screen.
- Clicking a board opens its panel. Editing requires staying within four metres;
  leaving the area closes the panel. Area-wide viewing remains available.
- The eraser removes whole strokes, not portions of strokes.
- Persistent character identity owns a stroke; another character on the same
  account cannot undo/erase it as an owner.
- The dormitory board draws only inside its large sketch workspace. Header,
  frame, notes, schedule and toolbar artwork are retained. Other screens should
  be supported through the same reusable system.
- DMs can restore the last clear. Player undo cannot restore DM-removed work.
- Hybrid input is accepted: native NUI window/controls with plugin-captured
  continuous drawing inside the canvas. Moving, resizing and UI scaling must align.
- An additional authenticated drawing endpoint alongside EOM is accepted;
  deployment may require a port or reverse-proxy route.
- A full board preserves existing work and stops new drawing with a message;
  erasing or DM clearing frees capacity. Old content is never silently evicted.
- Immediate local previews, live shared updates and automatic latest-state loading
  on join/reconnect are accepted. Disconnected clients cannot submit edits until
  synchronized again.

## Findings so far

- The renderer has shader/material interception and injector-owned GL texture
  lifecycles. These do not provide a multiplayer drawing protocol.
- EOM uses NUI draw lists and per-player texture overrides for existing assets.
  Neither currently provides a synchronized dynamic drawing texture.
- Checked-in Anvil exposes mouse-down and mouse-up events but no continuous
  mouse-motion event. The shipped native `nw_inc_nui` and `nwscript` resources
  provide event-scoped payloads and conditional draw-list predicates, but no
  documented continuous pointer stream was found. Plugin input capture is the
  likely freehand path; a raw native-event probe remains an early validation step.
- No existing gameplay transport between EOM and this renderer was found.
- OS mouse-input plumbing exists on Linux and Windows, currently tied to the
  settings overlay. It is not yet a native NUI canvas-capture implementation.
- Material binding and texture-name hooks exist, but there is no board-specific
  GPU texture substitution. The excluded `BindInUnit` hook must not be revived.
- Native UI-scale and NUI symbols were found in Windows exports, but they are
  unvalidated candidates, not proven client bindings or safe structure layouts.
- Shared authoritative drawing state, identity/session association, ordering,
  persistence and late-join recovery will need an explicit design.

## Confirmed implementation design

### Authority and drawing data

EOM owns board identities, character authorship, DM authority, presence/range
checks, quotas and monotonically ordered revisions. Client requests never decide
their author, DM status or area membership. A canvas is an ordered collection of
strokes, not a client-uploaded authoritative image. Clients rasterize the same
accepted strokes into private GPU textures. Local previews are provisional until
accepted; rejected strokes disappear without altering the shared canvas.

Each operation has a unique identity so retries do not duplicate strokes. Server
ordering resolves overlap; later accepted marks appear over earlier ones. Erasing
targets whole eligible strokes. Player undo cannot override a DM removal or clear.
Restore-last-clear merges recovered marks beneath newer marks, preserving drawing
created after the clear. Recovery is restricted to the most recent clear on that board.

The initial engineering budgets proposed for validation are a 1024-square canvas,
4,096 live strokes and 100,000 stored points per board, with bounded stroke chunks,
payload sizes, coordinate validation and per-session submission rates. Adjust these
from measured performance; do not silently change the agreed full-board policy.

### World screen and NUI integration

Explicit board configuration links an activation/range anchor, area instance,
screen surface and writable UV rectangle. A generic texture resref or camera
distance must never establish ownership. The existing dormitory display needs an
activation anchor because its artwork is part of terrain; only its sketch rectangle
receives ink. Other screens can reuse the configuration contract.

NUI supplies controls and window lifecycle. The plugin displays the live canvas
and captures only a verified canvas-owned drawing gesture. Clipping, UI scale,
overlapping windows, focus loss and mouse release outside the panel must be handled.
The drawing tool must not steal unrelated gameplay or UI input.

The board shader samples a private ink texture over the authored display, preserving
the base artwork outside the writable rectangle. Rendering is opt-in for configured
boards only. Unmarked surfaces and clients without the plugin keep their normal
rendering. Shared texture assets must not cause different boards to mirror each
other. All touched GL state is restored and resources follow GL-context lifecycle.

### Network, saves and lifecycle

Host the drawing endpoint alongside EOM, with configurable deployment and WSS/TLS
for public network use. Session binding must be proven through an authenticated
game-to-client bootstrap; character names or caller-supplied IDs are insufficient.
No reusable administrative credential is shipped in the client or HAK.

EOM serializes concurrent edits, persists permanent boards in the existing server
storage infrastructure, and sends a snapshot plus ordered revisions to watchers.
Late joiners and reconnecting clients must catch up without losing updates between
snapshot capture and subscription. A revision gap triggers resynchronization.
Client GL work stays on the render thread; server game-object checks stay on the
appropriate game thread. Network/storage work must not block either loop.

Leaving an area invalidates that board session; walking beyond four metres disables
editing. Re-entering reloads authoritative state. An instance's destruction removes
its boards, sessions and retained snapshots, with no leakage into a new instance.
Permanent board keys must survive object-ID reuse and server restarts. Failed saves
must be reported, never presented as durable success.

### Implementation order and acceptance gates

1. Prove tagged NUI canvas geometry, continuous input ownership and pixel alignment.
   Verify native Linux first, then Windows through the existing platform adapters.
2. Prove authenticated EOM-to-plugin bootstrap and bounded round-trip transport.
   Do not accept production drawing writes without verified game-session identity.
3. Implement authoritative strokes, ownership-aware erase/undo, DM clear/recovery,
   persistence, quotas and ordered snapshot/reconnect handling.
4. Add isolated GPU ink targets, the board shader and the dormitory UV/anchor setup.
5. Test at least two simultaneous drawing clients plus a late joiner, two boards
   sharing base artwork, two area instances, reconnects, server restarts, full boards,
   DM recovery, and a client without the renderer plugin.
6. Run full EOM builds and renderer builds/shader checks appropriate to both platforms.
   Record native runtime evidence; do not equate compile success with visual parity.
   Deploy verified artifacts only while affected game processes are stopped.

If either capability gate fails, report the concrete evidence and revisit that
part of the design rather than silently substituting click-only drawing, a separate
ImGui-only UI or an unauthenticated connection.

## Implementation status — capability instrumentation

An opt-in renderer token observer and raw mouse-event journal are implemented,
alongside EOM's `eom_draw_probe` native NUI window. The diagnostic token uses
NWNX's player-only custom-token API, never the global script token table.
Payload parsing is bounded; probe values are never logged in full. These are
diagnostics, not drawing credentials accepted by a network endpoint.

The independent EOM bootstrap-ticket registry implements bounded, short-lived,
hashed-at-rest, one-use grants tied to a server-issued game-session/character/
area/board binding. Its identity/replay/expiry/revocation/concurrency/input-limit
tests pass. It remains disconnected from the diagnostic nonce and networking
until the native channel has been validated.

The protocol parser tests pass. Native Linux development/shipping renderer builds,
the Windows proxy build, and existing shader validation pass. The full EOM rebuild
passes with the seven existing warnings. The Windows token hook and continuous
pointer recording now have native runtime evidence: 25 downs and 25 ups, including
releases outside the canvas, plus native canvas events and geometry changes.
Coordinate correlation across window moves/resizes is within three pixels in the
recorded pass. Exact clipping, UI scale, overlapping-window ownership and
cross-player delivery still need verification. No production authentication
endpoint, shared drawing, persistence, board shader or world texture replacement
is enabled. See `docs/drawing-capability-probe.md` and its redacted evidence.

Controlled Windows stationary clicks now have zero measured residual at both
GUI scale 1.0 and 1.5. Native mouse payloads use scaled pixels, and the canvas
inset changes with layout; nominal logical dimensions are insufficient to locate
ink. Native game options prevent hidden-canvas downs. The repaired Reset/Cover
controls are verified: a covering native NUI window prevents canvas downs at the
same screen position. Closing it can leak an unowned canvas up, which cannot
start a stroke. The recorder now reserves controls separately from held movement.

A local ink preview through a dedicated `NuiDrawListImage` texture is implemented
and installed for Windows native validation. It calibrates from a stationary
native click, clips line segments, cancels unfinished work on focus/layout changes,
and preserves old strokes at its diagnostic limits. It still waits for server
acknowledgement of canvas downs; immediate local native ownership is unproven.
The current preview is neither synchronized nor persistent and does not modify
the dormitory screen. Runtime ink validation and cross-player isolation remain
open. See the capability document for artifact hashes and test scope.

The build host has Docker's Linux environment but no native Linux game session;
compilation is not the required native Linux runtime evidence. Continue with
`docs/drawing-capability-probe.md` before declaring either gate passed.

## Local texture proof requested — 4 November 2026

The user confirmed that the native cyan freehand preview follows the cursor.
Live geometry now comes from the tagged NUI image's actual GPU quad, queried
through the verified Nuklear shader interface and current vertex layout. A cover
window blocks new canvas downs while preserving existing ink. The redacted native
report is `docs/evidence/drawing-windows-ink-20261104.json`.

The user then requested prioritizing a local NUI-to-world texture proof. A
dedicated copy of the dormitory emission texture and development-only material
override are prepared and installed for that test. The original HAK resources
are preserved. This local proof shares one diagnostic canvas among uses of the
test material; it does not establish production board/instance identity or
multiplayer authorization. See `docs/drawing-world-proof.md` for the exact scope,
test command, resource validation, build hashes and pending native result.

The local room test subsequently passed: drawing from NUI appeared on the actual
screen texture, and remained after the NUI closed. A horizontal export correction
made both authored lettering and the handwritten "hello" readable. The installed
renderer hash is `db389b39027fe043d74ae975a701f76c865c8ad50beef6991f05120d10e23dcb`.
See `docs/evidence/drawing-windows-world-corrected-20261104.json` for the redacted
trace and visual-confirmation scope. This is a local texture proof; production
multiplayer authorization and board persistence are not enabled.
