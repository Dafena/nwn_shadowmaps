// ===========================================================================
//  Engine bindings (resolved by mangled name from .symtab, so these survive
//  patches instead of pinning build offsets).
// ===========================================================================
namespace eng {
    using SceneRender_t          = void (*)(void* self);
    using ManageSceneBSP_t       = void (*)(void* self);
    using BSPTraverseAll_t       = void (*)(void* root,
                                             void (*callback)(void*, void*),
                                             void* user);
    using AddPartToMeshBuckets_t = void (*)(void* part);
    using SceneAddPartsToDrawBuckets_t = void (*)(void* self);
    using SceneRenderDrawBucket_t= void (*)(void* self, int bucket);
    using SceneRenderShadows_t   = void (*)(void* self, int light, bool clearStencil);
    using SceneRenderSinglePass_t = void (*)(void* self);
    using SceneRenderDynamicGeometry_t = void (*)(void* self);
    using CameraRender_t         = void (*)(void* self, bool renderChildren);
    using CameraRenderScene_t    = void (*)(void* self);
    using LightPrioritizeShadow_t= void (*)(void* self);
    // Disassembly confirms this returns the address of LightManager's internal
    // selected-light list in RAX.  Phase 1 only logs that returned address;
    // it does not infer the list layout or read any PartLight fields.
    using LightGetShadowLights_t = void* (*)(void* self, int mode);
    // *** GLRender's methods are STATIC -- there is NO `this` parameter. ***
    // Proof: GLRender::SetViewTransform does `mov $0x1,%edi; call SetMatrixMode`,
    // passing the mode itself in %rdi. The class keeps static data members
    // (m_nCurrentMatrix, m_aurMtxStack) reached RIP-relative and never
    // dereferences an instance. Declaring a leading void* self here silently
    // shifts every argument by one register: the first capture read %rdi
    // (really &pos) as `this` -- a stack address -- and %rsi (really &quat) as
    // the position, and calling SetMatrixMode(instance, mode) fed a stale stack
    // pointer in as the mode and indexed the matrix stack out of bounds.
    using SetViewTransform_t     = void (*)(void* pos, void* quat);
    using SetPerspective_t       = void (*)(double a, double b,
                                            double c, double d);
    using SetOrtho_t             = void (*)(double l, double r,
                                            double b, double t,
                                            double n, double f);
    // aurMatrixStack::Perspective(float,float,float,float) -- hooked instead of
    // GLRender::SetPerspectiveTransform, whose prologue is not relocatable.
    using MtxPerspective_t       = void (*)(void* self, float fov, float aspect,
                                            float zn, float zf);
    using AreaUpdateShadowingLights_t = void (*)(void* self);
    // The THREE calls CNWCArea::UpdateShadowingLights() ends in. Hooking these
    // observes the engine's own decision instead of re-deriving it from the
    // area's flag fields, which is what makes area shadow policy work on
    // Windows: nwmain.exe exports all three but exports NOTHING of CNWCArea.
    using AurShadowToggle_t      = void (*)(void* aurObject);
    using AurSetDynProjLight_t   = void (*)(void* aurObject);
#ifndef _WIN32
    using AurTextureBindInUnit_t = void (*)(void* texture, unsigned int unit,
                                            int controllerIndex);
    using AurTextureGetName_t    = const char* (*)(void* texture);
#endif

    SceneRender_t           SceneRender           = nullptr;
    ManageSceneBSP_t        ManageSceneBSP        = nullptr;
    BSPTraverseAll_t        BSPTraverseAll        = nullptr;
    AddPartToMeshBuckets_t  AddPartToMeshBuckets  = nullptr;
    SceneAddPartsToDrawBuckets_t SceneAddPartsToDrawBuckets = nullptr;
    SceneRenderDrawBucket_t SceneRenderDrawBucket = nullptr;
    SceneRenderShadows_t    SceneRenderShadows    = nullptr;
    SceneRenderSinglePass_t SceneRenderSinglePass = nullptr;
    SceneRenderDynamicGeometry_t SceneRenderDynamicGeometry = nullptr;
    CameraRender_t          CameraRender          = nullptr;
    CameraRenderScene_t     CameraRenderScene     = nullptr;
    LightPrioritizeShadow_t LightPrioritizeShadow = nullptr;
    LightGetShadowLights_t  LightGetShadowLights  = nullptr;
    // SetLightGL(PartLight const*, int, float) -- the engine's own "enable this
    // light in GL" call. GetShadowLights returns only the SHADOW-CASTER subset
    // (measured: the carried torch and nothing else, while four lamps lit the
    // area), so it is the wrong source for the sun-shadow lift. Every light the
    // engine actually shades with passes through here.
    typedef void (*SetLightGL_t)(const void*, int, float);
    SetLightGL_t            SetLightGL            = nullptr;
    // GetLightAdjustedRadius(PartLight const*) -- the engine's OWN accessor for
    // a light's effective radius. The raw field at kPartLightRadiusOff reported
    // 0.1 for every lamp reaching SetLightGL (measured: 8 lights, all r=0.1),
    // which made the sun-shadow lift fall off within 10 cm and vanish. Ask the
    // engine instead of decoding a field whose meaning depends on the caller.
    typedef float (*GetLightAdjustedRadius_t)(const void*);
    GetLightAdjustedRadius_t GetLightAdjustedRadius = nullptr;
    // GetLightFadeSpeed(PartLight const*) -- non-zero on lights that animate,
    // which is what an EMITTER's light does (flames, spell effects). Used to
    // keep them out of the shadow-casting set: they are numerous, short-lived
    // and each one costs a full depth capture per frame.
    typedef float (*GetLightFadeSpeed_t)(const void*);
    GetLightFadeSpeed_t     GetLightFadeSpeed     = nullptr;
    // SDL_PollEvent is not real SDL code in this binary: it is a five-byte
    // thunk that jumps through a writable function pointer. We resolve the
    // thunk only to find that pointer and patch it (see
    // install_sdl_pollevent_patch), which is the same "swap the pointer"
    // hook the GLEW entry points use -- no subhook, no prologue relocation.
    void*                   SdlPollEventThunk     = nullptr;
    SetViewTransform_t      SetViewTransform      = nullptr;
    SetPerspective_t        SetPerspective        = nullptr;   // called, not hooked
    SetOrtho_t              SetOrtho              = nullptr;   // called, not hooked
    MtxPerspective_t        MtxPerspective        = nullptr;   // hooked
    AreaUpdateShadowingLights_t AreaUpdateShadowingLights = nullptr;
    AurShadowToggle_t       AurEnableShadowing    = nullptr;
#ifndef _WIN32
    AurTextureBindInUnit_t  AurTextureBindInUnit  = nullptr;
    AurTextureGetName_t     AurTextureGetName     = nullptr;
#endif
    AurShadowToggle_t       AurDisableShadowing   = nullptr;
    AurSetDynProjLight_t    AurSetDynProjLight    = nullptr;
    // GLRender::SetMatrixMode(Aurora::AuroraTransStates). The enum VALUES are
    // not in the symbol table; SetViewTransform is seen passing 1. Exposed so
    // the projection stack can be selected by experiment, not by guesswork.
    using SetMatrixMode_t        = void (*)(int mode);
    SetMatrixMode_t         SetMatrixMode         = nullptr;   // called, not hooked
    // Direct view-matrix composition (view substitution mode 2).
    using LoadIdentity_t         = void (*)();
    using Translate_t            = void (*)(float x, float y, float z);
    using Rotate_t               = void (*)(float deg, float x, float y, float z);
    // Static DATA symbols. GLRender keeps its matrix stacks here: 4 entries of
    // 88 bytes (stride confirmed as 11*8 in SetOrthoTransform's disassembly),
    // selected by m_nCurrentMatrix. Reading these gives the engine's ACTUAL
    // matrices instead of guessing at SetViewTransform's semantics.
    void*                   MtxStack              = nullptr;  // m_aurMtxStack
    int*                    CurMtxIdx             = nullptr;  // m_nCurrentMatrix
    // The engine's area lighting, also static data. m_lightAreaDiffuseDirection
    // IS the sun direction -- no need to invent one, and no need to hook the
    // shadow system to get at it. 16-byte spacing => each is a vec4.
    const float*            SunDir                = nullptr;
    const float*            SunDiffuse            = nullptr;
    const float*            SunAmbient            = nullptr;
    // Address of GLEW's glUseProgram FUNCTION POINTER (a writable BSS
    // variable, not the function). Patching it is the cleanest hook in this
    // binary: no trampoline, no prologue relocation -- just assign.
    void**                  GlewUseProgram        = nullptr;
    // Matrix uniforms go through the same GLEW indirection.  During the
    // bucket replay the engine can retain camera-space m_mvp/m_mv values even
    // though we replaced its matrix stack.  Intercepting this single upload
    // point lets the injector give every replayed caster an actual light-space
    // transform, while leaving the normal camera and stencil paths untouched.
    void**                  GlewUniformMatrix4fv  = nullptr;
    void**                  GlewUniform3f         = nullptr;
    void**                  GlewUniform3fv        = nullptr;
    // The normal-world geometry did not reach the executable's GL 1.1 draw
    // imports. These GLEW extension/core dispatch slots cover the remaining
    // draw paths exported by this build and are trace-only at this stage.
    void**                  GlewDrawRangeElements  = nullptr;
    void**                  GlewMultiDrawArrays    = nullptr;
    void**                  GlewMultiDrawElements  = nullptr;
    void**                  GlewDrawElementsBaseVertex = nullptr;
    void**                  GlewDrawRangeElementsBaseVertex = nullptr;
    void**                  GlewDrawElementsInstanced = nullptr;
    void**                  GlewDrawElementsInstancedBaseVertex = nullptr;
    // Same mechanism for runtime shader-source injection. This lets the .so
    // instrument compiled shaders without changing a single .shd on disk.
    void**                  GlewShaderSource      = nullptr;
    void**                  GlewCreateShader      = nullptr;
    void**                  GlewCompileShader     = nullptr;
    void**                  GlewGetShaderiv       = nullptr;
    void**                  GlewGetShaderInfoLog  = nullptr;
    void**                  GlewCreateProgram     = nullptr;
    void**                  GlewAttachShader      = nullptr;
    void**                  GlewLinkProgram       = nullptr;
    void**                  GlewGetProgramiv      = nullptr;
    void**                  GlewGetProgramInfoLog = nullptr;
    void**                  GlewDeleteShader      = nullptr;
    void**                  GlewDeleteProgram     = nullptr;
    using FlagDirty_t            = void (*)();
    FlagDirty_t             FlagDirty             = nullptr;
    LoadIdentity_t          LoadMatrixIdentity    = nullptr;
    Translate_t             Translate             = nullptr;
    Rotate_t                Rotate                = nullptr;
    // Global CExoArrayList objects.  ClearBuckets() writes their count at
    // offset +8, and CExoArrayList::Add() uses the same ABI; only that count
    // is read by Phase 4b.
    int*                    MeshShadowBucket      = nullptr;
    int*                    StaticShadowBucket    = nullptr;
    int*                    CountCulledPart       = nullptr;
    int*                    CountCulledShadows    = nullptr;
    int*                    CountBackShadows      = nullptr;
    // NWN's own stencil-shadow state, READ ONLY. `shadowalpha` is the engine's
    // shadow opacity: the maintainer observes it ramping 100 -> 0 going night
    // to day and back, for CREATURE shadows specifically. It is 4 bytes and the
    // symbol table does not say whether that is a float or an int, so it is
    // bound as raw storage and interpreted at the probe rather than guessed.
    const uint32_t*         ShadowAlphaRaw        = nullptr;
    // NWN's own "Lighting Max Lights" video setting, live. The injector used to
    // ship its own copy of this number ("Light supported"), which could only
    // ever disagree with the engine's.
    const int32_t*          LightManagerMaxLights = nullptr;
    // The creature / non-creature split that matches "this is JUST for creatures".
    const uint32_t*         NonCharacterDynamicShadowsRaw = nullptr;
}

static const struct { const char* mangled; void** slot; bool required; } kBindings[] = {
    // Scene::Render()                                             >>> VERIFY
    {"_ZN5Scene6RenderEv",             (void**)&eng::SceneRender,           true },
    {"_Z14ManageSceneBSPP5Scene",      (void**)&eng::ManageSceneBSP,        false},
    {"_Z11BSPTraverseP7BSPNodePFvS0_PvES1_",
                                       (void**)&eng::BSPTraverseAll,        false},
    // The full-BSP path must go through these native helpers.  Calling a
    // PartTriMesh draw directly skips its model transform and material bucket
    // setup; the old camera-following experiments showed that is not viable.
    {"_Z20AddPartToMeshBucketsP11PartTriMesh",
                                       (void**)&eng::AddPartToMeshBuckets,  false},
    {"_ZN5Scene21AddPartsToDrawBucketsEv",
                                       (void**)&eng::SceneAddPartsToDrawBuckets, false},
    // Scene::RenderDrawBucket(int) -- optional: without it the Phase 2 probe
    // and the Phase 3 light pass are unavailable, but Phase 1 still installs.
    {"_ZN5Scene16RenderDrawBucketEi",  (void**)&eng::SceneRenderDrawBucket, false},
    // The engine's stable stencil-volume pass. Used only for opt-in tracing;
    // no shadow-map state is substituted here.
    {"_ZN5Scene13RenderShadowsEib",    (void**)&eng::SceneRenderShadows,    false},
    {"_ZN5Scene16RenderSinglePassEv",  (void**)&eng::SceneRenderSinglePass, false},
    {"_ZN5Scene21RenderDynamicGeometryEv", (void**)&eng::SceneRenderDynamicGeometry, false},
    {"_ZN6Camera6RenderEb",             (void**)&eng::CameraRender,          false},
    {"_ZN6Camera11RenderSceneEv",       (void**)&eng::CameraRenderScene,     false},
    {"_ZN12LightManager16PrioritizeShadowEv", (void**)&eng::LightPrioritizeShadow, false},
    {"_ZN12LightManager15GetShadowLightsEi", (void**)&eng::LightGetShadowLights, false},
    {"_ZN12LightManager12m_nMaxLightsE",
                              (void**)&eng::LightManagerMaxLights,          false},
    {"_Z10SetLightGLPK9PartLightif",           (void**)&eng::SetLightGL,           false},
    {"_Z22GetLightAdjustedRadiusPK9PartLight", (void**)&eng::GetLightAdjustedRadius, false},
    {"_Z17GetLightFadeSpeedPK9PartLight",      (void**)&eng::GetLightFadeSpeed,      false},
#ifndef _WIN32
    {"_ZN11CAurTexture10BindInUnitEji", (void**)&eng::AurTextureBindInUnit, false},
    {"_ZN11CAurTexture7GetNameEv",      (void**)&eng::AurTextureGetName,    false},
#endif
    {"SDL_PollEvent",                  (void**)&eng::SdlPollEventThunk,     false},
    // GLRender matrix entry points. Hooked to capture the renderer instance +
    // camera state; called directly to substitute the light's transform.
    {"_ZN8GLRender16SetViewTransformER6VectorR10Quaternion",
                                       (void**)&eng::SetViewTransform,      false},
    {"_ZN8GLRender23SetPerspectiveTransformEdddd",
                                       (void**)&eng::SetPerspective,        false},
    {"_ZN8GLRender17SetOrthoTransformEdddddd",
                                       (void**)&eng::SetOrtho,              false},
    // Hooked in place of SetPerspectiveTransform -- see the RIP-relative note
    // in the header. This one's prologue is relocatable.
    {"_ZN14aurMatrixStack11PerspectiveEffff",
                                       (void**)&eng::MtxPerspective,        false},
    {"_ZN8CNWCArea21UpdateShadowingLightsEv",
                              (void**)&eng::AreaUpdateShadowingLights,     false},
    // See AurShadowToggle_t. Verified against the 2026-01 build: the ONLY
    // references to AurSetDynamicProjectionLight in the whole binary are three
    // tail jumps from inside UpdateShadowingLights, and every path reaching it
    // has exactly one preceding Enable/Disable -- so it marks the end of an
    // area update and disambiguates the area's toggle from
    // CNWCVisualEffectOnObject::EnableHardCodedEffectShadow, the only other
    // caller of the toggles, which never reaches it.
    {"_Z18AurEnableShadowingP10CAurObject",
                                       (void**)&eng::AurEnableShadowing,    false},
    {"_Z19AurDisableShadowingP10CAurObject",
                                       (void**)&eng::AurDisableShadowing,   false},
    {"_Z28AurSetDynamicProjectionLightP10CAurObject",
                                       (void**)&eng::AurSetDynProjLight,    false},
    {"_ZN8GLRender13SetMatrixModeEN6Aurora17AuroraTransStatesE",
                                       (void**)&eng::SetMatrixMode,         false},
    {"_ZN8GLRender18LoadMatrixIdentityEv",
                                       (void**)&eng::LoadMatrixIdentity,    false},
    {"_ZN8GLRender9TranslateEfff",     (void**)&eng::Translate,             false},
    {"_ZN8GLRender6RotateEffff",       (void**)&eng::Rotate,                false},
    {"_ZN8GLRender25FlagCurrentTransformDirtyEv",
                                       (void**)&eng::FlagDirty,             false},
    {"meshshadowbucket",               (void**)&eng::MeshShadowBucket,      false},
    {"staticshadowbucket",             (void**)&eng::StaticShadowBucket,    false},
    {"countculledpart",                (void**)&eng::CountCulledPart,       false},
    {"countculledshadows",             (void**)&eng::CountCulledShadows,    false},
    {"countbackshadows",               (void**)&eng::CountBackShadows,      false},
    {"shadowalpha",                    (void**)&eng::ShadowAlphaRaw,        false},
    {"g_bNonCharacterDynamicShadows",  (void**)&eng::NonCharacterDynamicShadowsRaw, false},
    {"m_aurMtxStack",                  (void**)&eng::MtxStack,              false},
    {"_ZN8GLRender16m_nCurrentMatrixE",(void**)&eng::CurMtxIdx,             false},
    {"_ZN8GLRender27m_lightAreaDiffuseDirectionE",
                                       (void**)&eng::SunDir,                false},
    {"_ZN8GLRender18m_lightAreaDiffuseE",
                                       (void**)&eng::SunDiffuse,            false},
    {"_ZN8GLRender18m_lightAreaAmbientE",
                                       (void**)&eng::SunAmbient,            false},
    {"__glewUseProgram",               (void**)&eng::GlewUseProgram,        false},
    {"__glewUniformMatrix4fv",          (void**)&eng::GlewUniformMatrix4fv, false},
    {"__glewUniform3f",                  (void**)&eng::GlewUniform3f,       false},
    {"__glewUniform3fv",                 (void**)&eng::GlewUniform3fv,      false},
    {"__glewDrawRangeElements",           (void**)&eng::GlewDrawRangeElements,false},
    {"__glewMultiDrawArrays",             (void**)&eng::GlewMultiDrawArrays, false},
    {"__glewMultiDrawElements",           (void**)&eng::GlewMultiDrawElements,false},
    {"__glewDrawElementsBaseVertex",      (void**)&eng::GlewDrawElementsBaseVertex, false},
    {"__glewDrawRangeElementsBaseVertex", (void**)&eng::GlewDrawRangeElementsBaseVertex, false},
    {"__glewDrawElementsInstanced",       (void**)&eng::GlewDrawElementsInstanced, false},
    {"__glewDrawElementsInstancedBaseVertex", (void**)&eng::GlewDrawElementsInstancedBaseVertex, false},
    {"__glewShaderSource",             (void**)&eng::GlewShaderSource,      false},
    {"__glewCreateShader",             (void**)&eng::GlewCreateShader,      false},
    {"__glewCompileShader",            (void**)&eng::GlewCompileShader,     false},
    {"__glewGetShaderiv",              (void**)&eng::GlewGetShaderiv,       false},
    {"__glewGetShaderInfoLog",         (void**)&eng::GlewGetShaderInfoLog,  false},
    {"__glewCreateProgram",            (void**)&eng::GlewCreateProgram,     false},
    {"__glewAttachShader",             (void**)&eng::GlewAttachShader,      false},
    {"__glewLinkProgram",              (void**)&eng::GlewLinkProgram,       false},
    {"__glewGetProgramiv",             (void**)&eng::GlewGetProgramiv,      false},
    {"__glewGetProgramInfoLog",        (void**)&eng::GlewGetProgramInfoLog, false},
    {"__glewDeleteShader",             (void**)&eng::GlewDeleteShader,      false},
    {"__glewDeleteProgram",            (void**)&eng::GlewDeleteProgram,     false},
};

static const size_t MTX_STRIDE  = 88;   // 11*8, from SetOrthoTransform
static const int    MTX_ENTRIES = 4;    // (m_nCurrentMatrix - m_aurMtxStack)/88
// LAYOUT DECODED from a live dump (2026-07-25): each entry is 3 pointers (24
// bytes) followed by 16 floats (64) = 88 exactly. The matrix is COLUMN-MAJOR
// OpenGL, standard lookAt: m[0..2]=right, m[4..6]=up, m[8..10]=-forward,
// m[12..14]=(-dot(s,eye), -dot(u,eye), dot(f,eye)). Verified numerically
// against the engine's own view matrix and eye position.
static const size_t MTX_MAT_OFF = 24;
static const int    MTX_PROJ    = 0;    // entry 0 held the 45deg/0.1/45 frustum
static const int    MTX_VIEW    = 1;    // entry 1 held R*T(-eye)
