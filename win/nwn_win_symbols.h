// Linux (Itanium/GCC) -> Windows (MSVC) symbol map for NWN:EE.
//
// The Linux injector resolves engine functions by walking the unstripped
// ELF .symtab. nwmain.exe is STRIPPED (MSVC 2017; the PDB path is baked in but
// Beamdog does not ship the PDB) -- however it EXPORTS 18,245 named symbols,
// which covers every function and, crucially, every static data symbol the
// injector needs. So on Windows resolution is simply:
//     GetProcAddress(GetModuleHandleW(nullptr), <MSVC name>)
//
// Names below were extracted from nwmain.exe's export table and matched
// one-to-one against the Linux binding list; all resolved unambiguously.
// Verify against the game build you are actually running before trusting them:
// a Beamdog patch can change a signature, and a wrong-but-present name would
// bind silently to the wrong function.
#pragma once

struct NwnWinSymbol {
    const char* linuxName;   // for cross-referencing with the Linux source
    const char* winName;     // MSVC-mangled export in nwmain.exe
    bool        required;    // abort/report loudly if missing
};

static const NwnWinSymbol kNwnWinSymbols[] = {
    // --- scene / render entry points -------------------------------------
    {"_ZN5Scene6RenderEv",                     "?Render@Scene@@UEAAXXZ",                        true },
    {"_Z14ManageSceneBSPP5Scene",              "?ManageSceneBSP@@YAXPEAVScene@@@Z",             false},
    {"_Z11BSPTraverseP7BSPNodePFvS0_PvES1_",   "?BSPTraverse@@YAXPEAVBSPNode@@P6AX0PEAX@Z1@Z",  false},
    {"_Z20AddPartToMeshBucketsP11PartTriMesh", "?AddPartToMeshBuckets@@YAXPEAVPartTriMesh@@@Z", false},
    {"_ZN5Scene21AddPartsToDrawBucketsEv",     "?AddPartsToDrawBuckets@Scene@@QEAAXXZ",         false},
    {"_ZN5Scene16RenderDrawBucketEi",          "?RenderDrawBucket@Scene@@QEAAXH@Z",             false},
    {"_ZN5Scene13RenderShadowsEib",            "?RenderShadows@Scene@@UEAAXH_N@Z",              false},
    {"_ZN5Scene16RenderSinglePassEv",          "?RenderSinglePass@Scene@@UEAAXXZ",              false},
    {"_ZN5Scene21RenderDynamicGeometryEv",     "?RenderDynamicGeometry@Scene@@UEAAXXZ",         false},
    {"_ZN6Camera6RenderEb",                    "?Render@Camera@@UEAAX_N@Z",                     false},
    {"_ZN6Camera11RenderSceneEv",              "?RenderScene@Camera@@UEAAXXZ",                  false},

    // --- lights -----------------------------------------------------------
    {"_ZN12LightManager16PrioritizeShadowEv",  "?PrioritizeShadow@LightManager@@EEAAXXZ",       false},
    {"_ZN12LightManager15GetShadowLightsEi",
     "?GetShadowLights@LightManager@@QEAAAEAV?$List@PEAVPartLight@@@@H@Z",                      false},

    // --- matrix stack: the core of the light-substitution technique -------
    {"_ZN8GLRender16SetViewTransformER6VectorR10Quaternion",
     "?SetViewTransform@GLRender@@CAXAEAVVector@@AEAVQuaternion@@@Z",                           false},
    {"_ZN8GLRender23SetPerspectiveTransformEdddd", "?SetPerspectiveTransform@GLRender@@CAXNNNN@Z",   false},
    {"_ZN8GLRender17SetOrthoTransformEdddddd",     "?SetOrthoTransform@GLRender@@CAXNNNNNN@Z",       false},
    {"_ZN14aurMatrixStack11PerspectiveEffff",      "?Perspective@aurMatrixStack@@QEAAXMMMM@Z",       false},
    {"_ZN8GLRender13SetMatrixModeEN6Aurora17AuroraTransStatesE",
     "?SetMatrixMode@GLRender@@CAXW4AuroraTransStates@Aurora@@@Z",                              false},
    {"_ZN8GLRender18LoadMatrixIdentityEv",         "?LoadMatrixIdentity@GLRender@@CAXXZ",            false},
    {"_ZN8GLRender9TranslateEfff",                 "?Translate@GLRender@@CAXMMM@Z",                  false},
    {"_ZN8GLRender6RotateEffff",                   "?Rotate@GLRender@@CAXMMMM@Z",                    false},
    {"_ZN8GLRender25FlagCurrentTransformDirtyEv",  "?FlagCurrentTransformDirty@GLRender@@CAXXZ",     false},

    // --- static DATA symbols (read directly; no hook, no risk) ------------
    {"m_aurMtxStack",                    "?m_aurMtxStack@@3PAVaurMatrixStack@@A",                    false},
    {"_ZN8GLRender16m_nCurrentMatrixE",  "?m_nCurrentMatrix@GLRender@@0IA",                          false},
    {"_ZN8GLRender27m_lightAreaDiffuseDirectionE",
     "?m_lightAreaDiffuseDirection@GLRender@@0VVector@@A",                                           false},
    {"_ZN8GLRender18m_lightAreaDiffuseE","?m_lightAreaDiffuse@GLRender@@0VVector@@A",                false},
    {"_ZN8GLRender18m_lightAreaAmbientE","?m_lightAreaAmbient@GLRender@@0VVector@@A",                false},
    {"meshshadowbucket",                 "?meshshadowbucket@@3V?$List@PEAVPartTriMesh@@@@A",         false},
    {"staticshadowbucket",               "?staticshadowbucket@@3V?$List@PEAVPartShadow@@@@A",        false},
    {"countculledpart",                  "?countculledpart@@3HA",                                    false},
    {"countculledshadows",               "?countculledshadows@@3HA",                                 false},
    {"countbackshadows",                 "?countbackshadows@@3HA",                                   false},

    // --- Lights feeding the sun-shadow lift --------------------------------
    // Missing these made the lift silently do nothing on Windows: the census
    // hook never installed, so the lamp list was empty every frame while the
    // log said only "UNRESOLVED (optional)". Verified against the export table
    // of the shipped bin/win32/nwmain.exe -- note PartLight mangles as V
    // (class), not U (struct), which a hand-written guess gets wrong.
    {"_Z10SetLightGLPK9PartLightif",
     "?SetLightGL@@YAXPEBVPartLight@@HM@Z",                                                       false},
    {"_Z22GetLightAdjustedRadiusPK9PartLight",
     "?GetLightAdjustedRadius@@YAMPEBVPartLight@@@Z",                                             false},
    {"_Z17GetLightFadeSpeedPK9PartLight",
     "?GetLightFadeSpeed@@YAMPEBVPartLight@@@Z",                                                  false},

    // --- material identity transport ------------------------------------
    // Exact exports from v89.8193.37-17. These support the read-only Windows
    // material census and, after lifecycle proof, strict Mode 2 routing.
    // BindInUnit is deliberately absent: its detour corrupted texture state
    // on Linux and normal identity routing does not need it.
    {"_ZN11CAurTexture7GetNameEv",      "?GetName@CAurTexture@@UEAAPEADXZ",                    false},
    {"_ZN8Material23BindAllStandardTexturesEv",
                                         "?BindAllStandardTextures@Material@@QEAAXXZ",          false},
    {"_ZN8Material10GetTextureEi",      "?GetTexture@Material@@QEAAPEAVCAurTexture@@H@Z",      false},
    {"_ZN8Material18InitSharedMaterialEPKc",
                                         "?InitSharedMaterial@Material@@AEAAPEAVSharedMaterial@@PEBD@Z", false},
    {"_ZN14SharedMaterial4InitEPKc",      "?Init@SharedMaterial@@QEAAXPEBD@Z",                      false},
    {"_ZN14SharedMaterial10ParseFieldEPKc",
                                         "?ParseField@SharedMaterial@@QEAAXPEBD@Z",              false},

    // --- NWN's own stencil-shadow state (read only) -----------------------
    // shadowalpha is the engine's shadow opacity and the source of the
    // day/night fade; the MSVC mangling confirms the type the Linux probe
    // inferred from raw bytes -- `3MA` is a float, `3HA` an int.
    {"shadowalpha",                    "?shadowalpha@@3MA",                        false},
    {"_ZN12LightManager12m_nMaxLightsE",
                                       "?m_nMaxLights@LightManager@@0HA",          false},
    // AREA SHADOW POLICY ON WINDOWS. nwmain.exe exports NOTHING of CNWCArea --
    // the only CNWCArea strings in it are RTTI type descriptors -- so
    // UpdateShadowingLights can never be hooked by name here, and it is not
    // virtual either (absent from the 14-entry CNWCArea vtable), so RTTI does
    // not reach it. These three are what that function CALLS, and all three are
    // exported. Observing them recovers the engine's own decision.
    {"_Z18AurEnableShadowingP10CAurObject",
                                       "?AurEnableShadowing@@YAXPEAVCAurObject@@@Z",       false},
    {"_Z19AurDisableShadowingP10CAurObject",
                                       "?AurDisableShadowing@@YAXPEAVCAurObject@@@Z",      false},
    {"_Z28AurSetDynamicProjectionLightP10CAurObject",
                                       "?AurSetDynamicProjectionLight@@YAXPEAVCAurObject@@@Z", false},
    {"g_bNonCharacterDynamicShadows",  "?g_bNonCharacterDynamicShadows@@3HA",      false},

    // --- SDL (statically linked; 629 SDL_* exports) -----------------------
    {"SDL_PollEvent",                    "SDL_PollEvent",                                            false},
    {"SDL_GetMouseState",                "SDL_GetMouseState",                                        false},
    {"SDL_GetKeyboardState",             "SDL_GetKeyboardState",                                     false},
};

static const int kNwnWinSymbolCount =
    (int)(sizeof(kNwnWinSymbols) / sizeof(kNwnWinSymbols[0]));
