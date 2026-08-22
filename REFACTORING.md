# Source layout

The source refactor is complete in this repository. The active tree is the
repository root; there is no nested checkout that should be built or deployed.

## Translation units

`nwn_shadowmap.cpp` remains the root translation unit for renderer state and
hooks. The following files are included into it deliberately:

| Module | Responsibility |
| --- | --- |
| `shadow_gl_api.inc` | OpenGL types, constants, function pointers, and binding |
| `shadow_engine_bindings.inc` | NWN engine types, symbols, and resolver table |
| `shadow_targets.inc` | Shadow texture/FBO allocation and target status |
| `shadow_diagnostics_settings.inc` | Readback, dumps, settings load/save, defaults |
| `shadow_replay.inc` | Matrix-stack replay for sun, world, and local targets |
| `shadow_shader_interposition.inc` | Shader injection and draw interception |
| `shadow_fullscreen_receiver.inc` | Receiver shader and scene-depth/color copies |
| `shadow_overlay_runtime.inc` | Overlay lifecycle, input, and frame ordering |
| `shadow_trace_cascade.inc` | Scene tracing, cascade setup, and bucket detours |
| `shadow_local_lights.inc` | Engine-selected local-light state and capture setup |

Private shared state is the reason these remain same-translation-unit modules.
Do not move a module to a separate object merely to make the file tree look
cleaner; that changes visibility and initialization assumptions.

Separate objects are intentionally limited to pure or independently owned
code:

- `shadow_config.{h,cpp}` — memoized environment lookup and shipping defaults;
- `shadow_math.{h,cpp}` — pure vector/matrix/projection helpers;
- `nwn_overlay_imgui.cpp` — the only file allowed to include Dear ImGui;
- `nwn_oit.cpp` — opt-in transparency census, source/program classification,
  and experimental A2C/OIT mechanics; its accepted contract is documented in
  `TRANSPARENCY_MODES.md`;
- `nwn_platform_win.cpp` — Windows proxy and platform mechanics.

## Build dependencies

Both `Makefile` and `win/Makefile` list the included `.inc` files as
dependencies. If a new included module is added, update both dependency lists.
Otherwise an include-only change can leave one target using an old object.

The Linux development build is `libnwn_shadowmap.so`; the Linux shipping build
is `libnwn_shadowmap_deploy.so`; the Windows build is `win/version.dll`.
`make portable` builds the shipping Linux library in Debian 11 to keep the
glibc/glibc++ floor suitable for distribution.

## Refactor invariants

- Preserve the exported Linux symbol surface unless an ABI change is intended.
- Keep ImGui headers inside `nwn_overlay_imgui.cpp`.
- Keep platform mechanics behind `nwn_platform.h`; keep shipping policy behind
  `NWN_SHIP`.
- Keep Windows-only local-light mechanics behind
  `NWN_WIN_LOCAL_FASTPATH`.
- Validate generated receiver shaders with `python3 check_shaders.py`.
- Runtime-test renderer changes on the actual platform before calling them
  complete.

The current implementation and active blocker are documented in
[SHADOWMAP_STATUS.md](SHADOWMAP_STATUS.md) and
[CURRENT_TASK.md](CURRENT_TASK.md). Transparency evidence and mode ownership
are documented in [TRANSPARENCY_MODES.md](TRANSPARENCY_MODES.md).
