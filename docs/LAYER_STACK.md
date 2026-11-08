# Ordered continuous-alpha layer module

Linux native v89 is the runtime reference. The module is explicit shader opt-in: an active `wmStackPriority` float (integer0–9) and `wmLayerAlphaPass` float publish the contract. The accepted prototype name `wmDiagAlphaPass` is also supported. Unknown shaders, native water, framebuffer-sampling buckets, shadow replays, owned passes and Windows remain native.

The exporter preserves original geometry and separates material identities by mesh transparency hint. Matching material/hint pieces may still batch. The shader keeps the priority uniform active using a valid-range guard; it must not use this value as colour/alpha or a geometric bias.

After one discovery frame, the module visits complete priority groups0 through9. Within each hint it draws the original Parts' opaque cores (alpha>=0.95, depth writes enabled), then their fades (0.001<alpha<0.95, depth writes disabled). Equal hints retain the engine's tie order, without an editor-order fallback. Unmarked native items retain their sublist order. Depth testing, blending, culling, vertices, UVs and alpha values remain unchanged. The modified uniform and depth-write mask are restored after every draw, including multi-draw calls.

Depth still matters: a priority is not an instruction to draw through a depth-writing surface. This is not A2C, OIT, a depth-copy workaround, or duplicated export geometry.

`nwn_layer_stack.inc` is part of the main translation unit. `nwn_layer_stack_plan.h` contains the shared scheduling policy. `bash tests/run-layer-stack-regression.sh` verifies complete ascending groups, equal ties and native fallbacks. Linux iterator offsets are bounded and match the accepted measured trial. Windows intentionally has no new iterator behaviour until separately validated.

Development-only `NWN_LAYER_STACK_TRACE=/absolute/directory` records one frame when `request.txt` receives a new alphanumeric label. Each label's JSONL includes actual priority/pass/depth state and native light inputs. No framebuffer readback or render policy depends on these records. Shipping builds exclude this diagnostic.

## Verification and lighting

The maintainer accepted the tuned whole-area hints before module integration.
The integrated Linux development library matches the separately native-tested
artifact byte-for-byte. Both normal development and shipping builds pass.
A full-Shadowmap native test with a held inventory torch confirms floor lighting
response with identical camera/projection matrices, 56 ordered layer draws and
correct depth masks. WorldMaker v6 shaders request per-fragment lighting so a
small light is not missed between distant vertices. Shipping runtime and Windows
parity still need their own validation; build success is not that validation.

## Approved integration — 2026-11-07

The maintainer approved the native lighting and tuned blending test. This module
is included in the normal Linux development and shipping artifacts; no legacy
trial environment switches are required. `launch-linux.sh` -> `run-dev.sh` ->
`run-shadowmap-trace.sh` -> `run-nwn.sh` preloads the repository development
artifact directly. `run-shadowmaps-clean.sh` selects the shipping artifact.
No game-directory plugin copy is needed for these launchers. Restart a running
game once to load a newly built plugin. Compatible shader/material resources
must accompany the area; upgrading the plugin alone does not rewrite older HAKs.
