# Ordered continuous-alpha layer module

Linux native v89 is the runtime reference. The module is explicit shader opt-in: an active `wmStackPriority` float (integer0–9) and `wmLayerAlphaPass` float publish the contract. The accepted prototype name `wmDiagAlphaPass` is also supported. Unknown shaders, native water, framebuffer-sampling buckets, shadow replays and owned passes remain native.

The exporter preserves original geometry and separates material identities by mesh transparency hint. Matching material/hint pieces may still batch. The shader keeps the priority uniform active using a valid-range guard; it must not use this value as colour/alpha or a geometric bias.

After one discovery frame, the module visits complete priority groups0 through9. Within each hint it draws the original Parts' opaque cores (alpha>=0.95, depth writes enabled), then their fades (0.001<alpha<0.95, depth writes disabled). Equal hints retain the engine's tie order, without an editor-order fallback. Unmarked native items retain their sublist order. Depth testing, blending, culling, vertices, UVs and alpha values remain unchanged. The modified uniform and depth-write mask are restored after every draw, including multi-draw calls.

Depth still matters: a priority is not an instruction to draw through a depth-writing surface. This is not A2C, OIT, a depth-copy workaround, or duplicated export geometry.

`nwn_layer_stack.inc` is part of the main translation unit. `nwn_layer_stack_plan.h` contains the shared scheduling policy. `bash tests/run-layer-stack-regression.sh` verifies complete ascending groups, equal ties and native fallbacks. Linux iterator offsets are bounded and match the accepted measured trial. Windows has a separate first-item adapter and requires safe first/next/Part draw hooks before scheduling. Native Windows runtime validation is pending.

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

## Windows adapter and audit corrections

The v89.8193.37-17 executable exports `?Draw@PartTriMesh@@UEAAXXZ`.
`GetFirstItem` RVA0xd94f0 resets manager+0x300 and cursor+0x308, then consumes
item0; `GetNextItem` RVA0xd9530 uses 16-byte bucket lists and8-byte item slots.
The adapter handles that first consumption explicitly, rather than assuming the
Linux caller starts with GetNextItem. Missing safe hooks leave the policy native.
The exact export mapping is checked against the game executable by win/verify.

Discovery is scene-scoped and ages only on that scene's bucket visits. A bounded
32-scene LRU discards retired scene identities; GUI/portrait renders do not expire
the area's Parts. A shader losing opt-in after the first queued submission does
not receive a second native blend. Regression tests cover both failures.

The rejected per-layer A2C depth-copy experiment was removed. It cost GPU depth
copies without satisfying the maintainer's full-map visual target. Standard Mode2
A2C remains supported separately; continuous-alpha layers reuse original meshes
at runtime without duplicated export geometry or depth copies.

The initial Windows Subhook approach safely refused the exported Draw prologue
(no relocatable trampoline). The Windows adapter instead changes its unique
virtual Draw pointer in the executable's read-only data, restoring page protection
immediately and retaining the original function. It does not rewrite executable
instructions. Ambiguous/missing slots leave layering native. Proton confirmed the
slot at RVA0xd883d0 and actual layer activation; native Windows remains pending.
