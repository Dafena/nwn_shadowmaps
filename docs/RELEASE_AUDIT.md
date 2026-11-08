# Release integration audit — 2026-11-08

## Approved scope and acceptance

Baseline: `b47f9805bdc45291f3b62b7a153eb83fd614c2f6` (main before local integration).
Review command: `git diff b47f980...HEAD`.

The maintainer approved a whole-plugin audit of pending changes, incorporation
of rain PR #4, cleanup of rejected active experiments while preserving useful
findings, and an implementation of continuous-alpha layer support for Windows.
Preserve accepted Linux behavior, authored geometry/materials, saves and installed
game assets. Keep new rain particles and native-rain hiding optional; existing
weather defaults must not change merely by loading the update.

Publish one integration PR rather than directly updating main. Require builds,
automated regressions, actual Linux/Proton visual and frame-time comparisons for
Type-0 blending/torch lighting, ordinary areas, weather and area transitions.
Native Windows verification remains explicitly pending (maintainer has no
native Windows test access for this run); do not call a DLL build native parity.
Proton exercises the Windows path but is not native Windows driver certification.

## Integration record

- Current local source and untracked code/tests were snapshotted before changes.
- Accepted local changes checkpoint: `0cc705d`.
- Rain PR4 commit `74f706a` merged with conflict resolution: `6fb689b`.
- Personal absolute-path launchers and generated assets are not release sources.

## Results

Audit, Windows implementation, runtime and performance verification in progress.
No release acceptance is implied by this checkpoint. Record failures and limits
before opening the final reviewable PR.
