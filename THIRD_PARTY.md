# Third-party code

Both are vendored in-tree so the project builds without fetching anything.

## Dear ImGui — `imgui/`

<https://github.com/ocornut/imgui>, MIT licence. Used only for the in-game
settings panel. `nwn_overlay_imgui.cpp` is the ONLY file permitted to include an
ImGui header; everything else talks to it through `nwn_overlay.h`.

## subhook — `subhook/`

<https://github.com/Zeex/subhook>, BSD 2-clause. Copyright (c) 2012-2018 Zeex.
Used to install the engine function detours.

Both licence files are bundled: `imgui/LICENSE.txt` and `subhook/LICENSE`.
subhook's was lifted verbatim from the header comment in its own
`subhook/subhook.h`; imgui's is the upstream MIT text for the vendored version
(1.91.5). If either dependency is updated, refresh its licence file too.

## Neverwinter Nights: Enhanced Edition

Not included and not modified. This project only injects into a copy you already
own. Symbol names and struct offsets documented in `AGENTS.md` were obtained by
inspecting the shipped binaries for interoperability.
