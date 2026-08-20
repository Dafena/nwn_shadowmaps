// nwn_shadowmap.cpp
// ---------------------------------------------------------------------------
// Shadow maps for Neverwinter Nights: Enhanced Edition (Linux, nwmain-linux).
//
// PHASE 1 (DONE, verified in-engine 2026-07-25)
//   Detour Scene::Render(), create a depth-only FBO + depth texture, bind it
//   each frame, clear it, restore all touched GL state. Proved the hook point,
//   the render target, and the state save/restore. Game rendered identically.
//
// PHASE 2 (THIS FILE) -- CAN WE PUT GEOMETRY IN IT?
//   Everything after this depends on one unknown: is Scene::RenderDrawBucket()
//   safely callable outside the engine's own render sequence? If yes, we can
//   replay the scene into the depth target in the same frame, which keeps bone
//   matrices valid and costs no latency. If no, we must capture primitives in
//   frame N and replay them in frame N+1.
//
//   PROBE v2 -- the first version fired once at a fixed frame number and found
//   empty buckets, which is unfalsifiable: it cannot tell "buckets are empty
//   because nothing is loaded" from "buckets are empty because this call does
//   not work". v2 fixes that three ways:
//
//     1. WALL-CLOCK SCHEDULING + RETRY. Frame counts are meaningless while a
//        module loads. The probe now waits a real delay, then RETRIES on an
//        interval until it finds geometry or gives up. You cannot mistime it.
//     2. BOTH SIDES OF Scene::Render(). Buckets may be filled inside Render()
//        and cleared at its end (ClearBuckets(Scene*) exists), in which case
//        probing after it will ALWAYS find nothing. Each attempt now probes
//        before AND after the original call and reports which side works.
//     3. OCCLUSION QUERIES instead of full depth readback. GL_SAMPLES_PASSED
//        answers "did this draw anything" exactly, for free, so retrying is
//        cheap. Full readback is kept only for the final PGM dump.
//
//   The probe also forces GL_DEPTH_TEST on and depth writes enabled, so a
//   disabled depth test left over from the engine cannot silently produce an
//   empty target.
//
//   Deliberately, it renders with the engine's CURRENT camera matrices and
//   CURRENT shaders -- no light matrices, no depth-only shader. One variable at
//   a time. If the mechanism works, the dumped PGM is a depth image of the
//   scene from the camera, which is unmistakable when you look at it. Swapping
//   in the light's viewpoint is Phase 3 and is comparatively trivial.
//
// CRASH GUARD
//   RenderDrawBucket may fault if it depends on setup we did not reproduce.
//   The call is wrapped in a SIGSEGV/SIGBUS/SIGFPE + siglongjmp guard so a
//   fault becomes a log line and self-disables instead of killing the client.
//   NOTE: longjmp-ing out of a fault leaves engine and GL state undefined. The
//   probe disables itself after a fault, but RESTART THE GAME before trusting
//   anything you see afterwards.
//
// WHY RAW GL AND NOT RenderInterface::
//   The engine exposes GenFramebuffer/SetFramebufferRenderTarget/etc. by name,
//   but their arguments are Aurora:: enums whose *values* are compile-time
//   constants and are NOT in the symbol table. Guessing them is how you get a
//   silent corrupt state. Raw GL has no such ambiguity, and the GL context is
//   already current when Scene::Render() runs. Later phases can move to the
//   engine API where it actually buys something (matrix + shader plumbing).
//
// GL entry points are resolved with dlsym(RTLD_DEFAULT) -- the process already
// links libGL.so.1 -- so this needs no GL headers at all.
//
// BUILD:  make
// RUN:    ./run-nwn.sh /path/to/nwmain-linux
//
// ENVIRONMENT
//   NWN_SHADOWMAP_OFF=1           load but install nothing (A/B, no rebuild)
//   NWN_SHADOWMAP_SIZE=2048       depth target resolution (power of two)
//   NWN_SHADOWMAP_VERBOSE=1       log every frame instead of once
//   NWN_SHADOWMAP_DUMP=N          N SECONDS after load, write shadowmap_dump.pgm
//   NWN_SHADOWMAP_PROBE=1         arm the Phase 2 probe (retries until it finds
//                                 geometry -- no need to time it yourself)
//   NWN_SHADOWMAP_PROBE_DELAY=60  seconds to wait before the first attempt
//   NWN_SHADOWMAP_PROBE_EVERY=5   seconds between retries
//   NWN_SHADOWMAP_PROBE_TRIES=24  give up after this many attempts
//   NWN_SHADOWMAP_BUCKETS=16      probe bucket indices 0..K-1
//   NWN_SHADOWMAP_TRACE=1         Phase 1 read-only camera/scene/light trace;
//                                 bypasses every legacy shadow-map pass
//   NWN_SHADOWMAP_TRACE_FRAMES=90 number of loaded-area frames to record
//   NWN_SHADOWMAP_TRACE_EVENTS=4096 maximum trace lines (safety bound)
//
// PHASE 3 -- RENDER FROM THE LIGHT
//   Replaces the camera view+projection with the light's before replaying the
//   buckets, then restores. Still no shader changes and nothing visible
//   in-game; the deliverable is a PGM that is a TOP-DOWN depth image instead
//   of the camera view.
//
//   GETTING A RENDERER INSTANCE. RenderInterface:: methods are pure tail-call
//   forwarders into GLRender:: with %rdi passed through untouched -- they are
//   NOT static and need a real `this`. There is no global instance symbol, so
//   we take one from the engine: hooking GLRender::SetViewTransform gives us
//   the instance pointer AND the live camera position/orientation, and
//   GLRender::SetPerspectiveTransform gives us the projection to restore.
//   (This is also why nwn_alphasort.cpp's GetModelMatrix() call, made with no
//   `this` at all, was reading a garbage matrix.)
//
//   *** DO NOT HOOK A FUNCTION WHOSE FIRST ~16 BYTES CONTAIN A RIP-RELATIVE
//   *** INSTRUCTION. subhook copies the displaced prologue into a trampoline at
//   *** a different address WITHOUT fixing up RIP-relative operands, so the
//   *** copied instruction resolves to garbage. This crashed the client on the
//   *** first attempt: GLRender::SetPerspectiveTransform begins with
//   ***   lea 0x151a149(%rip),%rax   ; -> GLRender::m_nCurrentMatrix
//   ***   ...
//   ***   mov (%rax),%eax            ; SIGSEGV on the mis-relocated pointer
//   *** Verified-safe prologues (position-independent for >= 15 bytes), checked
//   *** by disassembly before hooking:
//   ***   Scene::Render                    push/mov ...
//   ***   GLRender::SetViewTransform       push/mov/push/push/mov/mov $imm
//   ***   aurMatrixStack::Perspective      push/mov/push/mov/sub $imm32
//   *** CALLING an engine function is always safe -- only hooking is affected.
//   *** So we capture the projection from aurMatrixStack::Perspective (safe)
//   *** and still CALL GLRender::SetPerspectiveTransform to restore it.
//
//   The capture hooks are installed ONLY when the light pass is enabled, so a
//   problem here cannot affect the already-validated Phase 1 and Phase 2.
//
//   QUATERNION CONVENTION IS UNKNOWN. We do not know the engine's reference
//   forward axis or its quaternion component order, and guessing silently is
//   how this wastes a day. So NWN_SHADOWMAP_CONV selects among the four
//   plausible combinations and the PGM dump is the oracle -- sweep 0..3 and
//   keep whichever produces a top-down view.
//
// PHASE 3 ENVIRONMENT
//   NWN_SHADOWMAP_LIGHT=1         render the buckets from the light (else the
//                                 target is only cleared, as in Phase 1)
//   NWN_SHADOWMAP_DIR=x,y,z       light DIRECTION, world space, points from
//                                 the light toward the scene. Default 0,0,-1
//                                 (straight down; NWN is Z-up)
//   NWN_SHADOWMAP_CONV=0..3       quaternion convention to try (see above)
//   NWN_SHADOWMAP_EXTENT=40       ortho half-width in metres
//   NWN_SHADOWMAP_DIST=60         how far back along -dir to put the light
//   NWN_SHADOWMAP_WHEN=after      run the pass "after" (default) or "before"
//                                 the original Scene::Render()
//   NWN_SHADOWMAP_LIGHTBUCKETS=0,1,2,3,6,11,13   which buckets to replay
//
// Spots that must be confirmed against your build are marked  >>> VERIFY
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cfloat>
#include <ctime>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include <csetjmp>
#include <csignal>

#include "nwn_platform.h"   // must precede the POSIX headers it shims
#ifndef _WIN32
#include <link.h>      // dl_iterate_phdr
#include <elf.h>       // Elf64_* symtab parsing
// RTLD_NEXT needs _GNU_SOURCE, which g++ already defines for C++
#include <dlfcn.h>     // dlsym(RTLD_DEFAULT / RTLD_NEXT, ...)
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#else
#include "win/nwn_win_symbols.h"   // Itanium -> MSVC export-name map
#endif

#include "subhook.h"
#include "nwn_overlay.h"   // ImGui settings panel (its own TU; no ImGui headers here)
#include "nwn_hooks_core.h"   // cross-module contract (owned-pass guard, OIT entry)
#include "shadow_config.h"   // memoised settings + shipping defaults
#include "shadow_math.h"     // pure vector, matrix and projection helpers

// Settings lookup and shipping defaults live in shadow_config.cpp. Keeping
// this boundary independent from renderer state makes configuration changes
// cheap to compile and keeps startup policy out of the GL interposer.

// ===========================================================================
//  Config
// ===========================================================================
static int    g_size        = 2048;
static bool   g_verbose     = false;
static long   g_dumpAt      = -1;     // seconds after load; -1 = never
static double g_dumpDeadline= 0.0;
static long   g_mtxDumpAt    = -1;    // seconds after load; -1 = never
static double g_mtxDeadline  = 0.0;
static double g_statusEvery  = 30.0;  // seconds between health lines; 0 = off
static double g_nextStatus   = 0.0;
// How far ahead of the camera to centre the shadow box, in metres.
static float  g_focus        = 18.0f;
static bool   g_useCompare   = true;
static bool   g_noBuckets    = false;
static bool   g_dirOverride  = false;  // NWN_SHADOWMAP_DIR was set explicitly
static float  g_dirSign      = -1.0f;  // VALIDATED: m_lightAreaDiffuseDirection
                                       // points TOWARD the light, so negate it
static int    g_nBuckets    = 16;     // how many bucket indices to try  >>> VERIFY

// Phase 1 is intentionally *trace-only*.  It must never create an injector
// target, replay geometry, patch shader functions, or bind any GL object.  The
// previous red/green receiver remains in the binary as a diagnostic, but this
// mode bypasses that entire path so call ordering can be observed cleanly.
static bool     g_traceEnabled   = false;
// Opt-in, read-only local-light discovery.  This observes the documented
// CExoArrayList-shaped result returned by LightManager::GetShadowLights() and
// -- as of Phase 6b -- decodes PartLight's position/radius/colour fields (see
// kPartLightPosOff etc. and read_part_light() below for the disassembly
// evidence). It never WRITES to a PartLight or to the engine's selected-light
// list; the decode is a plain memcpy from offsets proven by cross-referencing
// four independent engine accessor functions.
static bool     g_localLightTrace = false;
// The lamp census that feeds the SUN-SHADOW LIFT. It used to ride on
// g_localLightTrace, a DEBUG flag: every Linux test command set it, the Windows
// built-in defaults did not, so on Windows the lift silently had no lights and
// looked like a regression from an unrelated change. A shipping feature must
// not depend on a diagnostic switch. NWN_SHADOWMAP_LAMP_CENSUS=0 disables it.
static bool     g_lampCensus     = true;
static unsigned g_localLightReports = 0;
static void*    g_localLightLastList = nullptr;
static uint32_t g_localLightLastCount = UINT32_MAX;
static int      g_localLightLastMode = -1;
// Phase 6b: single-light depth probe built on top of the census above.
static bool     g_localLightCapture      = false;  // NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE
// Phase 6d: make the captured local-light depth actually darken the screen.
// Same presentation as the validated sun composite (translucent black, its own
// strength) rather than a colour-tinted term -- NWN has no lighting model we
// could tint against without inventing one.
#if NWN_SHIP
static bool     g_localLightReceiver     = true;   // shipped on; panel hides it
#else
static bool     g_localLightReceiver     = false;  // NWN_SHADOWMAP_LOCAL_LIGHT_RECEIVER
#endif
// Local lights shadow the DYNAMIC buckets only (see the capture). A wall lamp
// casting the static world produces contact shadows hidden under their own
// casters; a creature lit by a torch is the case worth paying for.
static bool     g_localLightDynamicOnly  = true;
// 0 = try the dynamic buckets cold, 1 = render the static ones first because
// the engine will not draw the dynamic ones otherwise. Latched by measurement
// on the first capture, never guessed.
// Retained only for backwards-compatible settings parsing.  Local shadow-map
// ownership is NEVER selected from the SetLightGL census: NWN's
// LightManager::GetShadowLights() priority list owns that decision.
static constexpr int kLocalSlotsMax = 32;   // see kLocalLightFaces below
// Minimum far plane for a local light's shadow frustum, in world units. A
// carried torch reports a small radius, and radius*2 clipped its cone before it
// reached the floor.
static constexpr float kLocalLightMinRange = 20.0f;
static float    g_localLightSlotPos[kLocalSlotsMax][3] = {};
static float    g_localLightSlotRadius[kLocalSlotsMax] = {};

// ---------------------------------------------------------------------------
//  Engine-driven fade-out for local shadows (Option C)
// ---------------------------------------------------------------------------
// `shadowalpha` is the engine's own shadow opacity and it ramps continuously
// with time of day. Measured across one dawn it traces a V: it falls to ~0 as
// the moon's shadows go out, then CLIMBS BACK to a steady 0.5 as the sun takes
// over. One scalar, two different shadows.
//
// We only want the falling half. So this tracks a peak-relative ratio that can
// only ever DECREASE while a light is held:
//
//   * the peak is learned before the fall starts, so the absolute scale does
//     not matter -- and it is the missing piece that made an earlier attempt
//     fail. Scaling by the raw value (0.025) instead of by alpha/peak removed
//     local shadows entirely;
//   * once falling, a later rise cannot bring the shadow back, which is what
//     stops the sun's half of the V from re-lighting a local shadow;
//   * it resets to fully visible when a DIFFERENT light is selected, so a torch
//     lit later in the day still casts.
//
// Deliberately not 1:1 with the engine: reproducing that exactly would mean
// separating static from dynamic casters per light, which is not worth it.
static float       g_engineFadeLevel = 1.0f;   // 1 = full, 0 = faded out
static float       g_engineFadePeak  = 0.0f;
static const void* g_engineFadeLight = nullptr;

// update_engine_shadow_fade() is defined just before its caller: it needs the
// eng:: bindings and g_localLightSelected, both declared further down.
// NWN's per-light fade for each published slot, 0..1. Defaults to 1 so a slot
// that never received one behaves exactly as before.
static float    g_localLightSlotFade[kLocalSlotsMax] = {};
static unsigned g_localLightSlotCount     = 0;
static float    g_localLightStrength     = 0.77f;  // NWN_SHADOWMAP_LOCAL_LIGHT_STRENGTH
static float    g_localLightBias         = 0.0f;
// How much a local light's illumination cancels the SUN's shadow on the same
// surface. 1 = fully lit at the light's centre, fading with its radius; 0 =
// the old behaviour, where standing in torchlight inside a sun shadow dimmed
// everything. NWN_SHADOWMAP_LOCAL_LIGHT_LIFT.
static float    g_localLightLift         = 1.0f;
// How wide a band at the shadow map's border fades out, in UV. The shadow used
// to stop dead where the light's frustum ended.
static float    g_localLightEdgeFade     = 0.05f;
// LOCAL SHADOW FALLOFF CURVE. Exponent applied to the lamp's attenuation
// BEFORE it scales the local shadow's opacity. 1.0 is the old behaviour.
//
// The local term is (1-lit) * att * edge * slotFade, and att is NWN's own point
// light INTENSITY falloff. Tying opacity straight to intensity gives a range so
// wide that no single strength setting works: at the default the shadows across
// a room are nearly invisible, and turning strength up to compensate makes the
// ones right next to a lamp (the player's own torch) far too dark.
//
// An exponent below 1 compresses that range -- it lifts the mid and far field
// while leaving att==1 next to the lamp exactly where it was. A constant floor
// was the obvious alternative and is WRONG: att reaching 0 at the lamp's radius
// is what fades the shadow out smoothly, and a floor turns that into a hard
// visible ring at every light's edge. pow() keeps 0 at 0 and 1 at 1.
static float    g_localLightFalloff      = 0.52f;  // NWN_SHADOWMAP_LOCAL_FALLOFF
// PCF radius in texels for the local shadow's own outline.
static float    g_localLightSoft         = 0.51f;   // same default as the sun
// Slope-scaled depth offset used while FILLING a local shadow map. Kills the
// self-shadow acne a character gets from its own map; 0 disables it.
static float    g_localLightSlopeBias     = 1.8f;
// DEAD: setting the cull mode before the replay achieves nothing, because NWN
// sets its own cull state per draw and overwrites it before any geometry is
// stored -- confirmed in game, the map is identical either way. Making it work
// would mean intercepting glCullFace/glEnable for the whole local pass, which
// is not worth it now that "No self-shadow" solves the problem it was aimed at.
// Kept as a constant so the capture code reads clearly.
static constexpr bool g_localLightCullFront = false;
// Normal-offset bias, in shadow-map texels. The real cure for the acne pattern
// on characters: the lookup moves along the surface normal instead of along
// the light ray.
static float    g_localLightNormalBias    = 0.0f;   // maintainer-tuned
// Let dithered alpha cards (hair, cloaks) cast local shadows. Off by default:
// NWN dithers them in SCREEN space, so from a light they store as near-solid
// and hair paints a blob across the face.
static bool     g_localLightAlphaCasters  = false;
// How much nearer, in WORLD UNITS, a caster must be before it shadows. This is
// the "no self-shadowing" control: a character is ~0.3 units thick, so 0.3+
// stops it shadowing itself entirely while a shadow cast onto the floor (metres
// of separation) is untouched.
static float    g_localLightMinSep        = 0.30f;  // world units; preserves contact
// Emitter lights (flames, glowing water, spell effects) do not cast shadows.
// They are numerous and short-lived, and each one in the shadow set costs a
// full depth capture per frame. They still light the scene.
// "Lights casting shadows": do EMITTER lights (flames, torches, glowing water)
// compete for the caster slots, or do they only light and lift?
//
// ON is the current behaviour -- the caster cap bounds the cost, so an emitter
// is safe to include, and the player's carried torch IS an emitter, which is
// the one shadow players notice.
//
// OFF is what the build did before casting existed: emitters still light the
// scene and still lift the sun's shadow, they simply never take a slot. That
// keeps every slot for fixed lamps in an area where flames are everywhere.
static bool     g_localLightEmittersCast  = true;
// Does every slot need its own static warm-up, or does one per frame suffice?
// Latched by measurement, never assumed: if a later slot draws no dynamic
// geometry with one warm-up, this turns on and stays on.
// Brightness below which a pixel counts as unlit. The lift ramps from here to
// full white, so it follows the engine's own lit pools instead of a guessed
// distance falloff. NWN_SHADOWMAP_LIFT_THRESHOLD.
static float    g_liftThreshold          = 0.85f;   // self-illum guard: the engine-curve path
// NWN's own attenuation constants, read from its shaders at runtime. The
// defaults only cover the frame or two before the scan finds them.
static float    g_lightMaxIntensityInv   = 0.1f;
static float    g_lightFalloffFactor     = 1.0f;
// EVERY light in the census, for the sun-shadow lift. The shadow cube can only
// afford one light; the lift is a distance term with no depth map behind it, so
// it has no reason to be limited that way -- and a wall lamp lighting a wall
// must stop the sun's shadow darkening that wall just as a torch does.
//
// Peak-held PER LIGHT (keyed by the engine's pointer), because NWN animates a
// torch's radius as flicker -- one was measured swinging 0.09 <-> 13.30, and an
// unheld radius makes the lift pulse with the flame.
// NWN's own "Lighting Max Lights" setting goes to 128, so 8 was a number of
// mine, not the engine's: with 32 lights enabled it dropped three quarters of
// them (and once pushed the player's own torch out of the list entirely).
// The shader array is sized for the engine's maximum; how many are actually
// uploaded is a runtime cost/quality control.
static constexpr unsigned kMaxLampLights = 128;   // shader array size
static constexpr unsigned kLampPool      = 128;   // collected per frame
// How many lamps feed the cheap sun-shadow lift. NOT ours to choose: it is
// NWN's own "Lighting Max Lights" video setting, read live from
// LightManager::m_nMaxLights. The injector used to ship a duplicate control
// ("Light supported"), which could only ever disagree with the engine.
// NWN_SHADOWMAP_MAX_LAMPS still overrides it for A/B; 0 means follow the game.
// "Lights lift sun shadow" (panel, shipping too). NWN's sun shadow is a flat
// translucent black over every sun-shadowed pixel and knows nothing about
// torches, so walking under an awning with a torch dims the torchlight, the
// character and the floor together. The lift brightens sun-shadowed ground near
// a lamp to compensate.
//
// It is a TASTE call, not a correctness one -- some areas read better with the
// sun shadow left flat -- so it is switchable, and switchable in the builds
// users actually run rather than only in dev.
static bool g_lampLiftEnabled = true;
static int g_lampUploadMax = 0;   // 0 = follow the engine
struct LampEntry { const void* key = nullptr; float pos[3] = {}; float radius = 0.0f;
                   bool emitter = false; };
// DOUBLE-BUFFERED. The list is refilled by SetLightGL as the engine draws, so
// anything reading it mid-frame sees however much has arrived so far -- the
// shadow-light picker read it early and got 0 while the lift, which reads late,
// saw 11. Build into one array, publish the COMPLETED list at the frame
// boundary, and every reader gets a whole list.
static LampEntry g_lampBuild[kLampPool];
static unsigned  g_lampBuildCount = 0;
static LampEntry g_lampLights[kLampPool];
static unsigned  g_lampLightCount = 0;

static void lamp_list_begin() {
    // Publish what was collected, then start a fresh build -- but ONLY if
    // anything was collected.
    //
    // Scene::Render runs SEVERAL times per frame (world, sky, UI -- the trace
    // logged callers #1..#4). An unconditional publish therefore fired more
    // often than the list was filled: the world pass built the list, the next
    // Scene::Render published it and reset, and the pass after that published
    // an EMPTY build over the top. The census read 0 and the sun-shadow lift
    // lost every light.
    if (g_lampBuildCount == 0) return;
    for (unsigned i = 0; i < g_lampBuildCount; ++i) g_lampLights[i] = g_lampBuild[i];
    g_lampLightCount = g_lampBuildCount;
    g_lampBuildCount = 0;

    // CENSUS DUMP -- diagnosing the Windows lift bug (sun shadows vanish
    // everywhere when a torch is drawn). The shader clamps lampLit to 1 for any
    // pixel INSIDE a lamp and rejects only radius <= 0, so ONE oversized entry
    // lifts the sun's shadow across the whole screen. Prime suspect: the sun
    // itself arriving through SetLightGL with a directional light's radius.
    //
    // Prints only when the set changes, so it cannot spam. Enabled by default
    // on BOTH platforms until this is settled -- it is a handful of lines once
    // per change, and the bug only reproduces on Windows.
    {
        static unsigned lastCount = 0xFFFFFFFFu;
        static float    lastMaxR  = -1.0f;
        float maxR = 0.0f;
        for (unsigned i = 0; i < g_lampLightCount; ++i)
            if (g_lampLights[i].radius > maxR) maxR = g_lampLights[i].radius;
        if (g_lampLightCount != lastCount ||
            std::fabs(maxR - lastMaxR) > 0.5f) {
            lastCount = g_lampLightCount;
            lastMaxR  = maxR;
            fprintf(stderr, "[shadowmap][census] %u lamp(s), largest radius %.2f\n",
                    g_lampLightCount, maxR);
            const unsigned show = g_lampLightCount < 12 ? g_lampLightCount : 12;
            for (unsigned i = 0; i < show; ++i) {
                const LampEntry& e = g_lampLights[i];
                fprintf(stderr, "[shadowmap][census]   [%2u] r=%9.2f pos=(%8.2f,%8.2f,%8.2f)"
                                " emitter=%d key=%p%s\n",
                        i, e.radius, e.pos[0], e.pos[1], e.pos[2],
                        e.emitter ? 1 : 0, e.key,
                        e.radius > 200.0f ? "   <== SUSPICIOUS, lifts everywhere" : "");
            }
        }
    }
}

static void lamp_list_add(const void* key, float px, float py, float pz, float radius,
                          bool emitter) {
    if (radius <= 0.0f) return;
    // Peak-hold against this light's own previous radius, matched by pointer so
    // the hold follows the LIGHT and not a list position that can reorder.
    static LampEntry held[kMaxLampLights];
    LampEntry* slot = nullptr;
    for (auto& h : held) if (h.key == key) { slot = &h; break; }
    if (!slot) for (auto& h : held) if (!h.key) { slot = &h; break; }
    if (!slot) slot = &held[0];
    if (slot->key != key) { slot->key = key; slot->radius = 0.0f; }
    slot->radius = std::max(radius, slot->radius * 0.97f);

    for (unsigned i = 0; i < g_lampBuildCount; ++i)
        if (g_lampBuild[i].key == key) {           // already have it this frame
            g_lampBuild[i].radius = slot->radius;
            return;
        }
    if (g_lampBuildCount >= kLampPool) return;
    LampEntry& e = g_lampBuild[g_lampBuildCount++];
    e.key = key;
    e.emitter = emitter;
    e.pos[0] = px; e.pos[1] = py; e.pos[2] = pz;
    e.radius = slot->radius;
}// NWN_SHADOWMAP_LOCAL_LIGHT_BIAS
static int      g_receiverDebug          = 0;      // NWN_SHADOWMAP_RECEIVER_DEBUG (see shader)
// Direction and cone width of the single local-light face. Default: straight
// down in NWN's Z-up world (see the aim comment in capture_local_light_shadow
// for why aiming at the camera measured as zero coverage everywhere).
// NWN_SHADOWMAP_LOCAL_LIGHT_DIR="x,y,z" / NWN_SHADOWMAP_LOCAL_LIGHT_FOV=deg.
static float    g_localLightDir[3]       = { 0.0f, 0.0f, -1.0f };
static float    g_localLightFovDeg       = 150.0f;
// VIRTUAL LIGHT HEIGHT for the local shadow projection, in world units, 0 = off.
//
// A torch on a post is genuinely low, so a physically correct shadow map rakes
// the caster's shadow a long way across the floor. The base game does not do
// that -- it fakes a higher light and gets a shorter, steeper shadow. This
// raises the point the shadow is CAST FROM without moving the light itself.
//
// Deliberately applied to the projection ONLY. g_localLightSlotPos keeps the
// real position, so attenuation, radius culling and the falloff curve stay
// physical; just the geometry of the shadow changes. The one knock-on is that
// the receiver derives its bias texel size from the distance to the REAL light,
// so a very large lift makes that estimate slightly optimistic -- re-check
// slope bias if shadows start to shimmer after a big change here.
static float    g_localLightHeight       = 3.0f;
// NWN_SHADOWMAP_OVERLAY_LEGACY=1 restores the Phase 5b bitmap-font overlay
// (kept only as an A/B reference; it toggles but has never been visible).
static bool     g_overlayLegacy          = false;
// Local-light shadow resolution, its own quality setting rather than a number
// baked in here. One layer PER LIGHT, so the cost is size^2 * 4 bytes * lights
// -- the panel prints the total, because at 4096 with 32 lights that is 2 GB
// and the decision belongs to whoever owns the card.
//
// Starts at LOW (1024) -- the BOTTOM OF THE LADDER, which is the point.
//
// This used to default to 256, and 256 was later dropped from the panel's
// ladder ("never good enough to pick"). Nothing moved the default with it, so a
// fresh .ini was written with a resolution the UI can no longer even select:
// the combo's match loop found no entry, fell through to index 0, and DISPLAYED
// "Low (1024)" while the light maps were really being captured at 256. On
// Windows, where this control is hidden entirely, a fresh install simply ran at
// 256 forever with no way to discover it.
//
// NWN_SHADOWMAP_LOCAL_LIGHT_SIZE still overrides.
static int      g_localLightCaptureSize  = 1024;
static int      g_pendingLocalSize       = 1024;
static bool     g_localSizeCommit        = false;   // set by Apply
static long     g_localLightDumpAt       = -1;     // NWN_SHADOWMAP_LOCAL_LIGHT_DUMP
static double   g_localLightDumpDeadline = 0.0;
static unsigned g_traceFramesMax = 90;
static unsigned g_traceEventsMax = 4096;
static unsigned g_traceAreaFrame = 0;
static unsigned g_traceEvents    = 0;
static unsigned g_traceSequence  = 0;
// True only between the entry and exit of a selected area Scene::Render.
// Phase 3i needs this live after the bounded trace log has finished, while the
// second flag limits diagnostics to the requested number of records.
static bool     g_traceFrameActive = false;
static bool     g_traceFrameLogged = false;
// Some compositor/HiDPI combinations render the world into a half-resolution
// FBO after a full-size UI/menu pass. The normal 90%-of-largest-viewport area
// gate then never chooses the world scene, so no otherwise-valid diagnostic can
// start. Keep the looser threshold opt-in and diagnostic-only.
static bool     g_traceRelaxAreaViewport = false;

// Phase 4b: inventory the engine-owned caster submission lists immediately
// after its BSP/camera-frustum build.  This is read-only: it does not change a
// bucket, a draw, a matrix, or any GL state.  It exists to turn the Phase 3r
// "caster vanishes while orbiting" observation into a per-frame engine fact
// before attempting a non-camera-frustum caster route.
static bool     g_casterCullTrace = false;
static unsigned g_casterCullTraceMax = 64;
static unsigned g_casterCullTraceReports = 0;
static int      g_casterCullLastMesh = -1;
static int      g_casterCullLastStatic = -1;
static int      g_casterCullLastCulledPart = -1;
static int      g_casterCullLastCulledShadow = -1;
static int      g_casterCullLastBackShadow = -1;

// Phase 4c: compare the camera-frustum-filtered caster list above to the
// complete static BSP. `BSPTraverse` is the engine's plain recursive walker
// (no camera, planes or renderer state). ManageSceneBSP's callback does NOT
// hand its BSPNode directly to ProcessTriMeshParts: it first loads nodedata*
// from BSPNode +0x70, then ProcessTriMeshParts consumes that nodedata's
// PartTriMesh pointer/count pair at +0x20/+0x28. This callback follows that
// read-only path; it does not retain pointers, call an engine mutator, touch a
// bucket, or issue GL work.
static bool     g_casterFullBspTrace = false;
static unsigned g_casterFullBspTraceMax = 24;
static unsigned g_casterFullBspTraceReports = 0;
static uint64_t g_casterFullBspLastCandidates = UINT64_MAX;

// Phase 4d: feed the complete static BSP candidate set back through NWN's
// own material-bucket builder, then duplicate that native submission into the
// already-proven private static cascade target.  This is intentionally
// opt-in and static-only: the first question is whether a full, world-static
// caster source reaches the existing red diagnostic without camera-frustum
// loss.  Dynamic entities keep their established separate route.
static bool     g_casterFullBspNativeSubmit = false;
static unsigned g_casterFullBspNativeReports = 0;
static unsigned g_casterFullBspNativeLastFrame = 0;

// Phase 3a: pure cascade setup validation.  This is intentionally separate
// from NWN_SHADOWMAP_CASCADE_TARGETS: it derives the four stable light matrices
// from the frozen area-camera context but does not allocate, bind, or render to
// a target.  A bad fit therefore cannot regress menus, materials, or the
// known-good diagnostic.
static bool     g_cascadeMathTrace = false;
static float    g_cascadeLambda = 0.70f; // practical split: 0=uniform, 1=log
static int      g_cascadeMathResolution = 2048;
static unsigned g_cascadeMathLogFrames = 12;
static unsigned g_cascadeMathLogCount = 0;
// Phase 3c: allocate and validate the four-layer static/dynamic targets from
// the trace path.  It deliberately performs no clear, draw, copy, or receiver
// work; it exists to prove the PS4 target layout is accepted by this driver
// without re-entering the old red/green diagnostic path.
static bool     g_cascadeTargetValidate = false;
static bool     g_cascadeTargetValidationLogged = false;
// Phase 3e: census the original normal-pass geometry while the engine is
// already drawing it.  This is trace-only; it collects a compact per-bucket
// summary and never issues, suppresses, or redirects a draw.
static bool     g_cascadeGeometryTrace = false;
static bool     g_traceGeometryActive = false;
static int      g_traceGeometryBucket = -1;
static unsigned g_traceGeometryBuckets = 0;
static unsigned g_traceGeometryMaxBuckets = 32;
static unsigned g_traceGeometryDraws = 0;
static unsigned long long g_traceGeometryElements = 0;
static unsigned long long g_traceGeometryArrays = 0;
static uint32_t g_traceGeometryPrograms[16] = {};
static unsigned g_traceGeometryProgramCount = 0;
// Phase 3f: duplicate only the already-observed native draw calls from one
// selected bucket into private cascade layer 0, using the engine's unchanged
// normal camera state. This is a camera-depth capture, not a light pass and
// cannot affect the displayed frame; its one PGM proves safe in-sequence
// duplication before any light-space transforms are introduced.
static bool     g_cascadeCameraCapture = false;
static int      g_cascadeCameraCaptureBucket = 0;
static unsigned g_cascadeCameraCaptureFrame = 0;
static unsigned g_cascadeCameraCaptureDraws = 0;
static bool     g_cascadeCameraCaptureDumped = false;
static bool     g_cascadeCameraCaptureActive = false;
// Phase 3h: repeat the same narrow native capture, but substitute a private
// cascade-0 light transform derived from the frozen normal camera matrices.
// This remains depth-only and one-shot: it writes a PGM for inspection and
// never exposes a receiver or changes the displayed framebuffer.
static bool     g_cascadeLightCapture = false;
// The one-shot .pgm proofs of the cascade captures. Platform-split on purpose:
// Linux is the DEVELOPMENT build and they are the cheapest evidence there is
// when a capture looks wrong, while Windows is the SHIPPING build and has no
// business dropping files next to someone's nwmain.exe.
// NWN_SHADOWMAP_DUMP_PGM=1/0 overrides either way.
#if NWN_SHIP
static bool     g_dumpCapturePgm      = false;
#else
static bool     g_dumpCapturePgm      = true;
#endif
static int      g_cascadeLightCaptureBucket = 0;
static std::vector<int> g_cascadeLightCaptureBuckets;
static int      g_cascadeLightCaptureLayer = 0;
// Phase 4a: depth-only fan-out. This remains off for the accepted Phase 3r
// receiver; when enabled, each accepted native draw is replayed once per
// private array layer and no visible receiver is drawn.
static bool     g_cascadeMultiLayerCapture = false;
// Phase 6c: opt-in performance path (NWN_SHADOWMAP_CSM_BUCKET_REPLAY). The
// accepted fan-out above duplicates every individual draw call once per
// cascade layer (DUPLICATE_CASCADE_LIGHT), which rebinds the cascade FBO,
// re-attaches its depth layer and re-queries 6 pieces of GL state on EVERY
// (draw, layer) pair -- for a few hundred casters and 4 layers that is
// thousands of driver calls per frame, most of them redundant, and it
// measured as the dominant cost on a busy small area (dropping cascade
// resolution 2048->1024 and disabling PCF changed nothing, which rules out
// GPU/fill-rate and points at CPU/driver-call overhead). This path instead
// binds each cascade layer's FBO ONCE per bucket and replays the whole
// bucket into it (guarded_render_bucket(), the same mechanism already
// proven by render_from_light() and the local-light probe), while keeping
// the EXACT SAME per-object "recover model via the frozen view inverse,
// recombine with the light's view/proj" maths -- just applied inline inside
// glUniformMatrix4fv during the replay instead of via a separate duplicate
// draw. Default OFF; the draw-level path above remains the accepted
// baseline until this is play-tested for identical shadows at better fps.
static bool     g_cascadeBucketReplay    = false;  // NWN_SHADOWMAP_CSM_BUCKET_REPLAY
// PERFORMANCE. The bucket replay draws the ENTIRE caster set into every
// cascade layer, so cost is (casters x layers) per frame -- on a large area
// that measured ~7,200 static draws per layer, i.e. ~29,000 extra draw calls
// at four layers. These two knobs attack that directly:
//   CASCADES  - fewer layers is a LINEAR saving (2 layers = half the replay).
//   DISTANCE  - caps how far shadows are fitted. It does not by itself remove
//               draws (the replay is not per-cascade culled), but it packs the
//               same layers into a shorter range, so quality rises sharply and
//               a lower cascade count stops being noticeable.
// Inactive layers keep the last active layer's clipFar, which makes the
// receiver's existing "z > clipFar3 -> discard" test double as the distance
// cutoff with no shader change.
static int      g_cascadeActiveCount     = 3;  // NWN_SHADOWMAP_CSM_CASCADES (3 beat 4 in game)
static float    g_cascadeMaxDistance     = 0.0f;   // 0 = camera far plane. Env-only knob now:
                                                   // capping it never helped performance, because
                                                   // the replay is not culled per cascade.
// Dynamic casters are the only per-frame geometry left once the static map is
// world-anchored, and they are replayed into EVERY cascade layer. In a busy
// outdoor area that measured 3,820 draws per frame. Limiting them to the
// nearest N layers is close to free visually -- a creature 300 units away
// casts a shadow nobody can resolve -- and scales the cost linearly.
// 0 disables dynamic shadow casters entirely (static-only shadows), which is
// the cheapest useful configuration.
static int      g_cascadeDynamicLayers   = 3;   // NWN_SHADOWMAP_CSM_DYNAMIC_CASCADES
// Master A/B for the fullscreen receiver pass. Everything else (captures,
// replays, world map) keeps running; only the compositing pass is skipped.
// This is the one measurement that cleanly separates "cost is the receiver
// pass" from "cost is the caster side", live in one session.
static bool     g_receiverEnabled        = true;
// FOG. The shadow composite runs after the engine has already fogged the scene,
// so without this a shadow stays fully dark on geometry that fog has washed
// out -- very visible on a snowy/foggy exterior. We fade the shadow with the
// same curve. The engine's own fog state is read from GL when it is set
// (compatibility profile, so fixed-function fog state is readable); otherwise
// these manual values are used.
static bool     g_fogFade                = true;   // NWN_SHADOWMAP_FOG_FADE=0 disables
static float    g_fogStart               = 0.0f;   // world units; 0 with g_fogEnd 0 = auto
static float    g_fogEnd                 = 0.0f;
static float    g_fogStartLive           = 0.0f;   // what the shader is actually using
static float    g_fogEndLive             = 0.0f;
static bool     g_fogFromEngine          = false;
static float    g_fogStartEngine         = 0.0f;
static float    g_fogEndEngine           = 0.0f;
static bool     g_haveEngineFog          = false;
// THE dominant cost, measured 2026-08-10. The full-BSP submission injects every
// static part in the area into NWN's OWN mesh buckets with camera culling
// bypassed, so the ENGINE draws ~24,000 objects per frame instead of ~2,000.
// With the receiver disabled our own passes totalled 1.64 ms, so this is the
// whole 200 -> 28 fps drop. It exists so casters outside the camera frustum
// still reach the shadow map; the world-anchored static map only needs it on
// the frames that rebuild it.
static bool     g_fullBspEnabled         = true;   // NWN_SHADOWMAP_FULL_BSP_SUBMIT=0
// One-shot handshake between the world-map rebuild and the full-BSP submission.
// The previous attempt had each site independently ask "does the map need a
// refresh?" and skip accordingly; they disagreed (they run at different points
// in the frame with different contexts) so the submission effectively never got
// skipped and the engine ate ~24k draws every frame.
// Now it is explicit: the map REQUESTS the complete caster set, the submission
// grants it for exactly one frame, and the map rebuilds only on a frame where
// the set was actually present. Starts true so the first build is complete.
// HYBRID near-field: how many cascade layers also receive camera-fitted static
// casters. The world map alone forces one texel density for the whole area --
// crisp enough for close-up detail means it cannot cover the area, and wide
// enough to cover it loses small objects entirely (measured: extent 32 crisp
// but tiny, 256 and small objects lost their shadows). Cascades handle the near
// field at high density; the world map handles distance and off-screen casters.
// The two combine with min(), so either can shadow a pixel.
static int      g_staticNearCascades     = 4;   // NWN_SHADOWMAP_STATIC_NEAR_CASCADES ("Ultra").
                                                // Clamped to the live cascade count, so Ultra
                                                // means "crisp static in every cascade".
static bool     g_fullBspWanted          = true;
static bool     g_fullBspSubmittedFrame  = false;

// FRAME COST INSTRUMENTATION. Three rounds of optimisation guesses (bucket
// replay, cascade count, world-anchored static, dynamic layers) each helped a
// little and none of them fixed a 200 -> 27 fps drop; turning dynamic casters
// OFF ENTIRELY changed nothing. That disproves "caster draws are the cost", so
// measure where the time actually goes instead of optimising further.
// All CPU-side wall clock, which is what we can attribute; a GPU-bound cost
// will show up as time inside the receiver's own draw or as none of the below.
static double   g_msReceiver     = 0.0;   // fullscreen receiver pass
static double   g_msWorldMap     = 0.0;   // world-anchored static render
static double   g_msReplay       = 0.0;   // cascade bucket replays
static double   g_msSceneCopy    = 0.0;   // glCopyTexSubImage2D of scene depth
static unsigned g_frameDrawCalls = 0;     // draws the GAME issues per frame
// NWN's bucket indices run 0..8 with 5 unused; 16 is headroom, not a guess.
static constexpr int kBucketCount = 16;
static unsigned g_frameDrawCallsShown = 0;
static double   g_msReceiverShown = 0.0, g_msWorldMapShown = 0.0;
static double   g_msReplayShown = 0.0, g_msSceneCopyShown = 0.0;
static bool     g_fullBspSkipped = false; // is the engine back to culled draws?
// STATIC CASCADE CACHE. Static casters cannot move, so re-rendering them every
// frame is the single largest waste in the replay -- measured at 61,092 static
// draws per frame in a dense area (~15k casters x 4 layers). They only need
// redrawing when the CASCADE MATRICES change, because cached depth is only
// valid for the matrices it was rendered with.
// So the fit itself gets hysteresis: it is recomputed only when the camera
// moves/turns past a threshold (or the area or sun changes), and each refit
// bumps a generation counter. A static layer already holding the current
// generation is skipped entirely and simply re-stamped as fresh.
// The fitted extent is padded by the movement threshold so the view stays
// covered between refits.
static bool     g_cascadeStaticCache      = true;   // NWN_SHADOWMAP_CSM_STATIC_CACHE=0 disables
// The program the engine last bound. We hook glUseProgram on BOTH platforms,
// so this is authoritative and removes a per-uniform-upload driver query.
static unsigned g_curProgram              = 0;
// Per-frame call counts for the two highest-frequency wrappers. Added
// because a cost that survives every panel toggle has to be measured by
// FREQUENCY, not by which pass it belongs to.
static unsigned g_uniformMat4Calls        = 0;
static unsigned g_useProgramCalls         = 0;
static unsigned g_uniformMat4Shown        = 0;
static unsigned g_useProgramShown         = 0;
static unsigned g_engineShadowShown       = 0;
static bool     g_dynamicCastersEnabled   = true;   // "Moving casters" (panel)
static bool     g_staticCastersEnabled    = true;   // "Fixed casters" (panel)
// Panel: "Hide the game's own shadows". When set, Scene::RenderShadows is not
// called, so NWN's STENCIL shadows never draw. Confirmed in game 2026-08-12.
//
// It cannot remove the BLOB shadow, and that is a property of the engine, not a
// gap here: NWN's "Creature Shadow Detail = Off" does not mean "no shadow", it
// means "fall back to a blob", drawn somewhere other than this pass. So the
// working configuration is the counter-intuitive one -- leave the game's own
// shadows ON, set to BEST, and let this suppress them. Setting them Off in the
// game is the one choice that guarantees blobs.
//
// BEST, not Fast: Fast casts only from the player, Best covers creatures AND
// placeables. Since this discards whatever the pass produced, the widest
// setting is the correct one -- it decides how much gets removed, not how much
// gets drawn.
//
// Default ON: this module draws a cascaded shadow over the whole scene, so the
// engine's own shadow is a second, worse shadow under every object.
static bool     g_hideEngineShadows       = true;
static unsigned g_engineShadowDraws       = 0;
static float    g_cascadeCacheMove        = 2.0f;   // world units before a refit
// How far the sun direction may drift before the fit is thrown away. See the
// comment at the compare -- a bare 1e-3 meant a day/night cycle refitted every
// single frame and the static depth cache never hit once.
static constexpr float kCascadeSunHysteresis = 0.01f;   // ~0.6 degrees
static float    g_cascadeCacheTurn        = 0.995f; // cos of the allowed camera turn
static unsigned g_cascadeMathGeneration   = 0;
// Cache generation per (BUCKET, layer), not per layer. The static target is fed
// by TWO buckets -- 0 (opaque) and 1 (alpha-cutout) -- and stamping the layer
// from the first one made the second look already-cached, so alpha-cutout
// casters silently never entered the cascade. That is the "cache loses alpha
// meshes" bug. 16 buckets is the engine's range; layer is 0..kCascadeCount-1.
static unsigned g_cascadeStaticGeneration[16][4] = {};
static float    g_cascadeLastFitEye[3]      = {0,0,0};   // plain floats: Vec3f is declared later
static float    g_cascadeLastFitFwd[3]     = {0,0,0};
static float    g_cascadeLastFitSun[3]     = {0,0,0};
static void*    g_cascadeLastFitScene      = nullptr;
static bool     g_haveCascadeLastFit       = false;
static unsigned g_cascadeCacheHits         = 0;
static unsigned g_cascadeCacheRefits       = 0;
// Why the fit cache was rejected, split by cause -- see the comment at the
// rejection site. Reported in the [shadowmap][cost] line as deltas.
static unsigned g_cascadeRefitMove         = 0;
static unsigned g_cascadeRefitTurn         = 0;
static unsigned g_cascadeRefitSun          = 0;
static unsigned g_cascadeRefitScene        = 0;
static unsigned g_cascadeRefitDisabled     = 0;
// Per-frame replay shape, reset with the other frame accumulators.
static unsigned g_replayCallsFrame         = 0;
static unsigned g_replayDrawsFrame         = 0;
static unsigned g_replayCallsShown         = 0;
static unsigned g_replayDrawsShown         = 0;
// Plain int, not GLint: this block sits above the GL typedefs. They are the
// same type on every target here, and gl::GetIntegerv takes GLint* == int*.
static int      g_fbSamples                = -1;   // GL_SAMPLES of the live target
static int      g_fbViewport[4]            = {};
// Set for the whole of a frame that will print [cost], so every timed section
// can finish and report GPU time instead of submission time. One stall per 30
// main scenes; the alternative is numbers that move cost between each other.
static bool     g_costFinishThisFrame      = false;
static unsigned g_costFrames               = 0;
// The [cost]/[buckets] instrumentation is DEVELOPMENT ONLY. It is not free:
// reporting a truthful GPU number requires a glFinish, which stalls the
// pipeline on the frames it prints. That is the right trade on the development
// build and the wrong one in something a user installs, so it follows the same
// rule as the .pgm dumps -- on for Linux, off for Windows, overridable with
// NWN_SHADOWMAP_COST=1/0.
#if NWN_SHIP
static bool     g_costReport               = false;
#else
static bool     g_costReport               = true;
#endif

// WORLD-ANCHORED STATIC SHADOW MAP.
// The camera-fitted cascades must be refitted (and therefore re-rendered)
// whenever the camera moves, which is constantly. But NWN static geometry
// never moves and the area sun never changes, so the static shadow map does
// not need to follow the camera at all: anchor it to the WORLD and it can be
// rendered once per area and reused for every subsequent frame -- static
// caster cost per frame becomes zero rather than "zero until you walk 2m".
// Dynamic casters stay on the camera-fitted cascade array, where their much
// smaller draw count (hundreds, not tens of thousands) is not a problem.
// Resolution is the whole trade-off, so it is user-configurable: this map
// covers 2*extent world units, so texel size = 2*extent/size. At 4096 over a
// 512-unit box that is 12.5 cm; at 16384 it is 3.1 cm.
static bool     g_staticWorldEnabled   = true;    // always on: this is what makes static casters free
static int      g_staticWorldSize      = 8192;    // NWN_SHADOWMAP_STATIC_WORLD_SIZE (512..16384)
static float    g_staticWorldExtent    = 256.0f;  // NWN_SHADOWMAP_STATIC_WORLD_EXTENT (half-size)
static bool     g_staticWorldUsable    = false;
static bool     g_staticWorldValid     = false;
static bool     g_staticWorldDirty     = true;
static unsigned g_staticWorldRefreshSerial = 0;
static float    g_staticWorldVP[16]    = {};
static float    g_staticWorldCentre[3] = {};
static float    g_staticWorldSun[3]    = {};
// Earliest time a SUN change may trigger another world-map rebuild. Without it
// a moving sun rebuilds an area-sized map every frame.
static double   g_staticWorldSunRebuildAt = 0.0;
static void*    g_staticWorldScene     = nullptr;
// The AREA the static world map was built for. The Scene pointer alone is NOT
// an area identity -- the engine reuses it -- so this holds the area's own
// CAurObject as well. See static_world_needs_refresh().
static const void* g_staticWorldArea   = nullptr;
// The area object the last shadow toggle was called with. Declared HERE, above
// the .inc includes, because shadow_replay.inc reads it for the area-change
// test; it is written by the Aur toggle detours further down.
static const void* g_areaToggleObject = nullptr;
static unsigned g_staticWorldRenders   = 0;

// Panel-staged resolutions. Both own GL textures, so the overlay edits these
// and raises g_resolutionApplyRequested; apply_resolution_change() does the
// reallocation from the render path, where a context is current.
static int  g_pendingCascadeSize = 0;    // 0 until first read from g_size
static int  g_pendingWorldSize   = 0;
static bool g_resolutionApplyRequested = false;
// World extent is staged with the resolutions, not applied live: every change
// invalidates the map, and dragging a slider would rebuild it every frame.
static float g_pendingWorldExtent = 0.0f;
static bool     g_cascadeReplayActive    = false;  // true only inside one replay_bucket_into_cascade_layers() layer iteration
// True only inside the Phase 6b local-light depth pass. Same purpose as the
// flag above: that pass ALSO drives geometry through guarded_render_bucket(),
// so without it the re-entered bucket detour would treat those replays as
// ordinary engine draws and kick off a full 4-layer cascade replay for each
// -- 16 extra whole-bucket renders per frame, which is exactly how the
// local-light probe launcher ended up as slow as the pre-optimisation path.
static bool     g_localLightPassActive   = false;
// REAL draw counts for the local-light capture, per bucket. The existing
// g_localLightCaptureDraws counts BUCKETS THAT RETURNED TRUE (max 4), which is
// not evidence that anything was rendered -- it reported "draws=4" for a map
// whose centre texel is the floor, i.e. the character never made it in.
static unsigned g_localLightBucketDraws[4] = {};
static int      g_localLightCurrentBucket  = -1;
// True only while the ImGui overlay is issuing its own draws. It needs the
// SAME short-circuit as the passes above, and specifically must NOT reuse
// g_inOurPass: that flag makes shadow_before_draw() colour-mask out every
// program it does not recognise as an injected caster, which would silently
// mute the entire panel -- the exact failure mode this ImGui port exists to
// get away from.
static bool     g_overlayPassActive      = false;
static unsigned g_cascadeReplayDrawCount = 0;      // draws observed during the CURRENT layer iteration
// The light transform for a bucket replay goes through the engine's matrix
// stack, whose accessor is defined much further down beside the other matrix
// helpers; forward-declare it rather than move that block.
static float* mtx_entry(int idx);
// Phase 4f: the first visible CSM receiver. It consumes the same per-layer
// depth images as Phase 4a, but selects one static layer per receiver pixel
// from the immutable entry camera view depth. It remains red-only, static-only
// and uses hard split boundaries: no blending or PCF tuning.  Phase 4g may
// additionally consume the separately-proven bucket-2 dynamic depth array.
// Phase 4h extends that same fresh dynamic target with the separately proven
// alpha/card bucket 3; the original alpha-discard shader state is preserved.
static bool     g_cascadeCsmStaticReceiver = false;
static bool     g_cascadeCsmDynamicReceiver = false;
static bool     g_cascadeCsmAlphaReceiver = false;
// Phase 5a: the same receiver may draw a conventional translucent-black
// shadow overlay instead of the red proof colour.  It is opt-in so the red
// result remains an exact regression/reference mode.
// Same story as the two above. The env read below used to be unconditional
// (`= getenv(...) != nullptr`), so absence forced this OFF and the declared
// value never mattered; it is now a real default that the env can override.
static bool     g_cascadeCompositeShadows = true;
static float    g_cascadeCompositeStrength = 0.42f;
// Read-only area policy.  This deliberately follows NWN's existing area
// properties and never changes the engine's sun, the CSM fit, or cached depth.
// This is intentionally limited to the area's existing directional policy.
static bool     g_areaShadowFlagsEnabled  = true;
static bool     g_areaShadowOpacityApply  = true;
static bool     g_areaShadowFlagsApply    = true;
static bool     g_haveAreaShadowFlags     = false;
static bool     g_areaSunShadows          = true;
static bool     g_areaMoonShadows         = true;
static bool     g_areaIsNight             = false;
static float    g_areaShadowOpacity       = 1.0f;
static void*    g_areaShadowSource        = nullptr;
static double now_seconds();
// Composite-only policy fade.  It never invalidates or blends depth maps, so
// sunset/sunrise can be pleasant without reviving dynamic-area-light rebuilds.
static float    g_areaShadowFadeSeconds   = 0.75f;
static float    g_areaShadowFadeFrom      = 1.0f;
static float    g_areaShadowFadeTo        = 1.0f;
static double   g_areaShadowFadeStart     = 0.0;
static float effective_area_sun_strength() {
    if (!g_areaShadowFlagsEnabled || !g_haveAreaShadowFlags)
        return g_cascadeCompositeStrength;
    const float strength = g_areaShadowOpacityApply
        ? g_cascadeCompositeStrength * g_areaShadowOpacity
        : g_cascadeCompositeStrength;
    // The flag chooses the TARGET visibility; do not zero strength here or a
    // fade-out would vanish immediately on the same update that starts it.
    const bool allowed = !g_areaShadowFlagsApply ||
        (g_areaIsNight ? g_areaMoonShadows : g_areaSunShadows);
    if (g_areaShadowFadeSeconds <= 0.0f)
        return allowed ? strength : 0.0f;
    const double elapsed = now_seconds() - g_areaShadowFadeStart;
    const float t = (float)std::max(0.0, std::min(1.0,
        elapsed / (double)g_areaShadowFadeSeconds));
    return strength * (g_areaShadowFadeFrom +
                       (g_areaShadowFadeTo - g_areaShadowFadeFrom) * t);
}
static float    g_cascadeReceiverBias = 0.0025f;
// SHIPPED DEFAULTS, not phase gates. These were 0/0 -- the opt-in values from
// when cross-fade and PCF were being trialled -- while run-dev.sh passed 0.75
// for both and "Restore defaults" set 0.75. So Linux development never ran the
// compiled value, and a fresh install with no .ini did: hard cascade boundaries
// and unfiltered, aliased shadow edges. Worst on Windows, where these two
// sliders live behind "#if !NWN_SHIP" and cannot be reached to correct it.
static float    g_cascadeBlendWidth = 0.75f;
static float    g_cascadePcfRadius = 0.75f;

// Phase 5b: injector-owned ReShade-style settings overlay.  It is deliberately
// keyboard-first until SDL event consumption is mapped safely; Ctrl+Shift avoids
// colliding with ordinary NWN movement/camera bindings.
static bool     g_shadowOverlayVisible = false;
static bool     g_shadowOverlayInputLogged = false;
static bool     g_shadowOverlayToggleLatch = false;
static int      g_shadowOverlaySelection = 0;
static unsigned char g_shadowOverlayPreviousKeys[512] = {};
static unsigned int g_shadowOverlayProgram = 0;
static unsigned int g_shadowOverlayFontTex = 0;
static int      g_cascadeReplayLayer = 0;
static bool     g_cascadeLightCaptureAllBuckets = false;
static unsigned g_cascadeLightCaptureFrame = 0;
static unsigned g_cascadeLightCaptureDraws = 0;
// The old global counters describe one target only.  Once static bucket 0 and
// dynamic bucket 2 share an area frame they must be cleared and validated
// independently, or whichever bucket arrives second inherits stale depth.
static unsigned g_cascadeStaticCaptureFrame = 0;
static unsigned g_cascadeStaticCaptureDraws = 0;
static unsigned g_cascadeDynamicCaptureFrame = 0;
static unsigned g_cascadeDynamicCaptureDraws = 0;
static unsigned g_cascadeStaticCaptureFrameLayer[4] = {};
static unsigned g_cascadeStaticCaptureDrawsLayer[4] = {};
static unsigned g_cascadeDynamicCaptureFrameLayer[4] = {};
static unsigned g_cascadeDynamicCaptureDrawsLayer[4] = {};
// Phase 4h diagnostics: the shared dynamic target is valid with only the
// character body, but these separate counts prove whether bucket 3 actually
// contributed alpha/card depth in the current layer/frame.
static unsigned g_cascadeDynamicBodyCaptureDrawsLayer[4] = {};
static unsigned g_cascadeDynamicAlphaCaptureDrawsLayer[4] = {};
static unsigned g_cascadeLightCaptureSkipped = 0;
static unsigned g_cascadeLightCaptureAttempts = 0;
static unsigned g_cascadeLightCaptureNotReady = 0;
static unsigned g_cascadeLightCaptureBucketsVisited = 0;
static unsigned g_cascadeLightCaptureDynamicScopes = 0;
static bool     g_cascadeLightCaptureDumped = false;
static bool     g_cascadeLightCaptureActive = false;
// Phase 3i is intentionally narrower than the old red/green experiments:
// only the already-proven opaque bucket is captured, into the static array,
// every selected-area frame.  A private fullscreen pass will consume the same
// frozen frame context after NWN has completed that frame.  Dynamic geometry
// and alpha-tested casters stay out of this proof.
static bool     g_cascadeStaticReceiver = false;
// Phase 3l: compose the separately validated post-dynamic bucket into the
// static receiver.  It remains opt-in and only exists with the static receiver;
// neither target is allowed to share clears, counters, or a camera frame.
static bool     g_cascadeDynamicReceiver = false;
// Phase 3j: isolate the engine's explicit dynamic-geometry stage into the
// private dynamic cascade array.  This is depth-only and intentionally has no
// receiver; it answers where animated characters are submitted before the
// proven static receiver is expanded.
static bool     g_cascadeDynamicCharacterCapture = false;
static bool     g_cascadeDynamicTargetActive = false;
static bool     g_cascadeDynamicAlphaTargetActive = false;
static bool     g_cascadeDynamicCharacterReported = false;
// The named dynamic routine queues work but makes no native draw in this
// Linux build.  The first post-queue bucket with live geometry is therefore a
// separately proven candidate, kept in the dynamic array until its PGM shows
// the player.
static bool     g_cascadeDynamicBucketCapture = false;
static int      g_cascadeDynamicBucket = 2;
// Phase 3p: the independently proven second dynamic hair pass.  It shares the
// already isolated dynamic target with bucket 2, never the static target.  The
// receiver remains opt-in until the resulting anchored diagnostic is checked.
static bool     g_cascadeAlphaReceiver = false;
static int      g_cascadeAlphaBucket = 3;
// Phase 3q: depth-only proof for stock static alpha-discard foliage. The
// source signature is used instead of a process-local program ID.
static bool     g_cascadeStaticAlphaCapture = false;
static int      g_cascadeStaticAlphaBucket = 1;
static std::vector<uint32_t> g_cascadeStaticAlphaFragments;
// Phase 3r: extend the static target with only source-classified stock foliage.
// The filter is armed solely while native bucket 1 is rendering, so it cannot
// accidentally filter opaque bucket 0 or the dynamic buckets.
static bool     g_cascadeStaticAlphaReceiver = false;
static bool     g_cascadeStaticAlphaFilterActive = false;
// Phase 3o: bucket 3 is now known to be the second custom-hair pass. The
// normal full-map PGM makes a few hundred card texels almost impossible to
// inspect, so this opt-in leaves capture untouched and emits a tightly cropped,
// nearest-neighbour enlarged view of the same private depth layer.
static bool     g_cascadeAlphaCardCapture = false;
// Phase 3g: read only the normal-pass 4x4 uniform uploads which precede the
// native draws.  This names the camera projection/view locations from evidence
// before any later duplicate draw attempts light-space substitution.
static bool     g_cascadeMatrixTrace = false;
static unsigned g_cascadeMatrixTraceCount = 0;
static unsigned g_cascadeMatrixTraceMax = 96;
struct NativeTransformSlot {
    uint32_t prog = 0;
    int model = -1, modelView = -1, modelViewProj = -1;
    int view = -1, proj = -1, viewInv = -1;
    int fogParams = -2;   // -2 = not looked up yet, -1 = this program has none
    float normalModelView[16] = {};
    float normalProjection[16] = {};
    // The matrix-stack view that was current when normalModelView was uploaded.
    // A dynamic draw's m_mv is camera-view * model, so this is the only safe
    // inverse to use when recovering model for a private point-light face.
    float normalViewInverse[16] = {};
    bool haveNormalModelView = false;
    bool haveNormalProjection = false;
    bool haveNormalViewInverse = false;
};
static NativeTransformSlot g_nativeTransformSlots[64];
static unsigned g_nativeTransformSlotCount = 0;
// Phase 3b: identify the real normal-pass directional-light upload.  It is
// trace-only and records a bounded set of vec3 uploads; it never rewrites one.
static bool     g_lightVectorTrace = false;
static unsigned g_lightVectorTraceCount = 0;
static unsigned g_lightVectorTraceMax = 80;
// Selected only after the same unit world vector reaches two distinct normal
// material programs.  The stored direction is light -> scene, ready for the
// cascade lookAt convention.
static bool     g_haveTraceAreaLight = false;
static float    g_traceAreaLightDir[3] = { 0.0f, 0.0f, -1.0f };
static float    g_traceLightCandidate[3] = { 0.0f, 0.0f, 0.0f };
static int      g_traceLightCandidateProgram = 0;
static unsigned g_traceLightCandidateHits = 0;

static bool   g_probeArmed  = false;
static double g_probeDelay  = 60.0;   // seconds before first attempt
static double g_probeEvery  = 5.0;    // seconds between retries
static int    g_probeTries  = 24;
static int    g_probeAttempt= 0;
static double g_nextAttempt = 0.0;

static double now_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

// --- Phase 3 config ---------------------------------------------------------
static bool  g_lightPass   = false;
static float g_lightDir[3] = { 0.0f, 0.0f, -1.0f };   // world, light -> scene
static int   g_conv        = 0;
static float g_extent      = 40.0f;
// Must stay well inside the camera's far plane (captured: near=0.1 far=45)
// whenever we keep the engine's perspective projection, or the whole scene
// falls outside the frustum and the depth target comes back empty.
static float g_dist        = 25.0f;
static bool  g_afterOrig   = true;    // VALIDATED: must run AFTER the engine's own
                                      // draw, or dynamic Gobs (the player!) lose
                                      // their per-frame model matrix and render
                                      // at the world origin
// Substituting the projection is OFF by default. SetOrthoTransform applies to
// whatever m_nCurrentMatrix happens to be, and SetViewTransform selects matrix
// mode 1 just before -- so the ortho lands on the wrong stack and corrupts the
// main render (observed: whole screen reduced to fog, UI included). Keeping the
// engine's perspective still gives a valid depth map from the light's position,
// which is all Phase 3 needs to prove.
static bool  g_useOrtho    = true;    // VALIDATED: ortho written directly into
                                      // entry 0; removes the 45-unit far plane
// View substitution is ALSO opt-in and separate from the light pass itself.
// With it off, the light pass renders the buckets into our depth target using
// the engine's own camera -- i.e. exactly the Phase 2 conditions that are known
// to work, but every frame. That isolates "is repeated bucket replay stable?"
// from "does substituting the view break the engine?". Do not merge these two
// questions again; every compound change so far has cost a crash to diagnose.
// 0 = off (engine camera), 1 = engine SetViewTransform, 2 = compose the view
// matrix ourselves. Mode 1 proved unusable: the captured "camPos" comes back as
// ~(0.8, 0.5, 0.15), i.e. roughly the player's feet, so SetViewTransform's
// Vector& is a look-at target (or a direction), NOT an eye position -- feeding
// it a point 25 units up just aimed the camera at nothing (0 texels rendered).
// Mode 2 sidesteps the unknown semantics entirely with
// SetMatrixMode / LoadMatrixIdentity / Rotate / Translate.
static int    g_viewSub     = 4;   // VALIDATED: direct matrix write
static int    g_viewMtxMode = 1;      // SetViewTransform is seen passing 1
// The Phase 2 probe waited 60s before touching anything. The light pass had no
// such delay and ran from the first frame, while the module was still loading
// and bucket contents were not necessarily valid. Same delay, same reason.
static double g_lightDelay  = 60.0;
static double g_lightStart  = 0.0;
// ALL buckets the Phase 2 probe found geometry in. The earlier restriction to
// {0,6,11} was a workaround for the player drawing at the world origin -- which
// was actually caused by running the light pass BEFORE Scene::Render, and is
// fixed by WHEN=after. Casters (trees, rocks, placeables) are likely in the
// small buckets, and a shadow map containing only terrain cannot shadow
// anything: exactly the "uniformly lit" symptom.
static std::vector<int> g_lightBuckets = { 0, 1, 2, 3, 6, 11, 13 };

#include "shadow_gl_api.inc"
#include "shadow_engine_bindings.inc"
// ===========================================================================
//  Captured renderer state (Phase 3)
//  We cannot construct a RenderInterface, and there is no global instance
//  symbol, so we take `this` from a call the engine makes every frame.
// ===========================================================================
struct Vec3f { float x, y, z, w; };   // 4th float unknown; preserved verbatim
struct Quat4f { float a, b, c, d; };

// ===========================================================================
//  Phase 6b: PartLight field decode (local-light shadow probe)
//  Offsets verified 2026-08-09 by disassembling FOUR independent engine call
//  sites that all agree on the same layout:
//    - PartOutside(PartLight*, List<Plane>&)  (0x47a1e0) reads the world
//      position at +0xac as the centre argument to SphereAbovePlane(), with
//      the radius at +0x70 as the sphere radius.
//    - GetLightAdjustedRadius(const PartLight*) (0x4c0c10) reads +0x70.
//    - GetLightAdjustedColor(const PartLight*) (0x4c0b60) and
//      PartLight::Mat()'s SetColor4f call (0x450510) both read the RGB
//      triplet at +0x64/+0x68/+0x6c.
//    - LightManager::GetNearestLights(Scene*, Vector, float, int) (0x4c15f0)
//      independently reads the SAME position (+0xac/+0xb0/+0xb4) and radius
//      (+0x70) while walking LightManager's light array by distance.
//    - SetLightGL(const PartLight*, int, float) (0x4c0e60) reads +0x80 as a
//      boolean branch that swaps which SetLightProperties(AMBIENT) call runs
//      (ambient-only lights contribute their colour as ambient, not diffuse).
//  This is disassembly cross-reference, not a guess, but it has not yet been
//  checked against a live game session -- treat the first real run's logged
//  values (compare a torch's logged position against its known placement) as
//  the actual proof before trusting it further.
// ===========================================================================
// The WINDOWS build's PartLight has 4 extra bytes ahead of these fields, so
// every offset shifts by one float. Measured by logging the same lights on both
// platforms in the same area:
//     Linux    pos=(8.2,-1.1,4.1)  colour=(1.00,0.89,0.67)  raw=0.1000
//     Windows  pos=(0.0, 8.2,-1.1) colour=(0.00,1.00,0.89)  raw=0.6700
// -- Windows reads each field one float early, i.e. its data sits 4 bytes
// later. Without this the sun-shadow lift dimmed the wrong places on Windows
// (garbage positions) while the radius looked right, because that comes from
// GetLightAdjustedRadius, an engine call that does not care about layout.
#ifdef _WIN32
static constexpr size_t kPartLightDelta = 4;
#else
static constexpr size_t kPartLightDelta = 0;
#endif
static constexpr size_t kPartLightColorOff   = 0x64 + kPartLightDelta;  // 3 floats: R,G,B
static constexpr size_t kPartLightRadiusOff  = 0x70 + kPartLightDelta;  // 1 float: world units
static constexpr size_t kPartLightAmbientOff = 0x80 + kPartLightDelta;
// LIGHT PRIORITY, NWN's own "lightpriority" (a torch is 3). Found by
// disassembling Gob::SetLightPriority(int): it does NOT store the value on the
// Gob -- it collects that Gob's PartLights and writes the int into each one at
// +0x8c. So it is a plain read from a pointer we already have, with no hook and
// no Gob-to-light mapping.
static constexpr size_t kPartLightPriorityOff = 0x8c + kPartLightDelta;  // 1 int32: nonzero = ambient-only
static constexpr size_t kPartLightPosOff     = 0xac + kPartLightDelta;  // 3 floats: world X,Y,Z
// The light's CURRENT fade level, 0..1. Found by delta-scanning the struct
// across a night->day transition: this offset ramped 0.1774 -> 1.0000 in even
// ~0.10 steps and then held exactly 1.0, while +0x70 (the radius) grew with it.
// That is the curve NWN's own stencil shadow follows -- the engine fades the
// light, and only when the fade completes does it drop out of GetShadowLights.
// We saw only the drop, which is why our shadow vanished in one frame while the
// base game's faded out.
//
// Holding exactly 1.0 at steady state is what makes scaling by it safe: outside
// a transition it multiplies by one and changes nothing.
static constexpr size_t kPartLightFadeOff    = 0x90 + kPartLightDelta;  // 1 float: 0..1

struct PartLightInfo {
    Vec3f pos = {0.0f, 0.0f, 0.0f, 1.0f};
    float radius = 0.0f;
    float color[3] = {0.0f, 0.0f, 0.0f};
    float fade = 1.0f;          // 0..1, see kPartLightFadeOff
    int   priority = 0;         // NWN lightpriority, see kPartLightPriorityOff
    bool  ambientOnly = false;
};

// Read-only: never writes to the engine's PartLight. Returns false if the
// decoded values look implausible -- guards against a bad pointer or offset
// drift on a different engine build rather than trusting raw memory blindly.
static bool read_part_light(const void* light, PartLightInfo& out) {
    if (!light) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(light);
    float pos[3] = {}, radius = 0.0f, color[3] = {};
    int32_t ambientOnly = 0;
    std::memcpy(pos,          p + kPartLightPosOff,     sizeof(pos));
    std::memcpy(&radius,      p + kPartLightRadiusOff,  sizeof(radius));
    std::memcpy(color,        p + kPartLightColorOff,   sizeof(color));
    std::memcpy(&ambientOnly, p + kPartLightAmbientOff, sizeof(ambientOnly));
    int32_t priority = 0;
    std::memcpy(&priority,    p + kPartLightPriorityOff, sizeof(priority));
    for (float v : pos)   if (!std::isfinite(v)) return false;
    for (float v : color) if (!std::isfinite(v)) return false;
    if (!std::isfinite(radius) || radius <= 0.0f || radius > 1000.0f) return false;
    out.pos = { pos[0], pos[1], pos[2], 1.0f };
    out.radius = radius;
    // Bounds-checked like everything else here: a plausible priority is small.
    // Out of range means the offset is wrong on this build, and 0 makes the
    // ordering below a no-op rather than nonsense.
    out.priority = (priority >= 0 && priority <= 64) ? (int)priority : 0;
    out.color[0] = color[0]; out.color[1] = color[1]; out.color[2] = color[2];
    {   // Fade, defaulting to fully on if the value is not a sane 0..1 -- a
        // wrong offset on some other build must not silently erase shadows.
        float fade = 1.0f;
        std::memcpy(&fade, p + kPartLightFadeOff, sizeof(fade));
        out.fade = (std::isfinite(fade) && fade >= 0.0f && fade <= 1.0f) ? fade : 1.0f;
    }
    out.ambientOnly = ambientOnly != 0;
    return true;
}

// The first plausible non-ambient candidate from NWN's current
// GetShadowLights() priority list, refreshed by the detour below.  This is the
// authoritative source for injected local shadow maps.
static void*         g_localLightSelected      = nullptr;
static PartLightInfo  g_localLightSelectedInfo;
// The engine's own shadow priority is distinct from the broader SetLightGL
// census.  In particular, NWN puts a carried torch here even while a room has
// many regular lamps.  Never replace this ordering with camera distance,
// visibility, render order, or inferred actor ownership.
static const void*    g_engineShadowLightKey = nullptr;
static bool           g_haveEngineShadowLight = false;
struct EngineShadowLightEntry { const void* key = nullptr; PartLightInfo info; };
static EngineShadowLightEntry g_engineShadowLights[kLocalSlotsMax] = {};
static unsigned       g_engineShadowLightCount = 0;

// Phase 2 remains rendering-neutral.  It freezes the exact normal-camera
// state that belongs to a selected area Scene before the engine enters its
// bucket sequence.  The legacy experiment mixed a light map made from one
// camera with receiver state captured later from UI/portrait/transition
// cameras; that is the direct shape of the pan/zoom leak.  Keep this record
// self-contained so a later shadow pass can consume one coherent frame rather
// than piecing globals together after they have been overwritten.
struct ShadowFrameContext {
    bool valid = false;
    unsigned serial = 0;
    void* scene = nullptr;
    void* camera = nullptr;
    GLint viewport[4] = {0, 0, 0, 0};
    int matrixIndex = -1;
    Vec3f eye = {0, 0, 0, 0};
    float projection[16] = {};
    float view[16] = {};
    float viewInverse[16] = {};
    float viewProjectionInverse[16] = {};
    uint32_t projectionHash = 0;
    uint32_t viewHash = 0;
};
static ShadowFrameContext g_shadowFrameContext;

struct CascadeMathState {
    bool valid = false;
    unsigned sourceFrame = 0;
    float clipFar[4] = {};
    float extent[4] = {};
    float texelWorld[4] = {};
    Vec3f centre[4] = {};
    float lightView[4][16] = {};
    float lightProjection[4][16] = {};
    float lightVP[4][16] = {};
};
static CascadeMathState g_cascadeMath;

static int     g_camLogged    = 0;
static Vec3f   g_worldEye      = {0,0,0,0};
static GLint   g_maxVpWidth    = 0;
static bool    g_isWorldScene  = false;
static bool    g_haveWorldEye  = false;
// Once a real area camera has appeared, its Scene object is a stronger
// discriminator than viewport dimensions. NWN also renders full-size UI and
// transition Scenes; allowing those to replace a fresh shadow map is the
// source of the red/green full-map blink while orbiting or zooming.
static void*   g_areaScene     = nullptr;
static void reset_engine_shadow_light_anchor();
// Scene currently executing its original Scene::Render.  A full-size UI or
// transition scene can have a valid-looking m_vp_inv, so viewport size alone
// is not sufficient when capturing the matrix for the selected area.
static void*   g_renderingScene = nullptr;
static Vec3f   g_camPos       = {0,0,0,0};
static Quat4f  g_camQuat      = {0,0,0,1};
static double  g_camPersp[4]  = {0,0,0,0};
static bool    g_haveCam      = false;
static bool    g_havePersp    = false;
static bool    g_inOurPass    = false;     // re-entrancy guard for our own calls
// The cross-MODULE generalisation of the flag above and of
// g_cascadeReplayActive / g_localLightPassActive / g_overlayPassActive: set
// while ANY injector module (this one, nwn_oit.cpp) is issuing its own draws.
// Declared in nwn_hooks_core.h; defined here because the shadow module is
// always linked. See that header for why the two modules share one .so.
bool nwn_core::g_ownedPass = false;
int  nwn_core::g_currentBucket = -1;
void (*nwn_core::g_drawObserver)() = nullptr;
void (*nwn_core::g_drawObserverAfter)() = nullptr;
static bool    g_viewInsideRender = false; // does the engine set it inside
                                           // Scene::Render()? (diagnostic)
static bool    g_inSceneRender    = false;
// Per-light-pass evidence for the colour caster route. These counters are
// deliberately draw-call based (no readback, no timing impact).
static unsigned g_lightDrawCalls = 0;
static unsigned g_lightCasterDrawCalls = 0;
static unsigned g_lightMutedDrawCalls = 0;
static unsigned g_lightProgramBinds = 0;
static unsigned g_lightCasterProgramBinds = 0;
static unsigned g_lightPreloadedCasters = 0;

#ifndef _WIN32
struct MainModule { uintptr_t base = 0; std::string path; };

static int phdr_cb(struct dl_phdr_info* info, size_t, void* data) {
    auto* m = static_cast<MainModule*>(data);
    if (info->dlpi_name == nullptr || info->dlpi_name[0] == '\0') {
        m->base = info->dlpi_addr;
        m->path = "/proc/self/exe";
        return 1;
    }
    return 0;
}

static bool resolve_symbols() {
    MainModule mod;
    dl_iterate_phdr(phdr_cb, &mod);
    if (mod.path.empty()) mod.path = "/proc/self/exe";

    int fd = open(mod.path.c_str(), O_RDONLY);
    if (fd < 0) { perror("[shadowmap] open exe"); return false; }
    struct stat st{};
    if (fstat(fd, &st) != 0) { close(fd); return false; }
    void* map = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) { perror("[shadowmap] mmap exe"); return false; }

    auto* base = static_cast<uint8_t*>(map);
    auto* eh   = reinterpret_cast<Elf64_Ehdr*>(base);
    auto* sh   = reinterpret_cast<Elf64_Shdr*>(base + eh->e_shoff);

    const Elf64_Shdr* symtab = nullptr;
    for (int i = 0; i < eh->e_shnum; ++i)
        if (sh[i].sh_type == SHT_SYMTAB) { symtab = &sh[i]; break; }
    if (!symtab) { fprintf(stderr, "[shadowmap] no .symtab (stripped?)\n");
                   munmap(map, st.st_size); return false; }

    auto* syms = reinterpret_cast<Elf64_Sym*>(base + symtab->sh_offset);
    auto* strs = reinterpret_cast<const char*>(base + sh[symtab->sh_link].sh_offset);
    size_t nsym = symtab->sh_size / sizeof(Elf64_Sym);

    for (size_t i = 0; i < nsym; ++i) {
        if (syms[i].st_value == 0) continue;
        const char* nm = strs + syms[i].st_name;
        for (auto& b : kBindings) {
            if (*b.slot) continue;
            if (std::strcmp(nm, b.mangled) == 0)
                *b.slot = reinterpret_cast<void*>(mod.base + syms[i].st_value);
        }
    }
    munmap(map, st.st_size);

    bool allRequired = true;
    size_t bound = 0, total = sizeof(kBindings) / sizeof(kBindings[0]);
    for (auto& b : kBindings) {
        if (*b.slot) { ++bound; continue; }
        fprintf(stderr, "[shadowmap] UNRESOLVED%s: %s\n",
                b.required ? "" : " (optional)", b.mangled);
        if (b.required) allRequired = false;
    }
    fprintf(stderr, "[shadowmap] resolved %zu/%zu symbols (base=%p)\n",
            bound, total, (void*)mod.base);
    return allRequired;
}
#else   // ---------------------------------------------------------------- Windows
// nwmain.exe is stripped, but exports 18,245 named symbols -- every function
// AND every static data symbol the injector needs. So resolution is a plain
// GetProcAddress against the MSVC-mangled names in win/nwn_win_symbols.h,
// matched one-to-one with the Linux binding list.
static bool resolve_symbols() {
    size_t bound = 0, total = sizeof(kBindings) / sizeof(kBindings[0]);
    bool allRequired = true;
    for (auto& b : kBindings) {
        const char* win = nullptr;
        for (int i = 0; i < kNwnWinSymbolCount; ++i)
            if (std::strcmp(kNwnWinSymbols[i].linuxName, b.mangled) == 0) {
                win = kNwnWinSymbols[i].winName; break;
            }
        if (win) *b.slot = nwn_win_resolve(win);
        if (*b.slot) { ++bound; continue; }
        fprintf(stderr, "[shadowmap] UNRESOLVED%s: %s (win: %s)\n",
                b.required ? "" : " (optional)", b.mangled, win ? win : "no mapping");
        if (b.required) allRequired = false;
    }
    fprintf(stderr, "[shadowmap] resolved %zu/%zu symbols via exports (base=%p)\n",
            bound, total, (void*)GetModuleHandleW(nullptr));
    return allRequired;
}
#endif

#include "shadow_targets.inc"
#include "shadow_diagnostics_settings.inc"
#include "shadow_replay.inc"
// ===========================================================================
//  Phase 1: bind, clear, restore. Proves the target stays healthy every frame.
// ===========================================================================
static void shadow_prepass() {
    if (!g_created) create_target();
    if (!g_usable) return;

    SavedState st = bind_target(false);
    clear_target();
    restore_state(st);

    GLenum err = gl::GetError();
    static bool loggedOk = false, loggedErr = false;
    if (err != GL_NO_ERROR) {
        if (!loggedErr || g_verbose) {
            fprintf(stderr, "[shadowmap] GL error 0x%x during prepass (frame %ld)\n",
                    err, g_frames);
            loggedErr = true;
        }
    } else if (g_verbose) {
        fprintf(stderr, "[shadowmap] prepass ok (frame %ld)\n", g_frames);
    } else if (!loggedOk) {
        fprintf(stderr, "[shadowmap] prepass ok, depth target bound and cleared "
                        "cleanly. The bucket probe is safe.\n");
        loggedOk = true;
    }
}

// ===========================================================================
//  Phase 2 probe
// ===========================================================================
static bool g_probeFaulted = false;

// Render every bucket index into the target, counting samples per bucket with
// an occlusion query. Returns the indices that drew something.
static std::vector<int> probe_side(void* scene, const char* side) {
    std::vector<int> good;
    SavedState st = bind_target(true);

    for (int i = 0; i < g_nBuckets; ++i) {
        clear_target();
        GLuint samples = 0;
        gl::BeginQuery(GL_SAMPLES_PASSED, g_query);
        bool ok = guarded_render_bucket(scene, i);
        gl::EndQuery(GL_SAMPLES_PASSED);
        if (!ok) { g_probeFaulted = true; break; }
        gl::GetQueryObjectuiv(g_query, GL_QUERY_RESULT, &samples);
        if (samples > 0) {
            good.push_back(i);
            fprintf(stderr, "[shadowmap]   %s bucket %2d: %u samples\n",
                    side, i, samples);
        }
    }

    restore_state(st);
    return good;
}

// One attempt = both sides of Scene::Render(). Returns true when finished
// (found geometry, faulted, or ran out of tries).
static bool probe_finish(void* scene,
                         const std::vector<int>& before,
                         const std::vector<int>& after) {
    if (g_probeFaulted) {
        fprintf(stderr,
            "[shadowmap] RESULT: FAULT. RenderDrawBucket is not callable\n"
            "[shadowmap] standalone. Falling back to capture-in-frame-N,\n"
            "[shadowmap] replay-in-frame-N+1. RESTART THE GAME now -- state is\n"
            "[shadowmap] undefined after a caught fault.\n");
        return true;
    }

    const bool any = !before.empty() || !after.empty();
    if (!any) {
        if (g_probeAttempt >= g_probeTries) {
            fprintf(stderr,
                "[shadowmap] RESULT: gave up after %d attempts -- no bucket ever\n"
                "[shadowmap] drew anything, on either side of Scene::Render().\n"
                "[shadowmap] Try NWN_SHADOWMAP_BUCKETS=64. If still nothing, the\n"
                "[shadowmap] call needs setup we did not reproduce; fall back to\n"
                "[shadowmap] capture-and-replay via the five draw entry points.\n",
                g_probeAttempt);
            return true;
        }
        fprintf(stderr, "[shadowmap] attempt %d/%d: no geometry yet "
                        "(still loading?), retrying in %.0fs\n",
                g_probeAttempt, g_probeTries, g_probeEvery);
        return false;
    }

    // Found geometry. Re-render the winning side into one target and dump it so
    // the result can be eyeballed rather than trusted from counters.
    const std::vector<int>& win = !after.empty() ? after : before;
    const char* winSide         = !after.empty() ? "AFTER" : "BEFORE";

    SavedState st = bind_target(true);
    clear_target();
    for (int i : win) if (!guarded_render_bucket(scene, i)) break;
    gl::Finish();
    dump_depth("shadowmap_probe.pgm");
    restore_state(st);

    fprintf(stderr,
        "[shadowmap] RESULT: RenderDrawBucket IS callable standalone.\n"
        "[shadowmap] Geometry found on the %s side of Scene::Render().\n"
        "[shadowmap] Buckets:", winSide);
    for (int i : win) fprintf(stderr, " %d", i);
    fprintf(stderr, "\n[shadowmap] Open shadowmap_probe.pgm -- if that is a depth "
                    "image of your\n[shadowmap] scene, the light-matrix diagnostic is "
                    "unblocked.\n");
    return true;
}

// ===========================================================================
//  Phase 3: render the buckets from the light's point of view
// ===========================================================================
static subhook_t g_hookView  = nullptr;
static subhook_t g_hookPersp = nullptr;
static subhook_t g_hookAreaShadowFlags = nullptr;
static subhook_t g_hookAurEnableShadowing = nullptr;
static subhook_t g_hookAurDisableShadowing = nullptr;
static subhook_t g_hookAurSetDynProjLight = nullptr;
static subhook_t g_hookStencil = nullptr;
static subhook_t g_hookTraceCameraRender = nullptr;
static subhook_t g_hookTraceCameraScene = nullptr;
static subhook_t g_hookTraceManageSceneBSP = nullptr;
static subhook_t g_hookTraceSceneSingle = nullptr;
static subhook_t g_hookTraceSceneDynamic = nullptr;
static subhook_t g_hookTraceBucket = nullptr;
static subhook_t g_hookTracePrioritizeShadow = nullptr;
static subhook_t g_hookTraceGetShadowLights = nullptr;
static subhook_t g_hookSetLightGL           = nullptr;
#ifndef _WIN32
static subhook_t g_hookAurTextureBind       = nullptr;
#endif
static bool      g_inStencilShadow = false;
static GLuint    g_stencilPrograms[32] = {};
static unsigned  g_stencilProgramCount = 0;

// Phase 1 trace context. The render path is single-threaded, but nested camera
// renders are possible (world, portrait, UI), so camera ownership is restored
// on return instead of treated as one global forever.
static void* g_traceCurrentCamera = nullptr;
static void* g_traceAreaCamera = nullptr;
static void* g_traceCurrentScene = nullptr;
static int   g_traceCameraDepth = 0;

// *** TRAMPOLINE-FREE ORIGINAL CALLS ***
// subhook_make_trampoline() returns -EINVAL for any instruction its opcode
// table cannot decode, and subhook_install() then leaves hook->trampoline NULL
// while STILL installing the hook and returning success. Calling
// subhook_get_trampoline() blindly therefore jumps to address 0. That is what
// crashed the client twice here.
//
// So for these capture hooks we never use a trampoline: temporarily remove the
// hook, call the real function at its real address, reinstall. Immune to both
// undecodable prologues and RIP-relative relocation, at the cost of two
// mprotect+memcpy per call (these fire a handful of times per frame).
// Not thread-safe -- fine, the render path is single-threaded.
#define CALL_ORIGINAL(hook, fn, ...)                                          \
    do {                                                                      \
        if (hook) subhook_remove(hook);                                       \
        fn(__VA_ARGS__);                                                      \
        if (hook) subhook_install(hook);                                      \
    } while (0)

#ifndef _WIN32
extern "C" void AurTextureBindInUnit_detour(void* texture, unsigned int unit,
                                             int controllerIndex) {
    if (eng::AurTextureGetName)
        nwn_oit_note_texture_bind(unit, eng::AurTextureGetName(texture));
    // This is intentionally diagnostic-only.  CAurTexture::BindInUnit has a
    // prologue that subhook may expose as a nominal trampoline while still
    // relocating it incorrectly, which corrupts texture/controller state and
    // produces camera-dependent UV/checker garbage.  Always take the slower
    // trampoline-free path here; normal OIT never installs this hook.
    CALL_ORIGINAL(g_hookAurTextureBind, eng::AurTextureBindInUnit,
                  texture, unit, controllerIndex);
}
#endif

// CNWCArea owns these flags.  We read them only after the engine has updated
// its own lighting state.  The offsets were live-verified on final Linux NWN:
// MoonShadows +0xa8, SunShadows +0xc8, IsNight +0xdc, ShadowOpacity +0x104.
// AREA SHADOW POLICY, applied from EITHER source.
//
// Two paths produce the same answer and both feed this:
//   * Linux hooks CNWCArea::UpdateShadowingLights and reads the flag fields.
//   * BOTH platforms observe the three Aur* calls that function ends in, which
//     is the only route available on Windows (see nwn_win_symbols.h).
// Keeping the fade in one place is what lets the two run side by side on Linux
// and be compared, rather than each growing its own slightly different ramp.
static void apply_area_shadow_allowed(bool allowed, const void* source) {
    const bool hadState = g_haveAreaShadowFlags;
    const bool sourceChanged = source != g_areaShadowSource;
    const bool oldAllowed = !g_areaShadowFlagsApply ||
        (g_areaIsNight ? g_areaMoonShadows : g_areaSunShadows);
    const bool newAllowed = !g_areaShadowFlagsApply || allowed;
    g_areaShadowSource = const_cast<void*>(source);
    // Continue from the currently visible fade value, rather than snapping if
    // NWN sends consecutive day/night updates during dawn or dusk.
    if (!hadState || sourceChanged) {
        g_areaShadowFadeFrom = g_areaShadowFadeTo = newAllowed ? 1.0f : 0.0f;
    } else if (oldAllowed != newAllowed) {
        float visible = g_areaShadowFadeTo;
        if (g_areaShadowFadeSeconds > 0.0f) {
            const float t = (float)std::max(0.0, std::min(1.0,
                (now_seconds() - g_areaShadowFadeStart) / (double)g_areaShadowFadeSeconds));
            visible = g_areaShadowFadeFrom + (g_areaShadowFadeTo - g_areaShadowFadeFrom) * t;
        }
        g_areaShadowFadeFrom = visible;
        g_areaShadowFadeTo = newAllowed ? 1.0f : 0.0f;
        g_areaShadowFadeStart = now_seconds();
    }
}

// THE OBSERVED PATH. CNWCArea::UpdateShadowingLights() ends in exactly one of
//     AurEnableShadowing(area)  or  AurDisableShadowing(area)
// followed by a tail call to AurSetDynamicProjectionLight(). That last call is
// reached from NOWHERE else in the binary, so it marks the end of an area
// update and tells us the toggle just seen belongs to the AREA -- not to
// CNWCVisualEffectOnObject::EnableHardCodedEffectShadow, which also toggles
// shadowing but never reaches it.
//
// This yields `allowed` directly: the engine's own decision, already combining
// IsNight, SunShadows, MoonShadows and whether the player carries a light. That
// is strictly better than re-deriving it, and it is the ONLY form available on
// Windows, where nothing of CNWCArea is exported.
// THE PLAYER'S CARRIED LIGHT, learned for free: UpdateShadowingLights passes it
// to AurSetDynamicProjectionLight whenever the player is holding one (null
// otherwise). Windows uses it to pin that light to the front of the shadow
// roster; see the NWN_WIN_LOCAL_FASTPATH block in shadow_local_lights.inc.
#if NWN_WIN_LOCAL_FASTPATH
// 0 = priority ordering OFF (the read is not trusted on this platform yet).
// Set to the verified offset once [prio-scan] identifies it. Windows-only,
// like the ordering it gates.
static size_t g_localPriorityOffset = 0;
#endif
static const void* g_playerCarriedLight = nullptr;
// What this AREA said about shadows when no carried light was muddying it, and
// which area that was. See AurSetDynProjLight_detour for why this is needed.
static const void* g_areaPolicyScene   = nullptr;
static bool        g_areaPolicyAllowed = false;
static bool g_areaTogglePending      = false;
static bool g_areaTogglePendingOn    = false;

extern "C" void AurEnableShadowing_detour(void* aurObject) {
    g_areaTogglePending = true; g_areaTogglePendingOn = true;
    g_areaToggleObject = aurObject;
    CALL_ORIGINAL(g_hookAurEnableShadowing, eng::AurEnableShadowing, aurObject);
}

extern "C" void AurDisableShadowing_detour(void* aurObject) {
    g_areaTogglePending = true; g_areaTogglePendingOn = false;
    g_areaToggleObject = aurObject;
    CALL_ORIGINAL(g_hookAurDisableShadowing, eng::AurDisableShadowing, aurObject);
}

extern "C" void AurSetDynProjLight_detour(void* aurObject) {
    // Null means "no carried light this update", which is just as informative:
    // stow the torch and the pin must release.
    g_playerCarriedLight = aurObject;
    if (g_areaTogglePending) {
        g_areaTogglePending = false;
        // THE CARRIED-TORCH OVERRIDE, and it is not an area policy decision.
        //
        // UpdateShadowingLights has a path that fires when the player carries a
        // creature light: it calls AurDisableShadowing(area) and then passes
        // THAT LIGHT to AurSetDynamicProjectionLight. The engine is swapping
        // the area's directional shadowing for the torch's dynamic projection,
        // not saying "this area has no sun shadow".
        //
        // Taking it at face value is what made a torch delete the sun's shadow
        // on Windows while Linux was fine: Linux reads the FLAG FIELDS, which
        // describe the area and know nothing about what the player is holding,
        // so it never saw this. Windows has only the observed path, so it did.
        //
        // ...BUT A NON-NULL ARGUMENT ALONE DOES NOT IDENTIFY IT, and assuming it
        // did was a bug (fixed 2026-08-15, same day). Two different paths make
        // the identical pair of calls:
        //
        //   night + carried light            -> Disable(area) + SetLight(light)
        //   day, SunShadows OFF + carried light -> Disable(area) + SetLight(light)
        //
        // The second is an INTERIOR, where the area genuinely has no sun shadow.
        // Ignoring the disable there left sun shadows switched on indoors as
        // soon as the player walked in holding a torch.
        //
        // The flag fields would settle it instantly, and Linux reads them -- but
        // Windows cannot, which is the whole reason this path exists. So instead
        // we remember what the area said while NO light was carried, and only
        // that is treated as policy:
        //
        //   * Enable, or Disable with a null light  -> unambiguous. Apply, and
        //     remember it as this area's answer.
        //   * Disable with a light                  -> ambiguous. Keep this
        //     area's remembered answer if we have one; otherwise apply the
        //     disable, because an area we have never seen allow shadows is far
        //     more likely to be an interior than an outdoor area we happened to
        //     walk into torch-first.
        //
        // The memory is per-AREA and resets on an area change, so an outdoor
        // "allowed" can never leak into the interior you just entered.
        // KEY ON THE AREA'S OWN CAurObject, not the Scene. The Scene pointer is
        // REUSED across areas -- walking outside and back in produced the same
        // pointer, so the reset never fired and the outdoor "allowed" leaked
        // into the interior, which is exactly the bug this was meant to fix.
        //
        // The toggle's argument is the area's own object ([area+0x250] in
        // UpdateShadowingLights), so it changes when the area does.
        if (g_areaToggleObject != g_areaPolicyScene) {
            g_areaPolicyScene = g_areaToggleObject;
            g_areaPolicyAllowed = false;          // unknown until proven
        }
        const bool ambiguous = (!g_areaTogglePendingOn && aurObject != nullptr);
        if (!ambiguous) {
            g_areaPolicyAllowed = g_areaTogglePendingOn;
        } else if (g_areaPolicyAllowed) {
            CALL_ORIGINAL(g_hookAurSetDynProjLight, eng::AurSetDynProjLight, aurObject);
            return;                                // torch swap: keep shadows on
        }
        // CROSS-CHECK on Linux, where the flag-reading hook also runs. If the
        // two ever disagree the observed path is wrong somewhere and this says
        // so once, loudly, instead of silently shipping a different policy on
        // the two platforms.
        if (g_hookAreaShadowFlags && g_haveAreaShadowFlags) {
            const bool derived = g_areaIsNight ? g_areaMoonShadows : g_areaSunShadows;
            static bool warned = false;
            if (derived != g_areaTogglePendingOn && !warned) {
                warned = true;
                fprintf(stderr, "[shadowmap][area] observed toggle (%s) disagrees with the "
                                "flag fields (%s). The observed path is the one Windows "
                                "uses -- investigate before trusting it there.\n",
                        g_areaTogglePendingOn ? "on" : "off", derived ? "on" : "off");
            }
        }
        // Only OWN the state where the flag fields are unavailable; otherwise
        // the richer path (which also carries opacity) stays authoritative.
        if (!g_hookAreaShadowFlags) {
            apply_area_shadow_allowed(g_areaTogglePendingOn, g_areaToggleObject);
            g_areaSunShadows = g_areaMoonShadows = g_areaTogglePendingOn;
            g_areaIsNight = false;
            g_haveAreaShadowFlags = true;
            static bool said = false;
            if (!said) {
                said = true;
                fprintf(stderr, "[shadowmap][area] directional shadow policy from the "
                                "OBSERVED Aur* path (no CNWCArea access on this platform)\n");
            }
        }
    }
    CALL_ORIGINAL(g_hookAurSetDynProjLight, eng::AurSetDynProjLight, aurObject);
}

extern "C" void AreaUpdateShadowingLights_detour(void* self) {
    CALL_ORIGINAL(g_hookAreaShadowFlags, eng::AreaUpdateShadowingLights, self);
    if (!self) return;
    uint32_t moon = 0, sun = 0, night = 0;
    uint8_t opacity = 255;
    std::memcpy(&moon,    (const char*)self + 0x0a8, sizeof(moon));
    std::memcpy(&sun,     (const char*)self + 0x0c8, sizeof(sun));
    std::memcpy(&night,   (const char*)self + 0x0dc, sizeof(night));
    std::memcpy(&opacity, (const char*)self + 0x0104, sizeof(opacity));
    if (moon > 1 || sun > 1 || night > 1 || opacity > 100) return;

    const float opacityFloat = (float)opacity * 0.01f;
    const bool changed = !g_haveAreaShadowFlags || self != g_areaShadowSource ||
        g_areaSunShadows != (sun != 0) || g_areaMoonShadows != (moon != 0) ||
        g_areaIsNight != (night != 0) ||
        std::fabs(g_areaShadowOpacity - opacityFloat) > 0.0001f;
    // BEFORE the fields are updated: the helper reads the OLD ones to work out
    // where the fade is currently sitting.
    apply_area_shadow_allowed((night != 0) ? (moon != 0) : (sun != 0), self);
    g_areaSunShadows = sun != 0;
    g_areaMoonShadows = moon != 0;
    g_areaIsNight = night != 0;
    g_areaShadowOpacity = opacityFloat;
    g_haveAreaShadowFlags = true;
    if (changed) {
        const bool allowed = g_areaIsNight ? g_areaMoonShadows : g_areaSunShadows;
        fprintf(stderr, "[shadowmap][area] %s directional shadows: %s "
                        "(Sun=%d Moon=%d opacity=%u%%)\n",
                g_areaIsNight ? "night" : "day", allowed ? "on" : "off",
                g_areaSunShadows ? 1 : 0, g_areaMoonShadows ? 1 : 0,
                (unsigned)opacity);
    }
}

// Capture the renderer instance and the live camera transform.
extern "C" void SetViewTransform_detour(void* pos, void* quat) {
    if (!g_inOurPass) {
        if (pos)  std::memcpy(&g_camPos,  pos,  sizeof(Vec3f));
        if (quat) std::memcpy(&g_camQuat, quat, sizeof(Quat4f));
        if (g_inSceneRender) g_viewInsideRender = true;
        // Log a few calls, not just the first: the first is an identity setup
        // (quaternion (1,0,0,0) -- note the order is wxyz), the useful values
        // come later.
        if (g_camLogged < 3) {
            ++g_camLogged;
            fprintf(stderr,
                "[shadowmap] view #%d  pos=(%.3f %.3f %.3f)  "
                "quat[wxyz]=(%.4f %.4f %.4f %.4f)\n",
                g_camLogged, g_camPos.x, g_camPos.y, g_camPos.z,
                g_camQuat.a, g_camQuat.b, g_camQuat.c, g_camQuat.d);
        }
        g_haveCam = true;
    }
    CALL_ORIGINAL(g_hookView, eng::SetViewTransform, pos, quat);
}

// NOTE: this hooks aurMatrixStack::Perspective, NOT
// GLRender::SetPerspectiveTransform. The latter's prologue starts with a
// RIP-relative lea and is not trampoline-safe -- see the header. `self` here is
// an aurMatrixStack*, deliberately unused; we only want the parameters.
extern "C" void MtxPerspective_detour(void* self, float fov, float aspect,
                                      float zn, float zf) {
    if (!g_inOurPass) {
        g_camPersp[0] = fov; g_camPersp[1] = aspect;
        g_camPersp[2] = zn;  g_camPersp[3] = zf;
        if (!g_havePersp) {
            g_havePersp = true;
            fprintf(stderr, "[shadowmap] captured perspective fov=%.3f aspect=%.4f "
                            "near=%.3f far=%.3f\n", fov, aspect, zn, zf);
        }
    }
    CALL_ORIGINAL(g_hookPersp, eng::MtxPerspective, self, fov, aspect, zn, zf);
}

// Observation-only reference hook.  The engine's own stencil implementation
// is left entirely intact; the draw interceptor below merely counts the
// programs and draws which occur while this call is live.
extern "C" void SceneRenderShadows_detour(void* self, int light, bool clearStencil) {
    // g_stencilProgramCount is NOT diagnostic: the draw interceptor learns the
    // engine's stencil programs here, and the visible-caster duplication uses
    // that list to exclude them (see local_visible_draw_eligible). Resetting it
    // per pass is what keeps it current.
    g_stencilProgramCount = 0;
    // SUPPRESS THE ENGINE'S OWN SHADOWS. This module draws a cascaded shadow
    // over the whole scene, so NWN's blob/stencil shadows are a second, worse
    // shadow underneath every object. Skipping the call is how they go away for
    // ANY install -- the alternative, replacing the blob texture, needs each
    // user to modify their own game data.
    //
    // The call is skipped WHOLE, including whatever stencil clearing it does.
    // If a later pass turns out to depend on that clear, this is the first
    // thing to suspect.
    //
    // NOTE: with this on the engine's stencil pass never runs, so it never
    // writes `shadowalpha` and the day/night fade has nothing to follow.
    if (g_hideEngineShadows) return;
    g_inStencilShadow = true;
    CALL_ORIGINAL(g_hookStencil, eng::SceneRenderShadows, self, light, clearStencil);
    g_inStencilShadow = false;
}

// The engine's reference forward axis and quaternion component order are both
// unknown, so offer the four plausible combinations and let the dump decide.
static Quat4f light_quat(const float dir[3], int conv) {
    const float fwdGL[3]  = { 0.0f, 0.0f, -1.0f };   // camera looks down -Z
    const float fwdNWN[3] = { 0.0f, 1.0f,  0.0f };   // Z-up world, forward +Y
    const float* fwd = (conv < 2) ? fwdGL : fwdNWN;

    float q[4];
    quat_from_to(fwd, dir, q);                        // q = (x, y, z, w)

    Quat4f out;
    if (conv == 0 || conv == 2) { out = { q[0], q[1], q[2], q[3] }; }  // xyzw
    else                        { out = { q[3], q[0], q[1], q[2] }; }  // wxyz
    return out;
}

// View substitution mode 2: build the view matrix from primitives instead of
// going through SetViewTransform, whose Vector& semantics are unknown.
// A view matrix is R * T(-eye): rotate the world so the light direction becomes
// the camera's forward (-Z), then translate the eye to the origin. For a
// straight-down light in NWN's Z-up world the rotation is identity, because
// world -Z already IS the camera's forward.
static bool set_light_view_direct(const Vec3f& eye, const float dir[3]) {
    if (!eng::SetMatrixMode || !eng::LoadMatrixIdentity || !eng::Translate ||
        !eng::Rotate)
        return false;

    eng::SetMatrixMode(g_viewMtxMode);
    eng::LoadMatrixIdentity();

    const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    float q[4];
    quat_from_to(dir, fwd, q);                 // rotation taking dir -> -Z
    float w   = q[3] > 1.0f ? 1.0f : (q[3] < -1.0f ? -1.0f : q[3]);
    float ang = 2.0f * std::acos(w);
    float s   = std::sqrt(1.0f - w * w);
    if (s > 1e-5f && ang > 1e-5f)
        eng::Rotate(ang * 57.2957795f, q[0]/s, q[1]/s, q[2]/s);

    eng::Translate(-eye.x, -eye.y, -eye.z);
    return true;
}

#include "shadow_shader_interposition.inc"
#include "shadow_fullscreen_receiver.inc"
#include "shadow_overlay_runtime.inc"
#include "shadow_trace_cascade.inc"
#include "shadow_local_lights.inc"

// ---------------------------------------------------------------------------
// Direct CSM receiver for A2C foliage.
//
// The ordinary receiver reconstructs one surface from the engine's resolved
// single-sample depth. That cannot represent an A2C pixel containing foliage
// and background samples simultaneously. Source-classified foliage therefore
// samples the same cascade arrays in its own fragment shader, before coverage
// is resolved. The OIT/A2C module brackets exactly one native draw with these
// calls; all borrowed texture state is restored immediately afterwards.
// ---------------------------------------------------------------------------
namespace {
struct A2cShadowLocations {
    GLuint program = 0;
    GLint staticDepth = -1, dynamicDepth = -1;
    GLint cameraVpInv = -1, cameraView = -1, lightVp = -1;
    GLint clipFar = -1, viewport = -1, dynamicLayers = -1;
    GLint strength = -1, bias = -1, blend = -1, pcf = -1;
    GLint localDepth = -1, localVp = -1, localPos = -1;
    GLint localRadius = -1, localFade = -1, localSlots = -1;
    GLint localStrength = -1, localBias = -1, localEdgeFade = -1;
    GLint localSoft = -1, localNormalBias = -1, localMinSep = -1;
    GLint localTanHalfFov = -1, localLift = -1, localFalloff = -1;
    GLint lampFalloff = -1;
};
A2cShadowLocations g_a2cShadowLocations[192] = {};
unsigned g_a2cShadowLocationCount = 0;
bool g_a2cShadowBorrowed = false;
GLint g_a2cShadowOldActive = GL_TEXTURE0;
GLint g_a2cShadowOldStatic = 0;
GLint g_a2cShadowOldDynamic = 0;
GLint g_a2cShadowOldLocal = 0;

A2cShadowLocations* a2c_shadow_locations(GLuint program) {
    for (unsigned i = 0; i < g_a2cShadowLocationCount; ++i)
        if (g_a2cShadowLocations[i].program == program)
            return &g_a2cShadowLocations[i];
    if (g_a2cShadowLocationCount >=
        sizeof(g_a2cShadowLocations) / sizeof(g_a2cShadowLocations[0]))
        return nullptr;
    A2cShadowLocations& l = g_a2cShadowLocations[g_a2cShadowLocationCount++];
    l.program = program;
    l.staticDepth   = gl::GetUniformLocation(program, "nwnA2cStaticDepth");
    l.dynamicDepth  = gl::GetUniformLocation(program, "nwnA2cDynamicDepth");
    l.cameraVpInv   = gl::GetUniformLocation(program, "nwnA2cCameraVPInv");
    l.cameraView    = gl::GetUniformLocation(program, "nwnA2cCameraView");
    l.lightVp       = gl::GetUniformLocation(program, "nwnA2cLightVP[0]");
    l.clipFar       = gl::GetUniformLocation(program, "nwnA2cClipFar");
    l.viewport      = gl::GetUniformLocation(program, "nwnA2cViewport");
    l.dynamicLayers = gl::GetUniformLocation(program, "nwnA2cDynamicLayers");
    l.strength      = gl::GetUniformLocation(program, "nwnA2cShadowStrength");
    l.bias          = gl::GetUniformLocation(program, "nwnA2cShadowBias");
    l.blend         = gl::GetUniformLocation(program, "nwnA2cShadowBlend");
    l.pcf           = gl::GetUniformLocation(program, "nwnA2cShadowPcf");
    l.localDepth    = gl::GetUniformLocation(program, "nwnA2cLocalDepth");
    l.localVp       = gl::GetUniformLocation(program, "nwnA2cLocalVP[0]");
    l.localPos      = gl::GetUniformLocation(program, "nwnA2cLocalPos[0]");
    l.localRadius   = gl::GetUniformLocation(program, "nwnA2cLocalRadius[0]");
    l.localFade     = gl::GetUniformLocation(program, "nwnA2cLocalFade[0]");
    l.localSlots    = gl::GetUniformLocation(program, "nwnA2cLocalSlots");
    l.localStrength = gl::GetUniformLocation(program, "nwnA2cLocalStrength");
    l.localBias     = gl::GetUniformLocation(program, "nwnA2cLocalBias");
    l.localEdgeFade = gl::GetUniformLocation(program, "nwnA2cLocalEdgeFade");
    l.localSoft     = gl::GetUniformLocation(program, "nwnA2cLocalSoft");
    l.localNormalBias = gl::GetUniformLocation(program, "nwnA2cLocalNormalBias");
    l.localMinSep   = gl::GetUniformLocation(program, "nwnA2cLocalMinSep");
    l.localTanHalfFov = gl::GetUniformLocation(program, "nwnA2cLocalTanHalfFov");
    l.localLift     = gl::GetUniformLocation(program, "nwnA2cLocalLift");
    l.localFalloff  = gl::GetUniformLocation(program, "nwnA2cLocalFalloff");
    l.lampFalloff   = gl::GetUniformLocation(program, "nwnA2cLampFalloff");
    return &l;
}
} // namespace

bool nwn_shadow_begin_a2c_receiver(unsigned int rawProgram) {
    const GLuint program = (GLuint)rawProgram;
    if (!program || g_a2cShadowBorrowed || !g_receiverEnabled ||
        !g_cascadeCompositeShadows || !g_cascadeCsmStaticReceiver ||
        !g_cascadeTargetsUsable || !g_cascadeStaticTex ||
        !g_shadowFrameContext.valid || !g_cascadeMath.valid ||
        !gl::GetUniformLocation || !gl::Uniform1i || !gl::Uniform1f ||
        !gl::Uniform4f || !gl::UniformMatrix4fv || !gl::ActiveTexture ||
        !gl::BindTexture || !gl::GetIntegerv)
        return false;

    A2cShadowLocations* l = a2c_shadow_locations(program);
    if (!l || l->staticDepth < 0 || l->cameraVpInv < 0 ||
        l->cameraView < 0 || l->lightVp < 0 || l->clipFar < 0 ||
        l->viewport < 0 || l->strength < 0)
        return false;

    constexpr int kLocalUnit = 28;
    constexpr int kDynamicUnit = 29;
    constexpr int kStaticUnit = 30;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &g_a2cShadowOldActive);
    gl::ActiveTexture(GL_TEXTURE0 + kStaticUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &g_a2cShadowOldStatic);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, g_cascadeStaticTex);
    gl::ActiveTexture(GL_TEXTURE0 + kDynamicUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &g_a2cShadowOldDynamic);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, g_cascadeDynamicTex);
    gl::ActiveTexture(GL_TEXTURE0 + kLocalUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &g_a2cShadowOldLocal);
    const bool localReady = g_localLightReceiver && g_localLightTargetUsable &&
                            g_localLightDepthTex && g_haveLocalLightVP &&
                            g_localDeselectFadeShown > 0.0005f;
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, localReady ? g_localLightDepthTex
                                                    : g_cascadeStaticTex);
    gl::ActiveTexture((GLenum)g_a2cShadowOldActive);
    g_a2cShadowBorrowed = true;

    gl::Uniform1i(l->staticDepth, kStaticUnit);
    if (l->dynamicDepth >= 0) gl::Uniform1i(l->dynamicDepth, kDynamicUnit);
    gl::UniformMatrix4fv(l->cameraVpInv, 1, GL_FALSE,
                         g_shadowFrameContext.viewProjectionInverse);
    gl::UniformMatrix4fv(l->cameraView, 1, GL_FALSE,
                         g_shadowFrameContext.view);
    gl::UniformMatrix4fv(l->lightVp, kCascadeCount, GL_FALSE,
                         &g_cascadeMath.lightVP[0][0]);
    gl::Uniform4f(l->clipFar, g_cascadeMath.clipFar[0],
                  g_cascadeMath.clipFar[1], g_cascadeMath.clipFar[2],
                  g_cascadeMath.clipFar[3]);
    gl::Uniform4f(l->viewport,
                  (GLfloat)g_shadowFrameContext.viewport[0],
                  (GLfloat)g_shadowFrameContext.viewport[1],
                  (GLfloat)g_shadowFrameContext.viewport[2],
                  (GLfloat)g_shadowFrameContext.viewport[3]);
    if (l->dynamicLayers >= 0)
        gl::Uniform1i(l->dynamicLayers,
                      g_cascadeCsmDynamicReceiver ? g_cascadeDynamicLayers : 0);
    gl::Uniform1f(l->strength, effective_area_sun_strength());
    if (l->bias >= 0)  gl::Uniform1f(l->bias, g_cascadeReceiverBias);
    if (l->blend >= 0) gl::Uniform1f(l->blend, g_cascadeBlendWidth);
    if (l->pcf >= 0)   gl::Uniform1f(l->pcf, g_cascadePcfRadius);

    if (l->localDepth >= 0) gl::Uniform1i(l->localDepth, kLocalUnit);
    if (l->localVp >= 0)
        gl::UniformMatrix4fv(l->localVp, kLocalLightFaces, GL_FALSE,
                             &g_localLightFaceVP[0][0]);
    float localPos[kLocalLightFaces * 4] = {};
    float localRadius[kLocalLightFaces] = {};
    float localFade[kLocalLightFaces] = {};
    for (unsigned i = 0; i < (unsigned)kLocalLightFaces; ++i) {
        localPos[i*4+0] = g_localLightSlotPos[i][0];
        localPos[i*4+1] = g_localLightSlotPos[i][1];
        localPos[i*4+2] = g_localLightSlotPos[i][2];
        localPos[i*4+3] = 1.0f;
        localRadius[i] = g_localLightSlotRadius[i];
        localFade[i] = (g_localLightSlotFade[i] > 0.0f &&
                        g_localLightSlotFade[i] <= 1.0f)
                     ? g_localLightSlotFade[i] : 1.0f;
    }
    if (l->localPos >= 0 && gl::Uniform4fv)
        gl::Uniform4fv(l->localPos, kLocalLightFaces, localPos);
    if (l->localRadius >= 0 && gl::Uniform1fv)
        gl::Uniform1fv(l->localRadius, kLocalLightFaces, localRadius);
    if (l->localFade >= 0 && gl::Uniform1fv)
        gl::Uniform1fv(l->localFade, kLocalLightFaces, localFade);
    if (l->localSlots >= 0)
        gl::Uniform1i(l->localSlots, localReady ? (GLint)g_localLightSlotCount : 0);
    if (l->localStrength >= 0)
        gl::Uniform1f(l->localStrength, localReady
            ? g_localLightStrength * g_engineFadeLevel * g_localDeselectFadeShown : 0.0f);
    if (l->localBias >= 0) gl::Uniform1f(l->localBias, g_localLightBias);
    if (l->localEdgeFade >= 0) gl::Uniform1f(l->localEdgeFade, g_localLightEdgeFade);
    if (l->localSoft >= 0) gl::Uniform1f(l->localSoft, g_localLightSoft);
    if (l->localNormalBias >= 0) gl::Uniform1f(l->localNormalBias, g_localLightNormalBias);
    if (l->localMinSep >= 0) gl::Uniform1f(l->localMinSep, g_localLightMinSep);
    if (l->localTanHalfFov >= 0) gl::Uniform1f(l->localTanHalfFov,
        (float)std::tan(local_face_fov_deg(local_source_faces()) * 3.14159265f / 360.0f));
    if (l->localLift >= 0) gl::Uniform1f(l->localLift, g_localLightHeight);
    if (l->localFalloff >= 0) gl::Uniform1f(l->localFalloff, g_localLightFalloff);
    if (l->lampFalloff >= 0) gl::Uniform2f(l->lampFalloff,
                                           g_lightMaxIntensityInv,
                                           g_lightFalloffFactor);

    static bool reported = false;
    if (!reported) {
        reported = true;
        fprintf(stderr, "[a2c][shadow] direct per-fragment CSM receiver active: "
                        "program=%u static=%u dynamic=%u cascades=%d/%d\n",
                program, g_cascadeStaticTex, g_cascadeDynamicTex,
                g_cascadeActiveCount, g_cascadeDynamicLayers);
    }
    static bool reportedLocal = false;
    if (localReady && !reportedLocal) {
        reportedLocal = true;
        fprintf(stderr, "[a2c][shadow] direct per-fragment local receiver active: "
                        "program=%u depth=%u slots=%u\n",
                program, g_localLightDepthTex, g_localLightSlotCount);
    }
    return true;
}

void nwn_shadow_end_a2c_receiver(void) {
    if (!g_a2cShadowBorrowed || !gl::ActiveTexture || !gl::BindTexture) return;
    constexpr int kLocalUnit = 28;
    constexpr int kDynamicUnit = 29;
    constexpr int kStaticUnit = 30;
    gl::ActiveTexture(GL_TEXTURE0 + kDynamicUnit);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)g_a2cShadowOldDynamic);
    gl::ActiveTexture(GL_TEXTURE0 + kStaticUnit);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)g_a2cShadowOldStatic);
    gl::ActiveTexture(GL_TEXTURE0 + kLocalUnit);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)g_a2cShadowOldLocal);
    gl::ActiveTexture((GLenum)g_a2cShadowOldActive);
    g_a2cShadowBorrowed = false;
}

bool nwn_core_replay_bucket(void* scene, int bucket) {
    if (!scene || !eng::SceneRenderDrawBucket) return false;
    const bool priorReplay = g_cascadeReplayActive;
    const int priorBucket = g_cascadeReplayBucket;
    const char* priorLabel = g_faultLabel;
    g_cascadeReplayActive = true;  // makes the bucket detour call only NWN
    g_cascadeReplayBucket = bucket;
    g_faultLabel = "OIT FOLIAGE REPLAY";
    const bool ok = guarded_render_bucket(scene, bucket);
    g_faultLabel = priorLabel;
    g_cascadeReplayBucket = priorBucket;
    g_cascadeReplayActive = priorReplay;
    return ok;
}
// ===========================================================================
//  The hook
// ===========================================================================
static subhook_t g_hook = nullptr;

static void update_engine_shadow_fade() {
    if (!eng::ShadowAlphaRaw) return;
    float alpha = 0.0f;
    std::memcpy(&alpha, eng::ShadowAlphaRaw, sizeof(alpha));
    if (!std::isfinite(alpha) || alpha < 0.0f) return;
    // A new light is a new fade. Without this, a torch lit at noon would
    // inherit the previous light's finished fade and never appear.
    if (g_localLightSelected != g_engineFadeLight) {
        g_engineFadeLight = g_localLightSelected;
        g_engineFadeLevel = 1.0f;
        g_engineFadePeak  = 0.0f;
    }
    // Learn the reference only while still fully visible: once the fall has
    // begun, a rise must not raise the bar and stall the fade.
    if (g_engineFadeLevel >= 0.999f && alpha > g_engineFadePeak)
        g_engineFadePeak = alpha;
    if (g_engineFadePeak <= 1.0e-6f) return;
    const float ratio = std::max(0.0f, std::min(1.0f, alpha / g_engineFadePeak));
    if (ratio < g_engineFadeLevel) g_engineFadeLevel = ratio;   // monotone
}

extern "C" void SceneRender_detour(void* self) {
    ++g_frames;
    // Armed BEFORE the engine renders, because the cascade replay happens
    // inside its Scene::RenderDrawBucket calls -- deciding after the fact
    // would leave the replay unable to finish and its GPU cost would keep
    // landing on the next blocking call.
    g_costFinishThisFrame = g_costReport && ((g_costFrames + 1) % 30) == 0;
    update_engine_shadow_fade();   // engine-driven local shadow fade-out
    lamp_list_begin();   // SetLightGL refills it during this frame
    settings_tick();     // persist panel edits, debounced

    // A GL context exists by the time the engine renders a scene, so this is
    // the first point at which wglGetProcAddress can resolve modern GL on
    // Windows. Until it succeeds, pass straight through: every injector pass
    // below dereferences gl:: pointers.
    if (!g_glBound) {
        g_glBound = bind_gl();
        if (g_glBound)
            fprintf(stderr, "[shadowmap] GL entry points bound from the render path; "
                            "injector fully live\n");
        else {
            void* tramp0 = subhook_get_trampoline(g_hook);
            if (tramp0) reinterpret_cast<eng::SceneRender_t>(tramp0)(self);
            else        CALL_ORIGINAL(g_hook, eng::SceneRender, self);
            return;
        }
    }

    // OIT's read-only census must be armed before NWN submits this scene's
    // geometry. This is a no-op unless an OIT/census environment switch is on.
    nwn_oit_prepare();

    // Phase 1 has a deliberately separate path.  In particular, do not call
    // create_target(), install a GLEW patch, capture receiver uniforms, replay
    // buckets, or draw the legacy red/green fullscreen triangle.  The only
    // side effect of this branch is bounded stderr logging from the trace hooks.
    // The engine's shadow-light priority list is the source-selection
    // authority for every local tier, so this small read-only hook is installed
    // in normal runs too; the remaining trace hooks stay opt-in.
    if (eng::LightGetShadowLights &&
        !g_hookTraceGetShadowLights) {
        g_hookTraceGetShadowLights = subhook_new((void*)eng::LightGetShadowLights,
                                                 (void*)LightGetShadowLights_trace_detour,
                                                 SUBHOOK_64BIT_OFFSET);
        if (!g_hookTraceGetShadowLights || subhook_install(g_hookTraceGetShadowLights) != 0) {
            fprintf(stderr, "[shadowmap][local-light] warning: engine shadow-light priority hook unavailable; "
                            "using census fallback\n");
            if (g_hookTraceGetShadowLights) {
                subhook_free(g_hookTraceGetShadowLights);
                g_hookTraceGetShadowLights = nullptr;
            }
        } else if (!g_traceEnabled) {
            fprintf(stderr, "[shadowmap][local-light] engine shadow-light priority hook active\n");
        }
    }

    if (g_traceEnabled) {
        trace_scene_enter(self);
        void* tramp = subhook_get_trampoline(g_hook);
        if (tramp) reinterpret_cast<eng::SceneRender_t>(tramp)(self);
        else       CALL_ORIGINAL(g_hook, eng::SceneRender, self);
        // This is intentionally after NWN's complete selected-area draw and
        // before trace_scene_exit clears the active-frame marker.  The private
        // static depth layer and the camera inverse must share this serial.
        apply_resolution_change();   // panel-staged reallocation, context is current
        // The reported frame is the one about to PRINT, so the stall below is
        // paid once every 300 frames instead of every frame. Without it recv
        // was submission time only and read as ~0.10 ms while the pass was in
        // fact the most expensive thing in the frame -- the GPU stall landed on
        // whatever GL call blocked next and was billed to `replay`, which sent
        // this investigation after the sun cascades for a whole test round.
        // THIS COUNTS Scene::Render CALLS, NOT FRAMES, and NWN makes several
        // per frame -- most of them trivial secondary scenes. Sampling every
        // 300th call caught the real scene 4 times out of 54 and reported
        // "enginedraws=3, replay=0.00ms" for the other 50, which describes a
        // GUI element and says nothing about the frame the user is complaining
        // about. Only a call that actually drew the world may report.
        const bool mainScene = g_frameDrawCalls > 100;
        const bool reportFrame = mainScene && g_costFinishThisFrame;
        // MSAA state of the framebuffer we are actually drawing into. With
        // antialiasing on, the per-frame scene-depth capture is a multisample
        // RESOLVE rather than a copy, and the receiver writes into a
        // multisampled target -- both scale with this number. It is also the
        // one setting known to differ between this project's two platforms
        // (native Windows is where MSAA broke the depth capture outright), so
        // no Windows-vs-Linux cost comparison means anything without it.
        if (reportFrame && gl::GetIntegerv) {
            gl::GetIntegerv(GL_SAMPLES, &g_fbSamples);
            gl::GetIntegerv(GL_VIEWPORT, g_fbViewport);
        }
        const double tRecv0 = now_seconds();
        draw_static_receiver(self);
        // glFinish so the number is the GPU's cost, not just submission time --
        // otherwise a fill-rate bound pass measures as ~0 ms and misleads.
        if ((g_receiverDebug || reportFrame) && gl::Finish) gl::Finish();
        g_msReceiver += (now_seconds() - tRecv0) * 1000.0;
        {   // Report from the accumulators themselves. Publishing them for the
            // receiver's own log line printed the PREVIOUS frame's values and
            // read as all-zero, which cost two test runs.
            if (g_costReport && mainScene && (++g_costFrames % 30) == 0) {
                // Cache figures are DELTAS over the reporting interval. Running
                // totals cannot show "it stopped hitting a minute ago", which is
                // the shape of the Windows-vs-Linux question they exist to answer.
                static unsigned pHit=0,pMove=0,pTurn=0,pSun=0,pScene=0,pOff=0;
                const unsigned dHit  = g_cascadeCacheHits    - pHit;
                const unsigned dMove = g_cascadeRefitMove    - pMove;
                const unsigned dTurn = g_cascadeRefitTurn    - pTurn;
                const unsigned dSun  = g_cascadeRefitSun     - pSun;
                const unsigned dScn  = g_cascadeRefitScene   - pScene;
                const unsigned dOff  = g_cascadeRefitDisabled- pOff;
                pHit=g_cascadeCacheHits; pMove=g_cascadeRefitMove;
                pTurn=g_cascadeRefitTurn; pSun=g_cascadeRefitSun;
                pScene=g_cascadeRefitScene; pOff=g_cascadeRefitDisabled;
                fprintf(stderr, "[shadowmap][cost] mainscene=%u recv=%.2fms world=%.2fms "
                                "replay=%.2fms copy=%.2fms enginedraws=%u receiver=%s "
                                "worldmap=%d^2 cascade=%d^2 dyncascades=%d "
                                "msaa=%dx viewport=%dx%d unimat4=%u useprog=%u engshadow=%u "
                                "lamps=%u/%d localslots=%u localdup=%.2fms/%udraws replays=%u/%udraws | fitcache hit=%u "
                                "refit(move=%u turn=%u sun=%u scene=%u off=%u)\n",
                        g_costFrames, g_msReceiver, g_msWorldMap, g_msReplay, g_msSceneCopy,
                        g_frameDrawCalls, g_receiverEnabled ? "on" : "OFF",
                        g_staticWorldSize, g_size, g_cascadeDynamicLayers,
                        g_fbSamples, g_fbViewport[2], g_fbViewport[3],
                        g_uniformMat4Shown, g_useProgramShown,
                        g_engineShadowShown,
                        g_lampLightCount, lamp_upload_max(), g_localLightSlotCount,
                        // LIVE values, not the published copies: NWN calls
                        // Scene::Render several times per frame and the publish
                        // /reset runs on every one, so the GUI scenes zeroed
                        // these before the area scene could report them. The
                        // replay counters beside this print live for the same
                        // reason.
                        g_localDupMs, g_localDupDraws,
                        g_replayCallsFrame, g_replayDrawsFrame,
                        dHit, dMove, dTurn, dSun, dScn, dOff);
                // Which bucket the draws are actually IN, both sides. "engine"
                // is what NWN drew for the screen; "replay" is what we re-drew
                // as shadow casters. A bucket present in engine but absent from
                // replay is already excluded from casting.
                // "<bucket>:<draws>@<indices per draw>" -- ~6 is a particle
                // quad, hundreds is real geometry.
                char eb[256]; char rb[256]; int eo = 0, ro = 0;
                for (int b = 0; b < kBucketCount; ++b) {
                    if (g_engineBucketDraws[b] && eo < (int)sizeof(eb) - 24)
                        eo += snprintf(eb + eo, sizeof(eb) - eo, "%d:%u@%u ",
                                       b, g_engineBucketDraws[b],
                                       g_engineBucketIdx[b] / g_engineBucketDraws[b]);
                    if (g_replayBucketDraws[b] && ro < (int)sizeof(rb) - 24)
                        ro += snprintf(rb + ro, sizeof(rb) - ro, "%d:%u@%u ",
                                       b, g_replayBucketDraws[b],
                                       g_replayBucketIdx[b] / g_replayBucketDraws[b]);
                }
                if (!eo) snprintf(eb, sizeof(eb), "(none)");
                if (!ro) snprintf(rb, sizeof(rb), "(none)");
                fprintf(stderr, "[shadowmap][buckets] engine %s| replay %s\n", eb, rb);
                // Quad-sized draws and the programs issuing them. A blob
                // shadow is one quad per object, so this is where they show.
                char qb[224]; int qo = 0;
                for (int bk = 0; bk < kBucketCount; ++bk) {
                    if (!g_quadDraws[bk] || qo >= (int)sizeof(qb) - 40) continue;
                    qo += snprintf(qb + qo, sizeof(qb) - qo, "%d:%u(prog", bk, g_quadDraws[bk]);
                    for (int i = 0; i < kQuadProgSlots && g_quadProg[bk][i]; ++i)
                        qo += snprintf(qb + qo, sizeof(qb) - qo, " %u", g_quadProg[bk][i]);
                    qo += snprintf(qb + qo, sizeof(qb) - qo, ") ");
                }
                if (!qo) snprintf(qb, sizeof(qb), "(none)");
                fprintf(stderr, "[shadowmap][quads] %s\n", qb);
                char nb[128]; int no = 0;
                for (int i = 0; i < kQuadProgSlots && g_progNoBucket[i]; ++i)
                    no += snprintf(nb + no, sizeof(nb) - no, " %u", g_progNoBucket[i]);
                if (!no) snprintf(nb, sizeof(nb), " (none)");
                fprintf(stderr, "[shadowmap][nobucket] draws=%u quads=%u idx=%u "
                                "prog:%s\n",
                        g_drawsNoBucketShown, g_quadNoBucketShown,
                        g_idxNoBucketShown, nb);
            }
        }
        // PUBLISH ONLY A FINISHED FRAME. Without the glFinish these are
        // submission times, and the panel then reports a fill-rate bound pass as
        // "receiver 0.07" while it is in fact the most expensive thing on
        // screen -- the exact misreading that sent the Windows investigation
        // after the wrong subsystem for days. Updating every ~30 main scenes
        // with a true number beats updating every frame with a false one.
        if (g_costFinishThisFrame) {
            g_msReceiverShown = g_msReceiver;   g_msWorldMapShown  = g_msWorldMap;
            g_msReplayShown   = g_msReplay;     g_msSceneCopyShown = g_msSceneCopy;
            g_frameDrawCallsShown = g_frameDrawCalls;
        }
        g_msReceiver = g_msWorldMap = g_msReplay = g_msSceneCopy = 0.0;
        g_frameDrawCalls = 0;
        g_drawsNoBucketShown = g_drawsNoBucket;
        g_quadNoBucketShown  = g_quadDrawsNoBucket;
        g_idxNoBucketShown   = g_idxNoBucket;
        g_drawsNoBucket = g_quadDrawsNoBucket = g_idxNoBucket = 0;
        g_engineShadowShown = g_engineShadowDraws;
        g_engineShadowDraws = 0;
        g_uniformMat4Shown = g_uniformMat4Calls;
        g_useProgramShown  = g_useProgramCalls;
        g_uniformMat4Calls = g_useProgramCalls = 0;
        g_localDupMs = 0.0;
        g_localDupDraws = 0;
        g_replayCallsShown = g_replayCallsFrame;
        g_replayDrawsShown = g_replayDrawsFrame;
        g_replayCallsFrame = g_replayDrawsFrame = 0;
        for (int b = 0; b < kBucketCount; ++b) {
            g_engineBucketDraws[b] = g_replayBucketDraws[b] = 0;
            g_engineBucketIdx[b]   = g_replayBucketIdx[b]   = 0;
            g_quadDraws[b] = 0;
        }
        // OIT module (nwn_oit.cpp, NWN_OIT=1; a no-op otherwise). Placed AFTER
        // the receiver on purpose: that is the minimum-interference slot, so
        // the validated shadow receiver sees exactly the framebuffer state it
        // saw before this module existed. Whether transparency should instead
        // composite BEFORE the receiver is a real open question -- see the
        // ordering note at the top of nwn_oit.cpp -- but it is an experiment to
        // run once there is real geometry in those buffers, not a guess to bake
        // in now. Before the overlay, which must stay last.
        //
        // THE scene == g_areaScene GATE IS NOT OPTIONAL, and shipping without
        // it is what the Phase 1 pipe test caught: Scene::Render fires several
        // times per frame (world, skybox, UI, portraits, and the main menu's
        // own scenes), so an ungated composite runs once per CALL. The single
        // transparent layer then stacks -- five passes at alpha 0.35 composite
        // to 1-0.65^5 = 0.88 -- and it ran over the Load Game menu, where there
        // is no area at all. Same gate as the receiver and the overlay.
        if (self == g_areaScene) nwn_oit_frame(self);
        // The overlay is intentionally later than the receiver so it is never
        // copied into the scene-depth texture or interpreted as a shadow
        // receiver.  It shares the same selected-area/FBO gate.
        draw_shadow_overlay(self);
        // The local-light capture MUST stay after the receiver. Running it
        // first was tried and measured: the receiver's scene-depth copy then
        // came back completely empty (`range=4294967295..4294967295,
        // non-far=0/1931520`, no GL error), so its background test discarded
        // every pixel and BOTH sun and local shadows vanished. Something in
        // this pass leaves the default framebuffer unreadable by
        // glCopyTexSubImage2D for the rest of the frame; the engine re-renders
        // the scene next frame, so running afterwards avoids it entirely.
        // Consequence: the receiver consumes the PREVIOUS frame's local map,
        // which is harmless here -- the light's view/projection is world-space
        // and does not depend on the camera, so a one-frame-old map is still
        // correctly aligned (see the serial tolerance in draw_static_receiver).
        // The cube proof has its own receiver and its own real-dynamic
        capture_local_light_shadow(self);
        // The cube experiment is now populated during the normal dynamic
        // submission stage.  Do not run the old after-frame bucket replay here:
        // it would replace that real dynamic depth with six empty maps.
        if (g_localLightDumpAt >= 0 && now_seconds() >= g_localLightDumpDeadline) {
            g_localLightDumpAt = -1;   // one shot
            dump_local_light_depth("shadowmap_local_light.pgm");
        }
        trace_scene_exit(self);
        return;
    }

    if (!g_created) create_target();

    bool doProbe = false;
    if (g_probeArmed && g_usable && eng::SceneRenderDrawBucket) {
        double t = now_seconds();
        if (t >= g_nextAttempt) { doProbe = true; ++g_probeAttempt; }
    }

    std::vector<int> before;
    if (doProbe) {
        fprintf(stderr, "\n[shadowmap] ===== bucket probe attempt %d/%d "
                        "(frame %ld) =====\n", g_probeAttempt, g_probeTries, g_frames);
        before = probe_side(self, "before");
    }

    // Latch the world camera while the view matrix is still guaranteed to be
    // the world one -- i.e. before the engine's own passes overwrite it.
    {
        Vec3f e;
        if (eye_from_view(e)) { g_worldEye = e; g_haveWorldEye = true; }
        const float* cv = mtx_entry(MTX_VIEW);
        if (cv) { std::memcpy(g_camView, cv, sizeof(g_camView)); g_haveCamView = true; }

        // Log each distinct (Scene*, eye) pairing once. If the skybox pass is a
        // different Scene object, filtering it out is exact rather than
        // heuristic; if it is the SAME object, we need a different discriminator.
        static void* seen[8]; static int nseen = 0;
        bool known = false;
        for (int i = 0; i < nseen; ++i) if (seen[i] == self) { known = true; break; }
        if (!known && nseen < 8) {
            seen[nseen++] = self;
            fprintf(stderr, "[shadowmap] Scene::Render caller #%d self=%p "
                            "eye=(%.1f %.1f %.1f)\n",
                    nseen, self, e.x, e.y, e.z);
        }
    }
    install_useprogram_patch();
    install_uniform_matrix_patch();
    install_shadersource_patch();
    install_geometry_trace_patch();
    build_gpu_decode_program();

    {   // classify this Scene::Render invocation by viewport size
        GLint vp[4] = {0,0,0,0};
        if (gl::GetIntegerv) gl::GetIntegerv(GL_VIEWPORT, vp);
        if (vp[2] > g_maxVpWidth) g_maxVpWidth = vp[2];
        g_isWorldScene = (g_maxVpWidth > 0 && vp[2] >= (g_maxVpWidth * 9) / 10);
        // A loaded area camera is close to the terrain; the menu/portrait
        // camera is the characteristic z=742. Only promote a Scene after
        // both conditions hold, so startup remains able to discover it.
        if (g_isWorldScene && g_haveWorldEye && std::fabs(g_worldEye.z) < 200.0f) {
            if (g_areaScene != self) {
                g_areaScene = self;
                reset_engine_shadow_light_anchor();
                fprintf(stderr, "[shadowmap] selected area Scene=%p eye.z=%.1f\n",
                        self, g_worldEye.z);
            }
        }
    }

    if (g_mtxDumpAt >= 0 && now_seconds() >= g_mtxDeadline) {
        g_mtxDumpAt = -1;
        dump_matrix_stack("inside Scene::Render, before orig");
    }

    if (!g_lightPass)           shadow_prepass();          // Phase 1 behaviour
    else if (!g_afterOrig)      render_from_light(self);   // Phase 3, early

    // Scene::Render's trampoline is known good (Phase 1/2), but never trust it
    // blindly -- a null trampoline is exactly how this crashed before.
    void* tramp = subhook_get_trampoline(g_hook);
    g_renderingScene = self;
    g_inSceneRender = true;
    if (tramp) reinterpret_cast<eng::SceneRender_t>(tramp)(self);
    else       CALL_ORIGINAL(g_hook, eng::SceneRender, self);
    g_inSceneRender = false;
    g_renderingScene = nullptr;

    if (g_lightPass && g_afterOrig) {
        render_from_light(self);
        draw_fullscreen_depth_receiver(self);
    }

    if (doProbe) {
        std::vector<int> after;
        if (!g_probeFaulted) after = probe_side(self, "after ");
        if (probe_finish(self, before, after)) g_probeArmed = false;
        else                                    g_nextAttempt = now_seconds() + g_probeEvery;
        fprintf(stderr, "[shadowmap] ===== END ATTEMPT =====\n\n");
    }

    if (g_dumpAt >= 0 && now_seconds() >= g_dumpDeadline) {
        g_dumpAt = -1;                       // one shot
        SavedState st = bind_target(false);
        dump_depth("shadowmap_dump.pgm");
        restore_state(st);
    }
}

// ===========================================================================
//  Load / unload
// ===========================================================================
__attribute__((constructor))
static void shadowmap_init() {
    fprintf(stderr, "[shadowmap] loading current renderer hooks...\n");
#ifndef _WIN32
    // WHO ELSE IS IN THIS PROCESS? The maintainer has a second injector for
    // this game (the alphasort/OIT experiment), and anything hooking the same
    // render path could reorder or swallow draw buckets -- which is exactly the
    // shape of "bucket 2 draws nothing when called on its own". Print the list
    // rather than wonder about it.
    if (const char* pre = getenv("LD_PRELOAD"))
        fprintf(stderr, "[shadowmap] LD_PRELOAD=%s\n", pre);
    else
        fprintf(stderr, "[shadowmap] LD_PRELOAD unset (only this injector)\n");
#endif
#if NWN_SHIP
    // Both shipping builds carry the table, so this is not a Windows fact any
    // more. It also confirms in the log that a launcher-less run is configured.
    fprintf(stderr, "[shadowmap] %u built-in defaults available; anything set in "
                    "the environment overrides them\n", shadow_default_env_count());
#endif

    if (shadow_getenv("NWN_SHADOWMAP_OFF")) {
        fprintf(stderr, "[shadowmap] NWN_SHADOWMAP_OFF set; not installing.\n");
        return;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_SIZE")) {
        int v = atoi(s);
        if (v >= 256 && v <= 8192) g_size = v;
        else fprintf(stderr, "[shadowmap] ignoring NWN_SHADOWMAP_SIZE=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_BUCKETS")) {
        int v = atoi(s);
        if (v > 0 && v <= 256) g_nBuckets = v;
    }
#ifdef _WIN32
    install_windows_gl_interposition();
#endif
    g_traceEnabled = shadow_getenv("NWN_SHADOWMAP_TRACE") != nullptr;
    if (const char* cv = shadow_getenv("NWN_SHADOWMAP_COST")) g_costReport = (atoi(cv) != 0);
    g_localLightTrace = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_TRACE") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LAMP_CENSUS")) g_lampCensus = (atoi(s) != 0);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_ALL_BUCKETS"))
        g_localLightDynamicOnly = (atoi(s) == 0);
    if (g_localLightTrace && !g_traceEnabled) {
        fprintf(stderr,"[shadowmap][local-light] NWN_SHADOWMAP_LOCAL_LIGHT_TRACE requires NWN_SHADOWMAP_TRACE=1; disabled\n");
        g_localLightTrace = false;
    }
    g_localLightCapture = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE") != nullptr;
    if (g_localLightCapture && !g_localLightTrace) {
        fprintf(stderr,"[shadowmap][local-light] NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE requires "
                        "NWN_SHADOWMAP_LOCAL_LIGHT_TRACE=1 (it supplies the selected light); disabled\n");
        g_localLightCapture = false;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LOCAL_CUBE_UPDATE_MS")) {
        const double ms = atof(s);
        if (ms >= 16.0 && ms <= 2000.0)
        {
            // Keep the env A/B switch but quantise it to the same panel
            // presets.  Otherwise settings_tick would silently overwrite an
            // arbitrary ms value on the first rendered frame.
            int nearest = 0;
            double best = DBL_MAX;
            for (int i = 0; i < kLocalCubeQualityCount; ++i) {
                const double delta = std::fabs(ms - kLocalCubeCadenceSeconds[i] * 1000.0);
                if (delta < best) { best = delta; nearest = i; }
            }
            g_localCubeQuality = nearest;
            g_localCubeUpdateSeconds = kLocalCubeCadenceSeconds[nearest];
        }
        else
            fprintf(stderr, "[shadowmap][local-light] ignoring NWN_SHADOWMAP_LOCAL_CUBE_UPDATE_MS=%s (16..2000)\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_SIZE")) {
        int v = atoi(s);
        // Upper bound follows the panel's ladder, which now reaches 8192.
        if (v >= 128 && v <= 8192) { g_localLightCaptureSize = v; g_pendingLocalSize = v; }
        else fprintf(stderr, "[shadowmap][local-light] ignoring NWN_SHADOWMAP_LOCAL_LIGHT_SIZE=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_DUMP")) g_localLightDumpAt = atol(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_DIR")) {
        float x=0,y=0,z=0;
        if (sscanf(s,"%f,%f,%f",&x,&y,&z) == 3 &&
            (std::fabs(x)+std::fabs(y)+std::fabs(z)) > 1e-4f) {
            g_localLightDir[0]=x; g_localLightDir[1]=y; g_localLightDir[2]=z;
        } else fprintf(stderr,"[shadowmap][local-light] ignoring NWN_SHADOWMAP_LOCAL_LIGHT_DIR=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_MAX_LAMPS")) {
        int v = atoi(s);
        if (v >= 1 && v <= (int)kMaxLampLights) g_lampUploadMax = v;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LIFT_THRESHOLD")) {
        float v = (float)atof(s);
        if (std::isfinite(v) && v >= 0.0f && v <= 1.0f) g_liftThreshold = v;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_LIFT")) {
        float v = (float)atof(s);
        if (std::isfinite(v) && v >= 0.0f && v <= 1.0f) g_localLightLift = v;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_FOV")) {
        float v = (float)atof(s);
        if (std::isfinite(v) && v >= 20.0f && v <= 175.0f) g_localLightFovDeg = v;
        else fprintf(stderr,"[shadowmap][local-light] ignoring NWN_SHADOWMAP_LOCAL_LIGHT_FOV=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_RECEIVER_DEBUG")) {
        int v = atoi(s);
        if (v >= 0 && v <= 5) g_receiverDebug = v;
        else fprintf(stderr,"[shadowmap][static] ignoring NWN_SHADOWMAP_RECEIVER_DEBUG=%s\n", s);
        if (g_receiverDebug)
            fprintf(stderr,"[shadowmap][static] RECEIVER DEBUG %d active: %s\n", g_receiverDebug,
                    g_receiverDebug==1 ? "solid green over all scene pixels (after the background test)"
                  : g_receiverDebug==2 ? "red=sun shade, green=local shade (after the background test)"
                                       : "solid magenta BEFORE any sampling/discard (does this pass reach the screen at all?)");
    }
    g_overlayLegacy = shadow_getenv("NWN_SHADOWMAP_OVERLAY_LEGACY") != nullptr;
    if (g_overlayLegacy)
        fprintf(stderr,"[shadowmap][overlay] legacy bitmap overlay selected (ImGui panel disabled)\n");
    if (shadow_getenv("NWN_SHADOWMAP_OVERLAY_NO_INPUT_CAPTURE")) {
        g_overlayInputCapture = false;
        fprintf(stderr,"[shadowmap][overlay] input capture DISABLED; clicks on the panel "
                       "will also reach the game\n");
    }
    g_localLightReceiver = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_RECEIVER") != nullptr;
    if (g_localLightReceiver && !g_localLightCapture) {
        fprintf(stderr,"[shadowmap][local-light] NWN_SHADOWMAP_LOCAL_LIGHT_RECEIVER requires "
                        "NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE=1 (it supplies the depth map); disabled\n");
        g_localLightReceiver = false;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_STRENGTH")) {
        float v = (float)atof(s);
        if (std::isfinite(v) && v >= 0.0f && v <= 1.0f) g_localLightStrength = v;
        else fprintf(stderr,"[shadowmap][local-light] ignoring NWN_SHADOWMAP_LOCAL_LIGHT_STRENGTH=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LOCAL_LIGHT_BIAS")) {
        float v = (float)atof(s);
        if (std::isfinite(v) && v >= 0.0f && v <= 0.05f) g_localLightBias = v;
        else fprintf(stderr,"[shadowmap][local-light] ignoring NWN_SHADOWMAP_LOCAL_LIGHT_BIAS=%s\n", s);
    }
    g_traceRelaxAreaViewport =
        shadow_getenv("NWN_SHADOWMAP_TRACE_RELAX_AREA_VIEWPORT") != nullptr;
    g_casterCullTrace = shadow_getenv("NWN_SHADOWMAP_CASTER_CULL_TRACE") != nullptr;
    g_casterFullBspTrace = shadow_getenv("NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE") != nullptr;
    g_casterFullBspNativeSubmit =
        shadow_getenv("NWN_SHADOWMAP_CASTER_FULL_BSP_NATIVE_SUBMIT") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASTER_CULL_TRACE_MAX")) {
        int v = atoi(s);
        if (v > 0 && v <= 4096) g_casterCullTraceMax = (unsigned)v;
        else fprintf(stderr, "[shadowmap][cull] ignoring NWN_SHADOWMAP_CASTER_CULL_TRACE_MAX=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE_MAX")) {
        int v = atoi(s);
        if (v > 0 && v <= 256) g_casterFullBspTraceMax = (unsigned)v;
        else fprintf(stderr, "[shadowmap][fullbsp] ignoring NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE_MAX=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_TRACE_FRAMES")) {
        int v = atoi(s);
        if (v > 0 && v <= 3600) g_traceFramesMax = (unsigned)v;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_TRACE_EVENTS")) {
        int v = atoi(s);
        if (v > 0 && v <= 100000) g_traceEventsMax = (unsigned)v;
    }
    g_cascadeMathTrace = shadow_getenv("NWN_SHADOWMAP_CASCADE_MATH") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASCADE_LAMBDA")) {
        float v = (float)atof(s);
        if (std::isfinite(v) && v >= 0.0f && v <= 1.0f) g_cascadeLambda = v;
        else fprintf(stderr, "[shadowmap][csm] ignoring NWN_SHADOWMAP_CASCADE_LAMBDA=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASCADE_RESOLUTION")) {
        int v = atoi(s);
        if (v >= 256 && v <= 8192) g_cascadeMathResolution = v;
        else fprintf(stderr, "[shadowmap][csm] ignoring NWN_SHADOWMAP_CASCADE_RESOLUTION=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASCADE_LOG_FRAMES")) {
        int v = atoi(s);
        if (v > 0 && v <= 90) g_cascadeMathLogFrames = (unsigned)v;
    }
    g_cascadeTargetValidate =
        shadow_getenv("NWN_SHADOWMAP_CASCADE_TARGET_VALIDATE") != nullptr;
    g_cascadeGeometryTrace =
        shadow_getenv("NWN_SHADOWMAP_CASCADE_GEOMETRY_TRACE") != nullptr;
    g_cascadeMatrixTrace =
        shadow_getenv("NWN_SHADOWMAP_CASCADE_MATRIX_TRACE") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASCADE_MATRIX_TRACE_MAX")) {
        int v = atoi(s);
        if (v > 0 && v <= 512) g_cascadeMatrixTraceMax = (unsigned)v;
        else fprintf(stderr, "[shadowmap][mat] ignoring NWN_SHADOWMAP_CASCADE_MATRIX_TRACE_MAX=%s\n", s);
    }
    g_cascadeCameraCapture =
        shadow_getenv("NWN_SHADOWMAP_CASCADE_CAMERA_CAPTURE") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASCADE_CAMERA_BUCKET")) {
        int v = atoi(s);
        if (v >= 0 && v <= 255) g_cascadeCameraCaptureBucket = v;
        else fprintf(stderr, "[shadowmap][capture] ignoring NWN_SHADOWMAP_CASCADE_CAMERA_BUCKET=%s\n", s);
    }
    g_cascadeLightCapture =
        shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_CAPTURE") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_DUMP_PGM"))
        g_dumpCapturePgm = (atoi(s) != 0);
    g_cascadeMultiLayerCapture =
        shadow_getenv("NWN_SHADOWMAP_CASCADE_MULTI_CAPTURE") != nullptr;
    g_cascadeBucketReplay =
        shadow_getenv("NWN_SHADOWMAP_CSM_BUCKET_REPLAY") != nullptr;
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_CSM_CASCADES")) {
        int v = atoi(sv);
        if (v >= 1 && v <= kCascadeCount) g_cascadeActiveCount = v;
        else fprintf(stderr, "[shadowmap][csm] ignoring NWN_SHADOWMAP_CSM_CASCADES=%s\n", sv);
    }
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_CSM_DISTANCE")) {
        float v = (float)atof(sv);
        if (std::isfinite(v) && v >= 0.0f && v <= 10000.0f) g_cascadeMaxDistance = v;
        else fprintf(stderr, "[shadowmap][csm] ignoring NWN_SHADOWMAP_CSM_DISTANCE=%s\n", sv);
    }
    // VALUE-BASED, and it has to be. This was presence-only, so run-dev.sh
    // passing NWN_SHADOWMAP_STATIC_WORLD=0 still evaluated TRUE (the variable
    // exists) and Linux ran with the world map on -- while a shipping build,
    // which passes no environment at all, got FALSE and ran without it. The
    // declared default says "always on"; it had never once applied.
    // run-dev.sh's own default moves to 1 in the same change, so Linux keeps
    // the exact behaviour it has been developed and tested against.
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_STATIC_WORLD"))
        g_staticWorldEnabled = (atoi(s) != 0);
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_STATIC_WORLD_SIZE")) {
        int v = atoi(sv);
        if (v >= 512 && v <= 16384) g_staticWorldSize = v;
        else fprintf(stderr, "[shadowmap][world] ignoring NWN_SHADOWMAP_STATIC_WORLD_SIZE=%s "
                             "(allowed 512..16384)\n", sv);
    }
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_STATIC_WORLD_EXTENT")) {
        float v = (float)atof(sv);
        if (std::isfinite(v) && v >= 16.0f && v <= 4096.0f) g_staticWorldExtent = v;
    }
    if (g_staticWorldEnabled)
        fprintf(stderr, "[shadowmap][world] world-anchored static map ENABLED: %d^2 over "
                        "%.0f units (%.1f cm/texel); static casters render once per area\n",
                g_staticWorldSize, g_staticWorldExtent * 2.0f,
                (2.0f * g_staticWorldExtent / (float)g_staticWorldSize) * 100.0f);
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_FOG_FADE"))
        g_fogFade = (atoi(sv) != 0);
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_FOG_START")) g_fogStart = (float)atof(sv);
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_FOG_END"))   g_fogEnd   = (float)atof(sv);
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_STATIC_NEAR_CASCADES")) {
        int v = atoi(sv);
        if (v >= 0 && v <= kCascadeCount) g_staticNearCascades = v;
    }
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_FULL_BSP_SUBMIT"))
        g_fullBspEnabled = (atoi(sv) != 0);
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_CSM_DYNAMIC_CASCADES")) {
        int v = atoi(sv);
        if (v >= 0 && v <= kCascadeCount) g_cascadeDynamicLayers = v;
    }
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_CSM_STATIC_CACHE"))
        g_cascadeStaticCache = (atoi(sv) != 0);
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_CSM_CACHE_MOVE")) {
        float v = (float)atof(sv);
        if (std::isfinite(v) && v >= 0.0f && v <= 64.0f) g_cascadeCacheMove = v;
    }
    if (const char* sv = shadow_getenv("NWN_SHADOWMAP_CSM_CACHE_TURN")) {
        float v = (float)atof(sv);
        if (std::isfinite(v) && v >= 0.0f && v <= 1.0f) g_cascadeCacheTurn = v;
    }
    fprintf(stderr, "[shadowmap][csm] static cascade cache %s (refit after %.1f units "
                    "or cos(turn)<%.4f)\n",
            g_cascadeStaticCache ? "ON" : "off", g_cascadeCacheMove, g_cascadeCacheTurn);
    if (g_cascadeActiveCount != kCascadeCount || g_cascadeMaxDistance > 0.0f)
        fprintf(stderr, "[shadowmap][csm] cascades=%d shadow-distance=%s (replay cost scales "
                        "with casters x cascades)\n", g_cascadeActiveCount,
                g_cascadeMaxDistance > 0.0f ? "capped" : "camera far");
    g_cascadeStaticReceiver =
        shadow_getenv("NWN_SHADOWMAP_STATIC_RECEIVER") != nullptr;
    g_cascadeCsmStaticReceiver =
        shadow_getenv("NWN_SHADOWMAP_CSM_STATIC_RECEIVER") != nullptr;
    g_cascadeCsmDynamicReceiver =
        shadow_getenv("NWN_SHADOWMAP_CSM_DYNAMIC_RECEIVER") != nullptr;
    g_cascadeCsmAlphaReceiver =
        shadow_getenv("NWN_SHADOWMAP_CSM_ALPHA_RECEIVER") != nullptr;
    // Presence-only reads cannot express "leave the default alone", which is
    // how this silently pinned itself off on every build without the env var.
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CSM_COMPOSITE"))
        g_cascadeCompositeShadows = (atoi(s) != 0);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_AREA_SHADOW_FLAGS"))
        g_areaShadowFlagsEnabled = atoi(s) != 0;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_AREA_SHADOW_OPACITY"))
        g_areaShadowOpacityApply = atoi(s) != 0;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_AREA_SHADOW_POLICY"))
        g_areaShadowFlagsApply = atoi(s) != 0;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_AREA_SHADOW_FADE")) {
        const float v = strtof(s, nullptr);
        if (std::isfinite(v) && v >= 0.0f && v <= 10.0f) g_areaShadowFadeSeconds = v;
        else fprintf(stderr, "[shadowmap][area] ignoring NWN_SHADOWMAP_AREA_SHADOW_FADE=%s (expected 0..10 seconds)\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CSM_STRENGTH")) {
        const float v = strtof(s, nullptr);
        if (v >= 0.0f && v <= 1.0f) g_cascadeCompositeStrength = v;
        else fprintf(stderr, "[shadowmap][csm] ignoring NWN_SHADOWMAP_CSM_STRENGTH=%s (expected 0..1)\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CSM_BIAS")) {
        const float v = strtof(s, nullptr);
        if (std::isfinite(v) && v >= 0.0f && v <= 0.05f) g_cascadeReceiverBias = v;
        else fprintf(stderr, "[shadowmap][csm] ignoring NWN_SHADOWMAP_CSM_BIAS=%s (expected 0..0.05)\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CSM_BLEND")) {
        const float v = strtof(s, nullptr);
        if (std::isfinite(v) && v >= 0.0f && v <= 10.0f) g_cascadeBlendWidth = v;
        else fprintf(stderr, "[shadowmap][csm] ignoring NWN_SHADOWMAP_CSM_BLEND=%s (expected 0..10 world units)\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CSM_PCF_RADIUS")) {
        const float v = strtof(s, nullptr);
        if (std::isfinite(v) && v >= 0.0f && v <= 4.0f) g_cascadePcfRadius = v;
        else fprintf(stderr, "[shadowmap][csm] ignoring NWN_SHADOWMAP_CSM_PCF_RADIUS=%s (expected 0..4 texels)\n", s);
    }
    g_cascadeDynamicReceiver =
        shadow_getenv("NWN_SHADOWMAP_DYNAMIC_RECEIVER") != nullptr;
    g_cascadeDynamicCharacterCapture =
        shadow_getenv("NWN_SHADOWMAP_DYNAMIC_CHARACTER_CAPTURE") != nullptr;
    g_cascadeDynamicBucketCapture =
        shadow_getenv("NWN_SHADOWMAP_DYNAMIC_BUCKET_CAPTURE") != nullptr;
    g_cascadeAlphaReceiver =
        shadow_getenv("NWN_SHADOWMAP_ALPHA_RECEIVER") != nullptr;
    g_cascadeStaticAlphaCapture =
        shadow_getenv("NWN_SHADOWMAP_STATIC_ALPHA_CAPTURE") != nullptr;
    g_cascadeStaticAlphaReceiver =
        shadow_getenv("NWN_SHADOWMAP_STATIC_ALPHA_RECEIVER") != nullptr;
    g_cascadeAlphaCardCapture =
        shadow_getenv("NWN_SHADOWMAP_ALPHA_CARD_CAPTURE") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_DYNAMIC_BUCKET")) {
        const int v = atoi(s);
        if (v >= 0 && v <= 255) g_cascadeDynamicBucket = v;
        else fprintf(stderr, "[shadowmap][dynamic] ignoring NWN_SHADOWMAP_DYNAMIC_BUCKET=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_ALPHA_BUCKET")) {
        const int v = atoi(s);
        if (v >= 0 && v <= 255) g_cascadeAlphaBucket = v;
        else fprintf(stderr, "[shadowmap][alpha] ignoring NWN_SHADOWMAP_ALPHA_BUCKET=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_STATIC_ALPHA_BUCKET")) {
        const int v = atoi(s);
        if (v >= 0 && v <= 255) g_cascadeStaticAlphaBucket = v;
        else fprintf(stderr, "[shadowmap][alpha] ignoring NWN_SHADOWMAP_STATIC_ALPHA_BUCKET=%s\n", s);
    }
    g_cascadeLightCaptureAllBuckets =
        shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_ALL_BUCKETS") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET")) {
        int v = atoi(s);
        if (v >= 0 && v <= 255) g_cascadeLightCaptureBucket = v;
        else fprintf(stderr, "[shadowmap][lightcap] ignoring NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_BUCKETS")) {
        g_cascadeLightCaptureBuckets.clear();
        for (const char* p = s; *p; ) {
            const int v = atoi(p);
            if (v >= 0 && v <= 255 &&
                std::find(g_cascadeLightCaptureBuckets.begin(),
                          g_cascadeLightCaptureBuckets.end(), v) ==
                    g_cascadeLightCaptureBuckets.end())
                g_cascadeLightCaptureBuckets.push_back(v);
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
        if (g_cascadeLightCaptureBuckets.empty())
            fprintf(stderr, "[shadowmap][lightcap] ignoring empty NWN_SHADOWMAP_CASCADE_LIGHT_BUCKETS=%s\n", s);
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_LAYER")) {
        int v = atoi(s);
        if (v >= 0 && v < kCascadeCount) g_cascadeLightCaptureLayer = v;
        else fprintf(stderr, "[shadowmap][lightcap] ignoring NWN_SHADOWMAP_CASCADE_LIGHT_LAYER=%s\n", s);
    }
    if (g_cascadeStaticReceiver) {
        // Keep the first visible receiver slice intentionally minimal and
        // reproducible: opaque area bucket 0, layer 2, no dynamic stage and
        // no inherited all-bucket launcher setting.  Broader caster coverage
        // comes only after this camera-stability proof.
        g_cascadeLightCapture = true;
        g_cascadeLightCaptureAllBuckets = false;
        g_cascadeLightCaptureBuckets.clear();
        if (!shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET"))
            g_cascadeLightCaptureBucket = 0;
        if (!shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_LAYER"))
            g_cascadeLightCaptureLayer = 2;
    }
    if (g_cascadeDynamicReceiver) {
        // The combined receiver is deliberately an extension of the proven
        // static path, never a stand-alone all-bucket replay.  It duplicates
        // only bucket 2 into the independent dynamic array in the same frozen
        // frame as static bucket 0.
        if (!g_cascadeStaticReceiver) {
            fprintf(stderr, "[shadowmap][dynamic] dynamic receiver requires "
                            "NWN_SHADOWMAP_STATIC_RECEIVER=1; disabled\n");
            g_cascadeDynamicReceiver = false;
        } else {
            g_cascadeLightCapture = true;
            g_cascadeLightCaptureAllBuckets = false;
            g_cascadeLightCaptureBuckets.clear();
            if (!shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_LAYER"))
                g_cascadeLightCaptureLayer = 2;
        }
    }
    if (g_cascadeAlphaReceiver) {
        // Phase 3p is deliberately an extension of Phase 3l.  Bucket 3 was
        // validated alone first, including the custom hair discard; it may not
        // promote itself to an all-bucket or static-target replay.
        if (!g_cascadeDynamicReceiver) {
            fprintf(stderr, "[shadowmap][alpha] alpha receiver requires "
                            "NWN_SHADOWMAP_DYNAMIC_RECEIVER=1 (and static receiver); disabled\n");
            g_cascadeAlphaReceiver = false;
        }
    }
    if (g_cascadeStaticAlphaReceiver) {
        // Extend only the proven static receiver target with the separately
        // validated foliage bucket.  This is never an all-bucket replay.
        if (!g_cascadeStaticReceiver) {
            fprintf(stderr, "[shadowmap][alpha] static alpha receiver requires "
                            "NWN_SHADOWMAP_STATIC_RECEIVER=1; disabled\n");
            g_cascadeStaticAlphaReceiver = false;
        } else {
            g_cascadeLightCapture = true;
            g_cascadeLightCaptureAllBuckets = false;
            g_cascadeLightCaptureBuckets.clear();
            if (!shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_LAYER"))
                g_cascadeLightCaptureLayer = 2;
        }
    }
    if (g_cascadeCsmStaticReceiver) {
        // Phase 4f is the visible consumer of the Phase 4e data contract.
        // Do not let it silently fall back to a single fixed layer.
        if (!g_cascadeStaticReceiver) {
            fprintf(stderr, "[shadowmap][csm] static CSM receiver requires "
                            "NWN_SHADOWMAP_STATIC_RECEIVER=1; disabled\n");
            g_cascadeCsmStaticReceiver = false;
        } else {
            g_cascadeMultiLayerCapture = true;
            g_cascadeLightCapture = true;
            g_cascadeLightCaptureAllBuckets = false;
            g_cascadeLightCaptureBuckets.clear();
        }
    }
    if (g_cascadeCsmDynamicReceiver) {
        // Phase 4g is deliberately a narrow extension of 4f: static buckets
        // 0+1 plus the already-proven character-body bucket 2.  Dynamic alpha
        // stays off until this four-layer body composite is verified in game.
        if (!g_cascadeCsmStaticReceiver || !g_cascadeStaticReceiver) {
            fprintf(stderr, "[shadowmap][csm] dynamic CSM receiver requires "
                            "NWN_SHADOWMAP_CSM_STATIC_RECEIVER=1 and "
                            "NWN_SHADOWMAP_STATIC_RECEIVER=1; disabled\n");
            g_cascadeCsmDynamicReceiver = false;
        } else {
            g_cascadeDynamicReceiver = true;
            g_cascadeAlphaReceiver = false;
            g_cascadeMultiLayerCapture = true;
            g_cascadeLightCapture = true;
            g_cascadeLightCaptureAllBuckets = false;
            g_cascadeLightCaptureBuckets.clear();
        }
    }
    if (g_cascadeCsmAlphaReceiver) {
        // Phase 4h adds only the already-proven dynamic alpha/card bucket to
        // Phase 4g's body target.  Bucket 2 clears every dynamic layer before
        // bucket 3 contributes, so a missing card draw leaves no stale alpha
        // depth behind.  Do not permit this to become an all-alpha replay.
        if (!g_cascadeCsmDynamicReceiver || !g_cascadeCsmStaticReceiver ||
            !g_cascadeStaticReceiver) {
            fprintf(stderr, "[shadowmap][csm] alpha CSM receiver requires Phase 4g "
                            "(CSM static + dynamic receiver); disabled\n");
            g_cascadeCsmAlphaReceiver = false;
        } else {
            g_cascadeDynamicReceiver = true;
            g_cascadeAlphaReceiver = true;
            g_cascadeMultiLayerCapture = true;
            g_cascadeLightCapture = true;
            g_cascadeLightCaptureAllBuckets = false;
            g_cascadeLightCaptureBuckets.clear();
        }
    }
    if (g_cascadeCompositeShadows) {
        // Phase 5a must remain a terminal presentation choice over a fully
        // validated CSM receiver.  A standalone/broken composite would only
        // darken arbitrary camera depth, so fail closed rather than guessing.
        if (!g_cascadeCsmStaticReceiver || !g_cascadeStaticReceiver) {
            fprintf(stderr, "[shadowmap][csm] composite requires a CSM static receiver; disabled\n");
            g_cascadeCompositeShadows = false;
        }
    }
    // Do not silently combine Phase 4f with the older single-layer dynamic
    // receiver.  That pairing would make the freshness guard reject every
    // area frame, which looks like a broken static CSM diagnostic rather than
    // a useful configuration error.
    if (g_cascadeCsmStaticReceiver && g_cascadeDynamicReceiver &&
        !g_cascadeCsmDynamicReceiver) {
        fprintf(stderr, "[shadowmap][csm] ordinary dynamic receiver is incompatible "
                        "with CSM static mode; set NWN_SHADOWMAP_CSM_DYNAMIC_RECEIVER=1 "
                        "for Phase 4g, otherwise dynamic receiver disabled\n");
        g_cascadeDynamicReceiver = false;
        g_cascadeAlphaReceiver = false;
    }
    if (g_casterFullBspNativeSubmit) {
        // This experiment is an extension of the frozen-context static
        // receiver, not a legacy light-pass fallback. Make every dependency
        // explicit so a hand-written launch command cannot silently do a
        // normal camera replay or leave the private target unallocated.
        if (!g_cascadeStaticReceiver) {
            fprintf(stderr, "[shadowmap][fullsubmit] requires NWN_SHADOWMAP_STATIC_RECEIVER=1; disabled\n");
            g_casterFullBspNativeSubmit = false;
        } else {
            g_cascadeMathTrace = true;
            g_lightVectorTrace = true;
            g_cascadeTargetValidate = true;
            g_cascadeGeometryTrace = true;
            g_cascadeLightCapture = true;
            g_cascadeLightCaptureAllBuckets = false;
            g_cascadeLightCaptureBuckets.clear();
        }
    }
    if (g_cascadeStaticAlphaCapture) {
        // This source-classified test owns only the dynamic scratch target and
        // has no fullscreen composite. Keep it isolated from accepted paths.
        if (g_cascadeStaticReceiver || g_cascadeDynamicReceiver || g_cascadeAlphaReceiver ||
            g_cascadeStaticAlphaReceiver) {
            fprintf(stderr, "[shadowmap][alpha] static-alpha capture is depth-only and cannot share a receiver; disabled\n");
            g_cascadeStaticAlphaCapture = false;
        } else {
            g_cascadeLightCapture = true;
            g_cascadeLightCaptureAllBuckets = false;
            g_cascadeLightCaptureBuckets.clear();
            if (!shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_LAYER"))
                g_cascadeLightCaptureLayer = 2;
        }
    }
    if (g_cascadeMultiLayerCapture) {
        // Phase 4a is depth-only. Phase 4f is its deliberately narrow visible
        // successor and selects one layer from frozen camera-space depth.
        if (!g_cascadeStaticReceiver) {
            fprintf(stderr, "[shadowmap][csm] multi-layer capture requires "
                            "NWN_SHADOWMAP_STATIC_RECEIVER=1; disabled\n");
            g_cascadeMultiLayerCapture = false;
        } else {
            g_cascadeLightCapture = true;
            g_cascadeLightCaptureAllBuckets = false;
            g_cascadeLightCaptureBuckets.clear();
        }
    }
    if (g_cascadeBucketReplay && !g_cascadeMultiLayerCapture) {
        fprintf(stderr, "[shadowmap][csm] NWN_SHADOWMAP_CSM_BUCKET_REPLAY requires the "
                        "multi-layer CSM path (NWN_SHADOWMAP_CSM_STATIC_RECEIVER=1 etc, "
                        "already off or disabled above); no-op\n");
        g_cascadeBucketReplay = false;
    } else if (g_cascadeBucketReplay) {
        fprintf(stderr, "[shadowmap][csm] Phase 6c bucket-level cascade replay ACTIVE: "
                        "each cascade layer binds once per bucket instead of once per "
                        "draw call. A/B against the default (unset) if shadows ever look "
                        "different from the per-draw path.\n");
    }
    if (g_cascadeDynamicCharacterCapture) {
        // This mode captures only Scene::RenderDynamicGeometry into the
        // separate dynamic array.  Do not also arm any area bucket: a PGM
        // must answer whether this named engine stage itself submits the
        // character before we infer a fallback bucket.
        g_cascadeLightCapture = true;
        g_cascadeLightCaptureAllBuckets = false;
        g_cascadeLightCaptureBuckets.clear();
        if (!shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_LAYER"))
            g_cascadeLightCaptureLayer = 2;
    }
    if (g_cascadeDynamicBucketCapture) {
        // This is a diagnostic split of the legacy all-bucket path.  It keeps
        // bucket 2 (the first live post-dynamic candidate) out of static
        // storage and produces one PGM before any receiver composition.
        g_cascadeLightCapture = true;
        g_cascadeLightCaptureAllBuckets = false;
        g_cascadeLightCaptureBuckets.clear();
        if (!shadow_getenv("NWN_SHADOWMAP_CASCADE_LIGHT_LAYER"))
            g_cascadeLightCaptureLayer = 2;
    }
    if (g_cascadeAlphaCardCapture && !g_cascadeDynamicBucketCapture) {
        fprintf(stderr, "[shadowmap][alpha] alpha-card crop requires "
                        "NWN_SHADOWMAP_DYNAMIC_BUCKET_CAPTURE=1; disabled\n");
        g_cascadeAlphaCardCapture = false;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CASCADE_GEOMETRY_BUCKETS")) {
        int v = atoi(s);
        if (v > 0 && v <= 256) g_traceGeometryMaxBuckets = (unsigned)v;
    }
    g_lightVectorTrace = shadow_getenv("NWN_SHADOWMAP_LIGHT_VECTOR_TRACE") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LIGHT_VECTOR_TRACE_MAX")) {
        int v = atoi(s);
        if (v > 0 && v <= 512) g_lightVectorTraceMax = (unsigned)v;
    }
    // The capture is valid only with the same frozen entry context, observed
    // area light, private arrays and resolver-level native draw wrappers used
    // by the preceding trace phases.  Turn on those dependencies here so a
    // direct invocation cannot silently fall back to legacy rendering.
    if (g_cascadeCameraCapture) {
        g_cascadeMathTrace = true;
        g_lightVectorTrace = true;
        g_cascadeTargetValidate = true;
        g_cascadeGeometryTrace = true;
    }
    if (g_cascadeLightCapture) {
        g_cascadeMathTrace = true;
        g_lightVectorTrace = true;
        g_cascadeTargetValidate = true;
        g_cascadeGeometryTrace = true;
        // The two proofs share a layer and intentionally must not compose.
        if (g_cascadeCameraCapture) {
            fprintf(stderr, "[shadowmap][lightcap] disabling camera capture; "
                            "light capture owns private layer 0\n");
            g_cascadeCameraCapture = false;
        }
    }
    g_verbose    = shadow_getenv("NWN_SHADOWMAP_VERBOSE") != nullptr;
    g_probeArmed = shadow_getenv("NWN_SHADOWMAP_PROBE")   != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_DUMP"))        g_dumpAt     = atol(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_PROBE_DELAY")) g_probeDelay = atof(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_PROBE_EVERY")) g_probeEvery = atof(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_PROBE_TRIES")) g_probeTries = atoi(s);
    if (g_probeEvery < 0.5) g_probeEvery = 0.5;
    if (g_probeTries < 1)   g_probeTries = 1;

    // --- Phase 3 ---
    g_lightPass = shadow_getenv("NWN_SHADOWMAP_LIGHT") != nullptr;
    g_injectTest = shadow_getenv("NWN_SHADOWMAP_INJECT_TEST") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_DIR")) {
        sscanf(s, "%f,%f,%f", &g_lightDir[0], &g_lightDir[1], &g_lightDir[2]);
        g_dirOverride = true;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_DIRSIGN")) g_dirSign = (float)atof(s);
    g_noBuckets = shadow_getenv("NWN_SHADOWMAP_NOBUCKETS") != nullptr;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_STATUS")) g_statusEvery = atof(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_CONV"))   g_conv    = atoi(s) & 3;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_EXTENT")) g_extent  = (float)atof(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_DIST"))   g_dist    = (float)atof(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_FOCUS"))  g_focus   = (float)atof(s);
    // Comparison sampling is the safe default. Set NWN_SHADOWMAP_COMPARE=0
    // only for a future raw-depth diagnostic.
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_COMPARE")) g_useCompare = atoi(s) != 0;
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_UNIT")) {
        int v = atoi(s);
        if (v >= 0 && v < 32) SHADOW_UNIT = v;
    }
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_WHEN"))
        g_afterOrig = (strcmp(s, "after") == 0);
    // NOTE: assign only when the variable is PRESENT. The old unconditional
    // form silently clobbered the baked-in default of true with false.
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_ORTHO")) g_useOrtho = (atoi(s) != 0);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_VIEW"))     g_viewSub     = atoi(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_VIEWMODE")) g_viewMtxMode = atoi(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LIGHT_DELAY")) g_lightDelay = atof(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_MTXDUMP"))     g_mtxDumpAt  = atol(s);
    if (const char* s = shadow_getenv("NWN_SHADOWMAP_LIGHTBUCKETS")) {
        g_lightBuckets.clear();
        for (const char* p = s; *p; ) {
            g_lightBuckets.push_back(atoi(p));
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
    }

    // Trace mode wins over inherited shell flags.  The Scene::Render trace
    // branch already bypasses all rendering work, but clearing these prevents
    // the auxiliary matrix/shader hooks from being installed at all.
    if (g_traceEnabled) {
        if (g_lightPass || g_injectTest || shadow_getenv("NWN_SHADOWMAP_FULLSCREEN_RECEIVER"))
            fprintf(stderr, "[shadowmap][trace] ignoring legacy shadow diagnostic flags\n");
        g_lightPass = false;
        g_injectTest = false;
        if (g_cascadeMathTrace)
            fprintf(stderr, "[shadowmap][csm] Phase 3a enabled: frozen-context matrix fit only; no GL rendering\n");
        if (g_lightVectorTrace)
            fprintf(stderr, "[shadowmap][lightvec] Phase 3b enabled: normal-pass vec3 upload trace only\n");
        if (g_cascadeTargetValidate)
            fprintf(stderr, "[shadowmap][csm] Phase 3c enabled: private target allocation "
                            "and all-layer completeness only; no rendering\n");
        if (g_cascadeGeometryTrace)
            fprintf(stderr, "[shadowmap][geom] Phase 3e enabled: bounded normal-pass "
                            "draw census only; no draw redirection\n");
        if (g_casterCullTrace)
            fprintf(stderr, "[shadowmap][cull] Phase 4b enabled: read-only post-BSP "
                            "caster-list census; no draw or GL-state changes\n");
        if (g_casterFullBspTrace)
            fprintf(stderr, "[shadowmap][fullbsp] Phase 4c enabled: read-only full-BSP "
                            "candidate census after normal culling; no draw or GL-state changes\n");
        if (g_casterFullBspNativeSubmit)
            fprintf(stderr, "[shadowmap][fullsubmit] Phase 4d enabled: append complete "
                            "static BSP to NWN's pending native mesh buckets immediately after "
                            "ManageSceneBSP; NWN builds/renders the DrawBucketManager normally\n");
        if (g_cascadeMatrixTrace)
            fprintf(stderr, "[shadowmap][mat] Phase 3g enabled: normal-pass 4x4 "
                            "uniform upload trace only; no substitution\n");
        if (g_cascadeCameraCapture)
            fprintf(stderr, "[shadowmap][capture] Phase 3f enabled: duplicate native "
                            "bucket %d into private camera-depth layer 0 once; "
                            "no light matrix or receiver\n",
                    g_cascadeCameraCaptureBucket);
        if (g_cascadeLightCapture)
            fprintf(stderr, "[shadowmap][lightcap] Phase 3h enabled: duplicate native "
                            "%s into private cascade-%d depth once using exact "
                            "normal m_mv/m_proj recovery; no receiver\n",
                    g_cascadeLightCaptureAllBuckets ? "all area buckets" :
                    g_cascadeLightCaptureBuckets.empty() ? "selected bucket" :
                        "selected buckets",
                    g_cascadeLightCaptureLayer);
        if (g_cascadeStaticReceiver)
            fprintf(stderr, "[shadowmap][static] Phase 3i enabled: opaque static bucket %d "
                            "to private static cascade-%d every area frame, followed by a "
                            "frozen-context fullscreen red shadow diagnostic; no dynamic or alpha casters\n",
                    g_cascadeLightCaptureBucket, g_cascadeLightCaptureLayer);
        if (g_cascadeDynamicReceiver)
            fprintf(stderr, "[shadowmap][dynamic] Phase 3l enabled: bucket %d "
                            "to private dynamic cascade-%d every area frame, "
                            "composed with static bucket %d in one frozen-context "
                            "red diagnostic\n",
                    g_cascadeDynamicBucket, g_cascadeLightCaptureLayer,
                    g_cascadeLightCaptureBucket);
        if (g_cascadeAlphaReceiver)
            fprintf(stderr, "[shadowmap][alpha] Phase 3p enabled: validated alpha bucket %d "
                            "accumulates with dynamic bucket %d in the same private dynamic "
                            "cascade-%d; frozen-context red diagnostic only\n",
                    g_cascadeAlphaBucket, g_cascadeDynamicBucket,
                    g_cascadeLightCaptureLayer);
        if (g_cascadeStaticAlphaReceiver)
            fprintf(stderr, "[shadowmap][alpha] Phase 3r enabled: source-classified static "
                            "AlphaDiscard bucket %d accumulates with opaque static bucket %d "
                            "in private static cascade-%d; frozen-context red diagnostic only\n",
                    g_cascadeStaticAlphaBucket, g_cascadeLightCaptureBucket,
                    g_cascadeLightCaptureLayer);
        if (g_cascadeMultiLayerCapture)
            fprintf(stderr, "[shadowmap][csm] Phase 4a enabled: accepted caster paths "
                            "fan out to all %d static/dynamic layers; receiver=%s; "
                            "writes shadowmap_cascade_{static,dynamic}_c{0..3}.pgm\n",
                    kCascadeCount,
                    g_cascadeCsmStaticReceiver ? "static depth-selected red diagnostic" : "suppressed");
        if (g_cascadeCsmStaticReceiver)
            fprintf(stderr, "[shadowmap][csm] Phase 4f enabled: frozen camera-view depth "
                            "selects cascade 0..3; hard splits, static red diagnostic%s\n",
                    g_cascadeCsmDynamicReceiver ? " + bucket-2 dynamic body" : " only");
        if (g_cascadeCsmDynamicReceiver)
            fprintf(stderr, "[shadowmap][csm] Phase 4g enabled: bucket %d body depth fans out "
                            "to all dynamic layers and is composited only when every layer is fresh\n",
                    g_cascadeDynamicBucket);
        if (g_cascadeCsmAlphaReceiver)
            fprintf(stderr, "[shadowmap][csm] Phase 4h enabled: alpha/card bucket %d "
                            "accumulates after bucket %d in the same fresh dynamic layers\n",
                    g_cascadeAlphaBucket, g_cascadeDynamicBucket);
        if (g_cascadeCompositeShadows)
            fprintf(stderr, "[shadowmap][csm] Phase 5a enabled: translucent-black shadow "
                            "composite strength=%.2f; red diagnostic remains available without "
                            "NWN_SHADOWMAP_CSM_COMPOSITE\n", g_cascadeCompositeStrength);
        if (g_cascadeDynamicCharacterCapture)
            fprintf(stderr, "[shadowmap][dynamic] Phase 3j enabled: duplicate only "
                            "Scene::RenderDynamicGeometry into private dynamic cascade-%d "
                            "once; no receiver and no static-bucket capture\n",
                    g_cascadeLightCaptureLayer);
        if (g_cascadeDynamicBucketCapture)
            fprintf(stderr, "[shadowmap][dynamic] Phase 3k enabled: duplicate candidate "
                            "post-dynamic bucket %d into private dynamic cascade-%d once; "
                            "no receiver\n",
                    g_cascadeDynamicBucket, g_cascadeLightCaptureLayer);
        if (g_cascadeAlphaCardCapture)
            fprintf(stderr, "[shadowmap][alpha] Phase 3o enabled: enlarged alpha-card "
                            "depth crop for bucket %d after the normal private PGM; "
                            "no receiver\n", g_cascadeDynamicBucket);
        if (g_cascadeStaticAlphaCapture)
            fprintf(stderr, "[shadowmap][alpha] Phase 3q enabled: source-classified static "
                            "AlphaDiscard bucket %d to private dynamic cascade-%d; "
                            "writes shadowmap_static_alpha_cards.pgm only\n",
                    g_cascadeStaticAlphaBucket, g_cascadeLightCaptureLayer);
    } else if (g_cascadeMathTrace) {
        // The fit is valid only at the deliberately identified Scene::Render
        // entry boundary.  Refuse a tempting partial configuration rather than
        // silently reintroduce legacy live-stack sampling.
        fprintf(stderr, "[shadowmap][csm] NWN_SHADOWMAP_CASCADE_MATH requires NWN_SHADOWMAP_TRACE=1; disabled\n");
        g_cascadeMathTrace = false;
        g_lightVectorTrace = false;
        g_cascadeTargetValidate = false;
        g_cascadeGeometryTrace = false;
        g_cascadeCameraCapture = false;
        g_cascadeLightCapture = false;
        g_cascadeMatrixTrace = false;
    } else if (g_lightVectorTrace) {
        fprintf(stderr, "[shadowmap][lightvec] NWN_SHADOWMAP_LIGHT_VECTOR_TRACE requires NWN_SHADOWMAP_TRACE=1; disabled\n");
        g_lightVectorTrace = false;
        g_cascadeTargetValidate = false;
        g_cascadeGeometryTrace = false;
        g_cascadeCameraCapture = false;
        g_cascadeLightCapture = false;
        g_cascadeMatrixTrace = false;
    } else if (g_cascadeTargetValidate || g_cascadeGeometryTrace || g_cascadeMatrixTrace ||
               g_cascadeLightCapture) {
        fprintf(stderr, "[shadowmap][csm] cascade target/geometry trace options require "
                        "NWN_SHADOWMAP_TRACE=1; disabled\n");
        g_cascadeTargetValidate = false;
        g_cascadeGeometryTrace = false;
        g_cascadeCameraCapture = false;
        g_cascadeLightCapture = false;
        g_cascadeMatrixTrace = false;
    }

    if (!bind_gl()) {
#ifdef _WIN32
        // EXPECTED ON WINDOWS, and not fatal. Modern GL entry points can only
        // be resolved through wglGetProcAddress, which needs a CURRENT GL
        // CONTEXT -- and this constructor runs at DLL attach, long before the
        // engine creates one. (Linux never hits this: dlsym finds GLEW's
        // symbols in the executable whether or not a context exists.) Install
        // the hooks anyway and retry the binding from the render path, where a
        // context is guaranteed; see g_glBound in SceneRender_detour.
        fprintf(stderr, "[shadowmap] GL entry points not resolvable yet (no GL context "
                        "at DLL attach); deferring bind_gl to the first Scene::Render\n");
#else
        fprintf(stderr, "[shadowmap] GL entry points missing; hook NOT installed.\n");
        return;
#endif
    } else {
        g_glBound = true;
    }
    if (!resolve_symbols()) {
        fprintf(stderr, "[shadowmap] symbol resolution failed; hook NOT installed.\n");
        return;
    }

    g_hook = subhook_new(reinterpret_cast<void*>(eng::SceneRender),
                         reinterpret_cast<void*>(SceneRender_detour),
                         SUBHOOK_64BIT_OFFSET);
    if (!g_hook || subhook_install(g_hook) != 0) {
        fprintf(stderr, "[shadowmap] subhook install failed.\n");
        return;
    }

#ifndef _WIN32
    if (nwn_oit_needs_texture_tracking() && eng::AurTextureBindInUnit &&
        eng::AurTextureGetName) {
        g_hookAurTextureBind = subhook_new((void*)eng::AurTextureBindInUnit,
                                           (void*)AurTextureBindInUnit_detour,
                                           SUBHOOK_64BIT_OFFSET);
        if (!g_hookAurTextureBind || subhook_install(g_hookAurTextureBind) != 0) {
            fprintf(stderr, "[oit][texture] warning: CAurTexture bind census unavailable\n");
            if (g_hookAurTextureBind) {
                subhook_free(g_hookAurTextureBind);
                g_hookAurTextureBind = nullptr;
            }
        } else {
            fprintf(stderr, "[oit][texture] CAurTexture name tracking active\n");
        }
    }
#endif

    if (g_areaShadowFlagsEnabled && eng::AreaUpdateShadowingLights) {
        g_hookAreaShadowFlags = subhook_new((void*)eng::AreaUpdateShadowingLights,
                                            (void*)AreaUpdateShadowingLights_detour,
                                            SUBHOOK_64BIT_OFFSET);
        if (!g_hookAreaShadowFlags || subhook_install(g_hookAreaShadowFlags) != 0) {
            fprintf(stderr, "[shadowmap][area] warning: area-policy hook unavailable; "
                            "using normal directional shadow strength.\n");
            if (g_hookAreaShadowFlags) {
                subhook_free(g_hookAreaShadowFlags);
                g_hookAreaShadowFlags = nullptr;
            }
        } else {
            fprintf(stderr, "[shadowmap][area] SunShadows/MoonShadows/ShadowOpacity policy active.\n");
        }
    }

    // THE OBSERVED PATH -- the only one Windows can use, and it runs on Linux
    // too so the two can be compared against each other in the same session.
    // See AurSetDynProjLight_detour for why this trio is unambiguous.
    if (g_areaShadowFlagsEnabled && eng::AurEnableShadowing &&
        eng::AurDisableShadowing && eng::AurSetDynProjLight) {
        struct Trio { subhook_t* slot; void* target; void* detour; };
        const Trio trio[] = {
            {&g_hookAurEnableShadowing,  (void*)eng::AurEnableShadowing,
                                         (void*)AurEnableShadowing_detour},
            {&g_hookAurDisableShadowing, (void*)eng::AurDisableShadowing,
                                         (void*)AurDisableShadowing_detour},
            {&g_hookAurSetDynProjLight,  (void*)eng::AurSetDynProjLight,
                                         (void*)AurSetDynProjLight_detour},
        };
        bool ok = true;
        for (const Trio& t : trio) {
            *t.slot = subhook_new(t.target, t.detour, SUBHOOK_64BIT_OFFSET);
            if (!*t.slot || subhook_install(*t.slot) != 0) { ok = false; break; }
        }
        if (!ok) {
            // ALL THREE OR NONE. Two of the three installed would leave the
            // pending-toggle protocol reading a truncated sequence, which is
            // worse than not observing at all.
            for (const Trio& t : trio) {
                if (*t.slot) { subhook_remove(*t.slot); subhook_free(*t.slot); *t.slot = nullptr; }
            }
            fprintf(stderr, "[shadowmap][area] warning: observed-policy hooks unavailable.\n");
        } else {
            fprintf(stderr, "[shadowmap][area] observed Aur* shadow-policy hooks active%s.\n",
                    eng::AreaUpdateShadowingLights ? " (cross-checked against the flag fields)"
                                                   : " (sole policy source on this platform)");
        }
    }

    const bool oitNeedsBucketHook = nwn_oit_needs_bucket_hook();
    if (g_traceEnabled || oitNeedsBucketHook) {
        auto install_trace = [](subhook_t& slot, void* target, void* detour,
                                const char* name) {
            if (!target) {
                fprintf(stderr, "[shadowmap][trace] unavailable: %s unresolved\n", name);
                return;
            }
            slot = subhook_new(target, detour, SUBHOOK_64BIT_OFFSET);
            if (!slot || subhook_install(slot) != 0) {
                fprintf(stderr, "[shadowmap][trace] WARNING: hook failed: %s\n", name);
                if (slot) { subhook_free(slot); slot = nullptr; }
                return;
            }
#ifdef _WIN32
            // REFUSE a trampoline-less hook on Windows.
            // Without a trampoline, CALL_ORIGINAL falls back to
            // remove -> call -> reinstall, which briefly unpatches the
            // function. That is explicitly not thread-safe, and the Linux
            // build gets away with it only because its render path is single
            // threaded. On Windows this crashed the game during startup:
            // A/B bisect showed NWN_SHADOWMAP_OFF and a plain launch both run,
            // while enabling the trace hooks crashes -- and Camera::Render was
            // the one hook reporting trampoline=NULL.
            // Nothing is lost by skipping it: the detour only records
            // g_traceCurrentCamera, which Camera::RenderScene (hooked with a
            // real trampoline) sets identically.
            if (!subhook_get_trampoline(slot)) {
                fprintf(stderr, "[shadowmap][trace] SKIPPED %s: no trampoline, and the "
                                "remove/call/reinstall fallback is unsafe here\n", name);
                subhook_remove(slot);
                subhook_free(slot);
                slot = nullptr;
                return;
            }
#endif
            fprintf(stderr, "[shadowmap][trace] hooked %s (trampoline=%p%s)\n",
                    name, subhook_get_trampoline(slot),
                    subhook_get_trampoline(slot) ? "" : " -- remove/call/reinstall");
        };

        if (g_traceEnabled) {
            fprintf(stderr, "[shadowmap][step] installing trace hooks\n");
            install_trace(g_hookTraceCameraRender, (void*)eng::CameraRender,
                          (void*)CameraRender_detour, "Camera::Render");
            install_trace(g_hookTraceCameraScene, (void*)eng::CameraRenderScene,
                          (void*)CameraRenderScene_detour, "Camera::RenderScene");
            if (g_casterCullTrace || g_casterFullBspTrace || g_casterFullBspNativeSubmit)
                install_trace(g_hookTraceManageSceneBSP, (void*)eng::ManageSceneBSP,
                              (void*)ManageSceneBSP_detour, "ManageSceneBSP");
            install_trace(g_hookTraceSceneSingle, (void*)eng::SceneRenderSinglePass,
                          (void*)SceneRenderSinglePass_detour, "Scene::RenderSinglePass");
            install_trace(g_hookTraceSceneDynamic, (void*)eng::SceneRenderDynamicGeometry,
                          (void*)SceneRenderDynamicGeometry_detour, "Scene::RenderDynamicGeometry");
        }
        // OIT needs only the bucket identity. Installing this one hook does not
        // turn on the shadow trace, targets, replay, or shader diagnostics.
        install_trace(g_hookTraceBucket, (void*)eng::SceneRenderDrawBucket,
                      (void*)SceneRenderDrawBucket_trace_detour, "Scene::RenderDrawBucket");
        if (g_traceEnabled) {
            install_trace(g_hookTracePrioritizeShadow, (void*)eng::LightPrioritizeShadow,
                          (void*)LightPrioritizeShadow_trace_detour, "LightManager::PrioritizeShadow");
            if (!g_hookTraceGetShadowLights)
                install_trace(g_hookTraceGetShadowLights, (void*)eng::LightGetShadowLights,
                              (void*)LightGetShadowLights_trace_detour, "LightManager::GetShadowLights");
            if (eng::SetLightGL)
                install_trace(g_hookSetLightGL, (void*)eng::SetLightGL,
                              (void*)SetLightGL_detour, "SetLightGL");
            fprintf(stderr, "[shadowmap][step] trace hooks installed\n");
            fprintf(stderr,
                    "[shadowmap][trace] scene-trace mode active: targets/replay/shader injection/"
                    "fullscreen receiver are bypassed; recording up to %u area frames and %u events.\n",
                    g_traceFramesMax, g_traceEventsMax);
        } else {
            fprintf(stderr, "[oit][foliage] bucket identity hook installed "
                            "without enabling shadow trace mode\n");
        }
    }

    // Phase 3: hook the matrix setters to capture a renderer instance and the
    // live camera transform. Installed ONLY when the light pass is enabled, so
    // the validated Phase 1/2 behaviour can never be affected by them.
    if (g_lightPass && eng::SetViewTransform) {
        g_hookView = subhook_new(reinterpret_cast<void*>(eng::SetViewTransform),
                                 reinterpret_cast<void*>(SetViewTransform_detour),
                                 SUBHOOK_64BIT_OFFSET);
        if (!g_hookView || subhook_install(g_hookView) != 0)
            fprintf(stderr, "[shadowmap] WARNING: SetViewTransform hook failed; "
                            "light pass unavailable.\n");
        else
            fprintf(stderr, "[shadowmap] SetViewTransform hooked (trampoline=%p%s)\n",
                    subhook_get_trampoline(g_hookView),
                    subhook_get_trampoline(g_hookView) ? ""
                        : " -- NULL, using remove/call/reinstall");
    }
    if (g_lightPass && eng::MtxPerspective) {
        g_hookPersp = subhook_new(reinterpret_cast<void*>(eng::MtxPerspective),
                                  reinterpret_cast<void*>(MtxPerspective_detour),
                                  SUBHOOK_64BIT_OFFSET);
        if (!g_hookPersp || subhook_install(g_hookPersp) != 0)
            fprintf(stderr, "[shadowmap] WARNING: aurMatrixStack::Perspective hook "
                            "failed; camera projection will not be restored.\n");
        else
            fprintf(stderr, "[shadowmap] aurMatrixStack::Perspective hooked "
                            "(trampoline=%p%s)\n",
                    subhook_get_trampoline(g_hookPersp),
                    subhook_get_trampoline(g_hookPersp) ? ""
                        : " -- NULL, using remove/call/reinstall");
    }
    if ((shadow_getenv("NWN_SHADOWMAP_STENCIL_TRACE") || g_traceEnabled) && eng::SceneRenderShadows) {
        g_hookStencil = subhook_new(reinterpret_cast<void*>(eng::SceneRenderShadows),
                                    reinterpret_cast<void*>(SceneRenderShadows_detour),
                                    SUBHOOK_64BIT_OFFSET);
        if (!g_hookStencil || subhook_install(g_hookStencil) != 0)
            fprintf(stderr, "[shadowmap] WARNING: stencil reference hook failed.\n");
        else
            fprintf(stderr, "[shadowmap] stencil reference hook active (no state changes).\n");
    }

    g_nextAttempt  = now_seconds() + g_probeDelay;
    g_lightStart   = now_seconds() + g_lightDelay;
    g_nextStatus   = now_seconds() + g_lightDelay + g_statusEvery;
    g_mtxDeadline  = now_seconds() + double(g_mtxDumpAt < 0 ? 0 : g_mtxDumpAt);
    g_dumpDeadline = now_seconds() + double(g_dumpAt < 0 ? 0 : g_dumpAt);
    g_localLightDumpDeadline =
        now_seconds() + double(g_localLightDumpAt < 0 ? 0 : g_localLightDumpAt);
    // AFTER the environment, so the panel's saved choices win over the
    // launcher's defaults -- see load_settings().
    load_settings();
    fprintf(stderr, "[shadowmap] active. Scene::Render detoured, target %dx%d.\n",
            g_size, g_size);
#ifdef _WIN32
    // Explicit end-of-init marker. Its ABSENCE is the signal: it means
    // shadowmap_init returned early or died, which is otherwise invisible once
    // the game's own logging starts sharing this file.
    fprintf(stderr, "[shadowmap][win] init complete: hooks installed, %u built-in "
                    "defaults in effect\n", shadow_default_env_count());
#endif
    if (g_lightPass) {
        fprintf(stderr, "[shadowmap] light pass ON: dir=(%.2f %.2f %.2f) "
                        "conv=%d extent=%.1f dist=%.1f when=%s buckets=",
                g_lightDir[0], g_lightDir[1], g_lightDir[2], g_conv,
                g_extent, g_dist, g_afterOrig ? "after" : "before");
        for (int i : g_lightBuckets) fprintf(stderr, "%d ", i);
        fprintf(stderr, "\n[shadowmap] Dump with NWN_SHADOWMAP_DUMP=<frame>; the "
                        "PGM should be TOP-DOWN.\n");
    }
    if (g_probeArmed)
        fprintf(stderr, "[shadowmap] probe armed: first attempt in %.0fs, then every "
                        "%.0fs, up to %d tries.\n"
                        "[shadowmap] Load into an area and look at some geometry.\n",
                g_probeDelay, g_probeEvery, g_probeTries);
}

__attribute__((destructor))
static void shadowmap_fini() {
    // Deliberately not deleting the GL objects: at destructor time the context
    // may already be gone, and a bad GL call on the way out would turn a clean
    // exit into a crash report. The process is ending; the driver reclaims it.
    // Put SDL_PollEvent back before this library can be unmapped: unlike the
    // GL objects above, leaving this patched would point the game's event pump
    // at code that no longer exists.
    if (g_sdlPollEventSlot && g_realSdlPollEvent) {
        *g_sdlPollEventSlot = (void*)g_realSdlPollEvent;
        g_sdlPollEventSlot = nullptr;
    }
    if (g_hookPersp) { subhook_remove(g_hookPersp); subhook_free(g_hookPersp); }
    if (g_hookView)  { subhook_remove(g_hookView);  subhook_free(g_hookView);  }
    if (g_hookStencil) { subhook_remove(g_hookStencil); subhook_free(g_hookStencil); }
    if (g_hookTraceManageSceneBSP) { subhook_remove(g_hookTraceManageSceneBSP); subhook_free(g_hookTraceManageSceneBSP); }
    if (g_hookTraceGetShadowLights) { subhook_remove(g_hookTraceGetShadowLights); subhook_free(g_hookTraceGetShadowLights); }
    if (g_hookTracePrioritizeShadow) { subhook_remove(g_hookTracePrioritizeShadow); subhook_free(g_hookTracePrioritizeShadow); }
    if (g_hookTraceBucket) { subhook_remove(g_hookTraceBucket); subhook_free(g_hookTraceBucket); }
    if (g_hookTraceSceneDynamic) { subhook_remove(g_hookTraceSceneDynamic); subhook_free(g_hookTraceSceneDynamic); }
    if (g_hookTraceSceneSingle) { subhook_remove(g_hookTraceSceneSingle); subhook_free(g_hookTraceSceneSingle); }
    if (g_hookTraceCameraScene) { subhook_remove(g_hookTraceCameraScene); subhook_free(g_hookTraceCameraScene); }
    if (g_hookTraceCameraRender) { subhook_remove(g_hookTraceCameraRender); subhook_free(g_hookTraceCameraRender); }
    if (g_hook)      { subhook_remove(g_hook);      subhook_free(g_hook);      }
}
