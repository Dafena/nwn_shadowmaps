# Native Windows release check

Status: pending. Proton passed the layer/torch path but does not certify native
Windows graphics drivers. Use a separate test installation/profile and back up
any existing proxy DLL. Keep the accepted area HAK with its matching WorldMaker
v6 shaders and per-priority material identities; the plugin does not rewrite it.

1. Build `win/version.dll`, run `make verify` against the exact game executable,
   and record the DLL/executable hashes, game version, GPU/driver and MSAA.
2. Place the DLL beside that test `nwmain.exe`. Enable `NWN_SHADOWMAP_LOG=1`.
   Confirm both `Windows virtual Draw slot active` and `explicit0..9 shader
   layers active` in the log. Missing/ambiguous hooks must leave native drawing.
3. Load the accepted Type-0 scene. At matching positions inspect the left broken
   waterfall, right waterfalls, rising wall rims, foreground shoreline, orange
   overlays and central ground. Rotate, approach and zoom out; verify solid rocks
   and the character occlude the layers. Equal hints retain native tie behaviour.
4. Equip/unequip the inventory torch. Compare the same floor/wall views with
   area ambient/diffuse, night and fog. Check ordinary stock areas too.
5. In one initialized game, move clear→rain→snow→interior→clear. Record engine
   weather and area flags, precipitation, surface accumulation and immediate
   interior reset. Merely setting a chance in an area file is not confirmation
   that the engine actually chose that weather.
6. New private rain and native-rain hiding start off. Compare preview off/on,
   then hiding off/on during verified rain; check roofs, foliage, spell effects,
   UI, transitions and restoration when prerequisites disappear.
7. Compare matched baseline/candidate frame times at2048/32768/262144 rain
   budgets, moving camera, many lights and crowded multiplayer snow trails.
   State whether VSync caps the result. Check settings save/reset and save/load.

Attach matching views, logs and timing windows to the integration PR. Record any
failure and keep the release draft until the remaining gates in
[release audit](RELEASE_AUDIT.md) are resolved. No DLL build, parser success or
load-only test substitutes for these visual checks.
