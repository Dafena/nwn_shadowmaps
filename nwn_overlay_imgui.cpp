// Dear ImGui implementation of the injector's settings overlay.
// See nwn_overlay.h for the input policy and why this lives in its own TU.

#include "nwn_overlay.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include "nwn_platform.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace {

bool g_ready   = false;
bool g_failed  = false;   // do not retry init forever
bool g_visible = false;

// SDL2 is statically linked into nwmain-linux, so these resolve from the
// executable itself. Polling only -- the event queue is never touched.
using SDLGetMouseState_t    = uint32_t (*)(int*, int*);
SDLGetMouseState_t g_sdlGetMouseState = nullptr;

double now_seconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

void feed_input(ImGuiIO& io) {
    if (!g_sdlGetMouseState)
        g_sdlGetMouseState = (SDLGetMouseState_t)dlsym(RTLD_DEFAULT, "SDL_GetMouseState");
    if (!g_sdlGetMouseState) return;
    int mx = 0, my = 0;
    const uint32_t buttons = g_sdlGetMouseState(&mx, &my);
    io.AddMousePosEvent((float)mx, (float)my);
    // SDL_BUTTON_*MASK: left = 1<<0, middle = 1<<1, right = 1<<2.
    io.AddMouseButtonEvent(0, (buttons & 0x1u) != 0);
    io.AddMouseButtonEvent(2, (buttons & 0x2u) != 0);
    io.AddMouseButtonEvent(1, (buttons & 0x4u) != 0);
}

// Hover help. The panel is the primary interface now, so terms of art
// ("cascade") have to be explained in it rather than in a doc nobody has open.
void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace

bool nwn_overlay_ready() { return g_ready; }
bool nwn_overlay_visible() { return g_visible; }
void nwn_overlay_set_visible(bool visible) { g_visible = visible; }

// These are read from the SDL_PollEvent hook, which runs outside the render
// pass, so they must not touch ImGui state that only exists between
// NewFrame/Render -- io.WantCaptureMouse/Keyboard are stable across the frame,
// which is exactly why they are the right thing to ask.
bool nwn_overlay_wants_mouse() {
    if (!g_ready || !g_visible) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool nwn_overlay_wants_keyboard() {
    if (!g_ready || !g_visible) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

void nwn_overlay_add_mouse_pos(float x, float y) {
    if (!g_ready) return;
    ImGui::GetIO().AddMousePosEvent(x, y);
}

void nwn_overlay_add_mouse_button(int button, bool down) {
    if (!g_ready || button < 0 || button > 2) return;
    ImGui::GetIO().AddMouseButtonEvent(button, down);
}

void nwn_overlay_add_mouse_wheel(float x, float y) {
    if (!g_ready || !g_visible) return;
    ImGui::GetIO().AddMouseWheelEvent(x, y);
}

bool nwn_overlay_init() {
    if (g_ready)  return true;
    if (g_failed) return false;

    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) {
        g_failed = true;
        fprintf(stderr, "[shadowmap][overlay] ImGui::CreateContext failed\n");
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    // No .ini on disk: this is someone else's process, and writing a settings
    // file next to the game would be a surprising side effect.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // There is no platform backend, so ImGui must not expect one to set these.
    io.BackendFlags &= ~ImGuiBackendFlags_HasSetMousePos;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        ImGui::DestroyContext();
        g_failed = true;
        fprintf(stderr, "[shadowmap][overlay] ImGui_ImplOpenGL3_Init failed\n");
        return false;
    }
    g_ready = true;
    fprintf(stderr, "[shadowmap][overlay] ImGui overlay ready (GL3 backend, polled SDL input)\n");
    return true;
}

void nwn_overlay_shutdown() {
    if (!g_ready) return;
    g_ready = false;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
}

void nwn_overlay_render(int viewportW, int viewportH, const NwnOverlayState& st) {
    if (!g_visible || viewportW <= 0 || viewportH <= 0) return;
    if (!nwn_overlay_init()) return;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)viewportW, (float)viewportH);
    static double last = 0.0;
    const double now = now_seconds();
    io.DeltaTime = (last > 0.0) ? (float)(now - last) : (1.0f / 60.0f);
    if (io.DeltaTime <= 0.0f) io.DeltaTime = 1.0f / 60.0f;
    last = now;
#ifndef _WIN32
    feed_input(io);
#else
    // Windows feeds ImGui from the window subclass (see nwn_platform_win.cpp).
    // Polling here would undo it: the messages the panel consumes are swallowed
    // before SDL sees them, so SDL_GetMouseState reports the buttons as up.
    (void)&feed_input;
#endif

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(430, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("NWN Shadow Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::TextDisabled("Ctrl+Shift+F11 closes this panel");
        ImGui::Separator();

        // NOTE: these were briefly hidden on Windows and are back by the
        // maintainer's call -- Windows is the platform that still needs
        // debugging, and hiding the Diagnostics ladder there removed the only
        // in-game way to ask "does the receiver pass reach the screen at all".
        // Do not trim the Windows panel again until Windows is working.
        // WINDOWS SHIPS A USER-FACING PANEL. Everything guarded out below is
        // either an internal knob (strength/bias/PCF, the unfinished
        // local-light shadow) or a readout that only means something with the
        // source open. The LIFT controls stay: they are quality settings a
        // player would actually choose between.
#if !NWN_SHIP
        if (ImGui::CollapsingHeader("Sun shadows")) {
            if (st.csmComposite) {
                bool composite = *st.csmComposite;
                if (ImGui::Checkbox("Dark composite (off = red diagnostic)", &composite))
                    *st.csmComposite = composite;
            }
            if (st.csmStrength)
                ImGui::SliderFloat("Strength", st.csmStrength, 0.0f, 1.0f, "%.2f");
            if (st.csmBias)
                ImGui::SliderFloat("Bias", st.csmBias, 0.0f, 0.05f, "%.4f");
            if (st.csmBlend)
                ImGui::SliderFloat("Cascade blend", st.csmBlend, 0.0f, 10.0f, "%.2f");
            if (st.csmPcf)
                ImGui::SliderFloat("PCF radius", st.csmPcf, 0.0f, 4.0f, "%.2f");
            if (st.areaShadowFadeSeconds) {
                ImGui::SliderFloat("Day/night fade", st.areaShadowFadeSeconds,
                                   0.0f, 5.0f, "%.2f s");
                help("Fade the directional sun/moon shadow when NWN changes between "
                     "its day and night area-shadow policies.\n\n"
                     "This affects only the final fullscreen shadow darkness. It does "
                     "not rebuild shadow maps, move the sun, or affect local lights. "
                     "Set 0 for the old instant switch.");
            }
            ImGui::Text("Cascade far clips: %.2f  %.2f  %.2f  %.2f",
                        st.clipFar[0], st.clipFar[1], st.clipFar[2], st.clipFar[3]);
            ImGui::Text("Sun dir: (%.2f %.2f %.2f)", st.sunDir[0], st.sunDir[1], st.sunDir[2]);
            ImGui::Text("Caster draws: %u static / %u dynamic", st.staticDraws, st.dynamicDraws);
            ImGui::Text("Bucket replay (fast path): %s", st.bucketReplay ? "ON" : "off");
        }

#endif   // !NWN_SHIP -- Sun shadows
#if !NWN_SHIP
        if (ImGui::CollapsingHeader("Local light")) {
            if (st.localEnabled) {
                bool enabled = *st.localEnabled;
                if (ImGui::Checkbox("Cast local-light shadow", &enabled))
                    *st.localEnabled = enabled;
            }
            if (st.localStrength)
                ImGui::SliderFloat("Local strength", st.localStrength, 0.0f, 1.0f, "%.2f");
            if (st.localBias)
                ImGui::SliderFloat("Local bias", st.localBias, 0.0f, 0.05f, "%.4f");
            // The "Shadow light" picker used to live here. It chose which single
            // light the capture followed, back when selection was an
            // injector heuristic. NWN's GetShadowLights() order is authoritative
            // now and the capture consumes the first N of it, so the control was
            // WRITE-ONLY -- the panel set g_localLightPick and nothing ever read
            // it. Its tooltip still described the old failure ("one light
            // attached to the player ... nothing ever appeared"), which now
            // contradicts how selection works. Removed 2026-08-13.

            if (st.localSoft) {
                ImGui::SliderFloat("Local PCF radius", st.localSoft, 0.0f, 4.0f, "%.2f");
                help("Softness of the local shadow's edge, in shadow-map "
                     "texels.\n\n"
                     "Same 3x3 filter and the same meaning as the sun's \"PCF "
                     "radius\", including 0 = a single hard sample -- so the two "
                     "shadow systems respond identically.");
            }
            if (st.localMinSep) {
                ImGui::SliderFloat("No self-shadow", st.localMinSep, 0.0f, 2.0f, "%.2f units");
                help("How much NEARER a caster must be, in world units, before "
                     "it casts a shadow here.\n\n"
                     "Acne is the case where the stored depth and the surface "
                     "being tested are the SAME surface, so the gap is ~0. A "
                     "real shadow has a caster tens of centimetres away. "
                     "Requiring a real gap removes self-shadowing outright and "
                     "leaves genuine shadows untouched -- and unlike a depth "
                     "bias it means the same thing at every distance.\n\n"
                     "A character is roughly 0.3 units thick, so 0.30 stops one "
                     "shadowing itself. 0 restores self-shadowing.");
            }
            if (st.localNormalBias) {
                ImGui::SliderFloat("Local normal bias", st.localNormalBias, 0.0f, 8.0f, "%.1f");
                help("Moves the shadow lookup along the SURFACE NORMAL, in "
                     "shadow-map texels.\n\n"
                     "This is the fix for the acne pattern on a character's "
                     "face: the sample stops landing on the very surface being "
                     "tested, so the depth comparison stops flipping across it. "
                     "Depth bias cannot do this -- it pushes along the light "
                     "ray, so on a grazing surface the amount needed is "
                     "unbounded and raising it just detaches the shadow.\n\n"
                     "Raise until the mottling goes; lower if shadows start "
                     "shrinking away from thin contact points.");
            }
            if (st.localAlphaCast) {
                bool ac = *st.localAlphaCast;
                if (ImGui::Checkbox("Alpha casters (hair, cloaks)", &ac)) *st.localAlphaCast = ac;
                help("Let dithered alpha geometry cast local shadows.\n\n"
                     "OFF by default: NWN dithers those cards in SCREEN space, "
                     "so seen from a light the pattern is different and the card "
                     "stores as near-solid depth -- hair then paints a solid "
                     "blob across the face.\n\n"
                     "Turn it on if you would rather have hair and cloak shadows "
                     "and can live with that.");
            }
            if (st.localSlope) {
                ImGui::SliderFloat("Local slope bias", st.localSlope, 0.0f, 8.0f, "%.1f");
                help("Depth offset applied while FILLING a local shadow map, "
                     "scaled by each polygon's slope.\n\n"
                     "A character is both caster and receiver, so its own stored "
                     "depth fights its surface and you get blocky self-shadowing "
                     "-- worst at Low, where a texel covers centimetres. This "
                     "removes it without the flat bias that would detach the "
                     "shadow from the feet.\n\n"
                     "Raise it if characters look mottled; lower it if shadows "
                     "start pulling away from their caster.");
            }
            if (st.localEdgeFade) {
                ImGui::SliderFloat("Edge fade", st.localEdgeFade, 0.0f, 0.5f, "%.2f");
                help("How gently a local shadow disappears at the EDGE of the "
                     "light's reach.\n\n"
                     "The shadow map covers a cone; without this the shadow "
                     "simply stopped where that cone ended, which reads as a "
                     "straight cut across the floor. This fades it out over a "
                     "band at the border, and does the same at the far plane.\n\n"
                     "Larger = softer and shorter shadows; 0 = the hard cut.");
            }
            if (st.localFov) {
                ImGui::SliderFloat("Cone angle", st.localFov, 20.0f, 175.0f, "%.0f deg");
                help("How wide the cone is that a local light's shadow map "
                     "covers.\n\n"
                     "One light gets ONE face aimed down, so this angle decides "
                     "how far from directly beneath the lamp a shadow can still "
                     "be cast. Wide (the 170 default) reaches almost to the "
                     "horizon but spends its texels on a huge area, which both "
                     "softens the result and stretches shadows at the rim -- a "
                     "perspective cone spreads texels by roughly 1/cos^3 off "
                     "axis.\n\n"
                     "Narrow it to match the base game's shallower look and to "
                     "concentrate the same resolution on the ground near the "
                     "lamp. Too narrow and shadows stop abruptly partway across "
                     "the floor -- Edge fade softens that boundary.");
            }
            if (st.localHeight) {
                ImGui::SliderFloat("Lamp lift", st.localHeight, 0.0f, 20.0f, "%.1f units");
                help("Raises the point a local shadow is CAST FROM, without "
                     "moving the light itself.\n\n"
                     "A wall torch is genuinely low, so a physically correct "
                     "shadow map rakes a character's shadow a long way across "
                     "the floor. The base game does not do that -- it behaves "
                     "as though the light were higher, giving a shorter, "
                     "steeper shadow. Raise this until the length matches.\n\n"
                     "Only the shadow geometry changes: brightness, reach and "
                     "falloff still come from the lamp's real position, so "
                     "nothing about the lighting shifts. 0 = physically "
                     "correct.");
            }
            if (st.localFalloff) {
                ImGui::SliderFloat("Local falloff", st.localFalloff, 0.05f, 1.0f, "%.2f");
                help("How fast a local shadow fades with distance from its "
                     "lamp.\n\n"
                     "At 1.00 a shadow's darkness follows the lamp's own "
                     "brightness exactly, and that range is too wide to tune: "
                     "shadows across a room are nearly invisible, while raising "
                     "Local strength to reach them turns the ones at your feet "
                     "black.\n\n"
                     "Lower values lift the middle and far field and leave the "
                     "near field alone. They still fade to nothing at the edge "
                     "of the light's reach, so no hard ring appears.");
            }
            if (st.localLift) {
                ImGui::SliderFloat("Lifts sun shadow", st.localLift, 0.0f, 1.0f, "%.2f");
                help("How much this light's illumination cancels the SUN's shadow "
                     "on the same surface.\n\n"
                     "Being out of the sun says nothing about a torch two feet "
                     "away, but the shadow composite is flat black and knew "
                     "nothing about local lights -- so walking under an awning "
                     "with a torch dimmed the torchlight, the character and the "
                     "floor together.\n\n"
                     "1 = fully cancelled where the scene is brightest. "
                     "0 = the old behaviour.");
            }
            ImGui::Text("Depth map: %s", st.localReady ? "fresh" : "none this frame");
            ImGui::Text("Candidates in list: %u", st.localLights);
            ImGui::Text("Selected: (%.2f %.2f %.2f) r=%.2f",
                        st.localPos[0], st.localPos[1], st.localPos[2], st.localRadius);
            ImGui::ColorButton("##lightcol",
                               ImVec4(st.localRgb[0], st.localRgb[1], st.localRgb[2], 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));
            ImGui::SameLine();
            ImGui::Text("colour (%.2f %.2f %.2f)", st.localRgb[0], st.localRgb[1], st.localRgb[2]);
            ImGui::TextDisabled("One light, one wide cone aimed DOWN (a point light\n"
                                "really needs six faces; down is where the floor is).");
        }

#endif   // !NWN_SHIP -- Local light

        if (ImGui::CollapsingHeader("Performance")) {
            // Deliberately NOT exposed: the full-BSP caster submission, the
            // shadow receiver pass, the static cascade cache and the
            // world-anchored map. All four are required for correct, fast
            // shadows and were only ever surfaced as A/B switches while their
            // costs were being diagnosed. Their env vars still work for
            // debugging (NWN_SHADOWMAP_FULL_BSP_SUBMIT=0 etc).
#if !NWN_SHIP
            // TESTING ONLY. Shipping builds pin this to three and let NWN's own
            // "Shadow Casting Lights" video setting decide how many of the three
            // the engine actually nominates -- our own copy of that number could
            // only ever disagree with it.
            if (st.localCubeSources) {
                // One method now (see shadow_targets.inc): this is purely how
                // many lights get a map. NWN exposes at most three.
                static const char* kSourceNames[] = { "1 light", "2 lights", "3 lights" };
                int idx = *st.localCubeSources - 1;
                idx = idx < 0 ? 0 : (idx > 2 ? 2 : idx);
                if (ImGui::Combo("Local shadow sources", &idx, kSourceNames, 3))
                    *st.localCubeSources = idx + 1;
                help("How many nearby lights get their own shadow map.\n\n"
                     "Each light's map is filled from inside the game's own drawing "
                     "pass -- the same way and at the same moment the sun's shadows are "
                     "built. That is what makes the casters exactly what you see on "
                     "screen while the work stays cheap: the engine draws the geometry, "
                     "rather than this tool re-issuing every caster itself.\n\n"
                     "FOUR FACES fixes shadow resolution rather than reach. One face is "
                     "a single wide cone, and a cone spreads its pixels by ANGLE, so the "
                     "further from straight-down you look the coarser it gets -- at the "
                     "wide angles needed for good reach, dramatically so. Four narrower "
                     "faces cover MORE ground while keeping every part of it near a face "
                     "centre, so shadows stay sharp out to the edge.\n\n"
                     "It costs one extra pass per face per light: 1 light x 4 faces does "
                     "about the same drawing as the sun's four cascade slices, which is "
                     "cheap because the engine issues those draws. 3 lights x 4 faces is "
                     "four times that -- try it, but expect it to be the one that hurts.\n\n"
                     "Separate from how many lights LIGHT the scene -- that follows "
                     "the game's own \"Lighting Max Lights\" video setting. This is the "
                     "expensive shadow-map budget, and NWN exposes at most three "
                     "shadow-casting lights.");
            }
#endif
            if (st.liftThreshold) {
                ImGui::SliderFloat("Bright surfaces keep light", st.liftThreshold, 0.0f, 1.0f, "%.2f");
                help("Stops SELF-ILLUMINATED surfaces being dimmed by shadow.\n\n"
                     "Glowing things -- runes, lava, lit windows, magic effects -- are "
                     "bright no matter how they are lit, so laying a shadow over them "
                     "is simply wrong. A fullscreen pass has no material information, "
                     "but \"already bright\" is exactly what distinguishes them, so the "
                     "shadow fades out as a surface approaches full brightness.\n\n"
                     "This is the brightness where the fade STARTS; it reaches zero at "
                     "white. Lower protects more (and can start lifting shadow off "
                     "ordinary bright stone). 1.00 turns the guard off entirely.\n\n"
                     "Automatically disabled if the frame\'s colour could not be "
                     "captured, so it can never darken the wrong thing.");
            }
            if (st.lampLift) {
                bool lift = *st.lampLift;
                if (ImGui::Checkbox("Lights lift sun shadow", &lift)) *st.lampLift = lift;
                help("Brighten the sun's shadow where a nearby light is shining on "
                     "it.\n\n"
                     "NWN's sun shadow is flat darkness laid over everything it "
                     "covers, and it knows nothing about torches -- so walking under "
                     "an awning with a torch dims the torchlight, your character and "
                     "the floor all together, which is backwards. This lifts the "
                     "shadow where a lamp is genuinely lighting the ground.\n\n"
                     "It is a matter of taste rather than correctness: some areas "
                     "read better with the sun's shadow left flat and heavy. Turn it "
                     "off if the lifted patches look washed out.\n\n"
                     "Costs nothing either way -- off simply skips the work.");
            }
            ImGui::TextDisabled("  live shadow sources: %u / NWN candidates: %u",
                                st.localCubeActiveSources, st.localShadowCandidates);
            if (st.localCubeQuality) {
                static const char* kUpdateNames[] = {
                    "Low (25 ms)", "Medium (16 ms)", "Ultra (every frame)"
                };
                int quality = *st.localCubeQuality;
                quality = quality < 0 ? 0 : (quality > 2 ? 2 : quality);
                if (ImGui::Combo("Local shadow update", &quality, kUpdateNames, 3))
                    *st.localCubeQuality = quality;
                help("How often local-light shadow maps are rebuilt.\n\n"
                     "Low updates at most every 25 ms and is the default. Medium "
                     "updates at most every 16 ms, which normally matches a 60 Hz "
                     "frame rate. Ultra rebuilds on every rendered frame, matching "
                     "the sun-shadow cadence even at higher frame rates.\n\n"
                     "This affects only the local shadow maps. It does not change "
                     "how many ordinary lights illuminate the scene or how many "
                     "lights participate in sun-shadow lifting. Faster updates "
                     "increase the cost of up to three local depth captures.");
            }
#if !NWN_SHIP
            if (st.hideEngineShadows) {
                bool hs = *st.hideEngineShadows;
                if (ImGui::Checkbox("Hide the game's own shadows", &hs))
                    *st.hideEngineShadows = hs;
                help("Whether NWN draws ITS OWN shadows on top of this module's.\n\n"
                     "ON (default): the game's stencil shadow pass is skipped, "
                     "so only this module's shadows remain. Without it every "
                     "object gets two shadows -- the game's underneath the real "
                     "one.\n\n"
                     "IMPORTANT, and it is backwards from what you would "
                     "expect: leave shadows ON in the game's own video options, "
                     "set to BEST. Setting them to Off does not remove them -- "
                     "the engine falls back to a dark BLOB under each creature, "
                     "drawn by a different path that this cannot reach.\n\n"
                     "It must be Best, not Fast: Fast only casts from the "
                     "player, while Best covers creatures AND placeables. This "
                     "suppresses whatever that pass would have drawn, so the "
                     "wider the pass, the more it removes.");
            }
#endif   // !NWN_SHIP -- "Hide the game's own shadows"
            if (st.localEmitCast) {
                bool ec = *st.localEmitCast;
                if (ImGui::Checkbox("Lights casting shadows", &ec)) *st.localEmitCast = ec;
                help("Master switch for local light shadows.\n\n"
                     "ON: the lights NWN selects cast shadows, using the "
                     "\"Local shadow sources\" budget below.\n\n"
                     "OFF: no local light casts and the capture is skipped "
                     "entirely, so it costs nothing -- the sun/moon cascade "
                     "shadows are unaffected.\n\n"
                     "This used to filter only EMITTER lights (flames, a "
                     "carried torch) out of the caster set. Light selection is "
                     "now NWN\'s own priority list, which has no such "
                     "distinction, so the setting became a plain on/off.");
            }
#if !NWN_SHIP
            // DIAGNOSTIC A/B SWITCHES -- DEV BUILD ONLY. These three exist to answer
            // "is this module costing anything at all", by turning off the
            // receiver, the moving casters and the fixed casters until nothing
            // is drawn. That is a debugging instrument, not a setting: every one
            // of them REMOVES shadows, so a shipping build must not offer them.
            // They are not persisted on Windows either -- a hidden control whose
            // value is saved can only ever fight the shipped default, which has
            // already happened once here (see win/README.md).
            //
            // MASTER A/B. Present in NwnOverlayState from the beginning but
            // never given a widget, so the one switch that isolates this
            // module's whole per-frame cost could not be reached from the UI.
            // With both caster toggles off the shadow map is empty and the
            // frame rate did not move -- so what remains is this: the
            // full-screen scene capture plus the composite pass, which measured
            // 4-9 ms and 6-12 ms respectively.
            if (st.receiverEnabled) {
                bool re = *st.receiverEnabled;
                // NOT "Sun shadows": that is the label of the collapsing
                // header above, and ImGui derives a widget ID from its label,
                // so two visible items sharing one produced a "conflicting ID"
                // assert on hover. A distinct name is better than a ##suffix
                // here anyway -- two controls reading "Sun shadows" in the same
                // panel is confusing regardless of what ImGui thinks.
                if (ImGui::Checkbox("Draw shadows at all (master)", &re))
                    *st.receiverEnabled = re;
                help("Master switch for the sun shadow effect.\n\n"
                     "OFF: no shadows are drawn and the per-frame screen "
                     "capture is skipped as well, so this module costs almost "
                     "nothing. Everything else in the panel stops mattering.\n\n"
                     "Use it to answer one question: how much is this costing "
                     "you at all? Toggle it and watch the frame rate. If it "
                     "does not move, the cost is somewhere else entirely.");
            }
            if (st.dynamicCasters) {
                bool dc = *st.dynamicCasters;
                if (ImGui::Checkbox("Moving casters", &dc)) *st.dynamicCasters = dc;
                help("Whether things that MOVE cast a sun shadow.\n\n"
                     "Anything the engine treats as moving -- characters, "
                     "creatures, doors, and in some areas the scenery itself -- "
                     "has to be redrawn into the shadow map every frame, "
                     "because a cached copy would be wrong the moment it "
                     "moves.\n\n"
                     "In an area whose geometry ROTATES, nearly everything "
                     "counts as moving: measured 1827 moving draws against 215 "
                     "fixed ones, which is why such an area costs far more than "
                     "its size suggests.\n\n"
                     "OFF: only fixed scenery casts. Much cheaper, and the "
                     "bigger the area's moving half, the bigger the saving -- "
                     "but your character stops casting a shadow too.");
            }
            if (st.staticCasters) {
                bool sc = *st.staticCasters;
                if (ImGui::Checkbox("Fixed casters", &sc)) *st.staticCasters = sc;
                help("Whether things that DO NOT move cast a sun shadow -- "
                     "buildings, terrain, scenery.\n\n"
                     "These are normally close to free: they are drawn into the "
                     "shadow map once and reused until the view moves far "
                     "enough to need refitting.\n\n"
                     "Turn this off together with \"Moving casters\" and NOTHING "
                     "is drawn into the sun's shadow map at all. That is a "
                     "diagnostic, not a setting: if the frame rate does not "
                     "recover with both off, the shadow map was never what was "
                     "costing you.");
            }
#endif   // !NWN_SHIP -- diagnostic A/B switches
            ImGui::TextDisabled("Sun shadows are split into slices by distance from\n"
                                "the camera: near slices are small and detailed,\n"
                                "far slices are large and coarse.");
            // ONE named control for what used to be two sliders. The engine has
            // two separate numbers here -- how many distance slices exist, and
            // how many of those also redraw the MOVING casters -- but they are
            // not independent knobs in practice: dynamic can never exceed the
            // slice count, and the pair is what determines per-frame cost. So
            // the six levels below are a monotonic walk through real (slices,
            // dynamic) pairs rather than a single number, which is also why the
            // exact pair is printed underneath: nothing is hidden.
            //
            // Note the engine's own limits: slices are 1..kCascadeCount(4), and
            // 0 slices does not exist (every call site clamps <1 to 1). "Off"
            // therefore means zero per-frame caster work -- moving things stop
            // casting -- not "no shadows at all", which is the Dark composite
            // checkbox above.
            if (st.cascadeCount && st.dynamicLayers) {
                // "Off" (1 slice, 0 dynamic layers) was removed at the
                // maintainer's request: it read as "the injector is broken"
                // rather than as a quality level.
                static const char* kNames[]  = { "Low", "Medium",
                                                 "High", "Extreme", "Ultra" };
                // SLICES ARE NEARLY FREE, DYNAMIC LAYERS ARE THE COST.
                // Measured in a 32x32 area: the static half is cached (27616 fit
                // hits, 0 refits in a steady frame) so slice count barely
                // registers, while each dynamic layer redraws every moving
                // caster -- High(3,2) was 3070 replay draws against Low(2,1)'s
                // 1535, and the replay was 55% of the frame at Extreme(3,3).
                //
                // The old table tied the two together, so the only way to buy
                // back the dynamic cost was to give up slices -- which is the
                // half you SEE. Medium is now 3 slices with 1 dynamic layer:
                // High's near-shadow sharpness at Low's per-frame cost. It is
                // strictly better than the (2,2) it replaces, which cost more
                // and looked worse.
                static const int   kSlices[] = {  2,  3,  3,  3,  4 };
                static const int   kDyn[]    = {  1,  1,  2,  3,  4 };
                const int kLevels = 5;

                // Derive the level from the live values: exact pair first, then
                // the closest, so an env-var override still shows something sane.
                int level = 3;
                int best = 1 << 30;
                for (int i = 0; i < kLevels; ++i) {
                    const int d = std::abs(kSlices[i] - *st.cascadeCount) * 2 +
                                  std::abs(kDyn[i]    - *st.dynamicLayers);
                    if (d < best) { best = d; level = i; }
                }
                if (ImGui::Combo("Cascades", &level, kNames, kLevels)) {
                    *st.cascadeCount  = kSlices[level];
                    *st.dynamicLayers = kDyn[level];
                }
                help("How much detail the sun shadows get, HOW FAR THEY REACH, and "
                     "what they cost every frame.\n\n"
                     "READ THIS FIRST: fewer slices also means less DISTANCE. The "
                     "slices divide the view up to the furthest one's clip plane, and "
                     "beyond that plane the sun casts no shadow at all. So a low "
                     "setting does not merely look coarser -- pull the camera back far "
                     "enough and the sun's shadow disappears entirely. If sun shadows "
                     "have gone missing at distance, raise this before suspecting "
                     "anything else.\n\n"
                     "Each level sets two things at once: how many distance "
                     "slices the shadow is split into, and how many of those "
                     "slices also redraw the MOVING casters (characters, "
                     "creatures).\n\n"
                     "The moving half is the real per-frame cost -- static "
                     "casters are drawn once per area, moving ones every frame "
                     "in every slice they occupy. Distant creatures cast shadows "
                     "too small to see, so the lower levels are mostly free "
                     "quality.\n\n"
                     "Off = moving things cast no shadow at all. Static shadows "
                     "still come from the world-anchored map.\n\n"
                     "The current slice boundaries are the \"Cascade far clips\" "
                     "numbers in the Sun shadows section.");
                ImGui::TextDisabled("  %d slice%s, %d with moving casters",
                                    *st.cascadeCount, *st.cascadeCount == 1 ? "" : "s",
                                    *st.dynamicLayers);
                if (*st.cascadeCount < 4)
                    ImGui::TextDisabled("  sun shadows STOP past the last slice --\n"
                                        "  raise this if they vanish when zoomed out");
            }

            // "Cascade distance" = how far the crisp camera-fitted static
            // cascades reach before the world map takes over.
            if (st.staticNearCascades) {
                // "Off" removed with the Cascades one: 0 near cascades means no
                // crisp static shadows at all, which reads as a fault. The stored
                // value is still the cascade COUNT, so the combo index is
                // count-1 and 0 can no longer be selected.
                static const char* kQuality[] = { "Low", "Medium", "High", "Ultra" };
                int q = *st.staticNearCascades - 1;
                if (q < 0) q = 0;
                if (q > 3) q = 3;
                if (ImGui::Combo("Cascade distance", &q, kQuality, 4))
                    *st.staticNearCascades = q + 1;
                help("How far the CRISP static shadows reach.\n\n"
                     "Close to the camera, static shadows come from the cascades "
                     "(high detail). Past this distance they come from the "
                     "world-anchored map instead, which covers the whole area but "
                     "at lower detail.\n\n"
                     "Raise it if small objects near you look unshadowed; lower it "
                     "if you need the frames.\n\n"
                     "Capped by the cascade count, so Ultra means every cascade "
                     "gets crisp static shadows.");
            }
            // Fog fade is HIDDEN: it reads NWN's own fogParams uniform, so
            // there is nothing left to tune and an always-correct setting is
            // just noise in the panel. The manual start/end sliders went with
            // it -- they only ever existed because the range could not be read.
            // Still switchable for debugging with NWN_SHADOWMAP_FOG_FADE=0.
#if !NWN_SHIP
            if (st.cacheMove) {
                ImGui::SliderFloat("Refit after", st.cacheMove, 0.0f, 16.0f, "%.1f units");
                help("How far you must move before the near cascades are re-aimed "
                     "and static casters redrawn into them.\n\n"
                     "Larger = fewer redraws (faster) but shadows can lag slightly "
                     "behind you. Smaller = tighter but more work.");
            }
#endif   // !NWN_SHIP -- Refit after

            ImGui::Separator();
            ImGui::TextDisabled("Applied on the button below:");
            if (st.worldExtent) {
                // FOUR STEPS, and the top one is deliberately 256: that is a
                // 512-unit box, and the largest NWN area (32x32 tiles) is 320
                // units, so 256 fits ANY area and the map never rebuilds.
                // Past that the value only wastes resolution.
                static const float kExtents[] = { 32.0f, 64.0f, 128.0f, 256.0f };
                static const char* kExtentNames[] = {
                    "32 - sharpest, rebuilds often",
                    "64 - sharp, rebuilds sometimes",
                    "128 - balanced",
                    "256 - whole area, never rebuilds"
                };
                int ei = 2;
                for (int k = 0; k < 4; ++k)
                    if (*st.worldExtent >= kExtents[k] - 0.5f) ei = k;
                if (ImGui::Combo("World extent", &ei, kExtentNames, 4))
                    *st.worldExtent = kExtents[ei];
                help("How much world one static shadow map covers, as a half-width "
                     "in world units. This is a SHARPNESS vs STUTTER trade, not a "
                     "straight speed setting -- the map is the same size either "
                     "way, so it is only ever a question of how much ground that "
                     "same resolution is spread over.\n\n"
                     "SMALLER concentrates the resolution, so static shadows are "
                     "sharper -- but the map only covers ground near you, and has "
                     "to be rebuilt whenever you walk out of it. Each rebuild is "
                     "one slow frame, so a small extent means periodic hitching "
                     "while moving. \"Static map renders so far\" counts them.\n\n"
                     "LARGER spreads the same resolution over more ground, so "
                     "shadows are softer and blockier, but rebuilds become rare or "
                     "stop entirely. 256 covers a 512-unit box, and the largest "
                     "NWN area is 320 units, so it is built ONCE and never again "
                     "-- the smoothest option while moving.\n\n"
                     "Raise \"Static world map\" to get sharpness back at a large "
                     "extent; that costs VRAM rather than frame time.");
            }

            static const int kCascadeSizes[]  = { 512, 1024, 2048, 4096, 8192 };
            static const char* kCascadeNames[] = { "Low (512)", "Medium (1024)",
                                                   "High (2048)", "Ultra (4096)",
                                                   "Extreme (8192)" };
            static const int kWorldSizes[]  = { 512, 1024, 2048, 4096, 8192, 16384 };
            static const char* kWorldNames[] = { "Low (512)", "Medium (1024)",
                                                 "High (2048)", "Ultra (4096)",
                                                 "Extreme (8192)", "Mega High (16384)" };
            auto sizeCombo = [](const char* label, int* value,
                                const int* sizes, const char* const* names, int count) {
                if (!value) return;
                int idx = 0;
                for (int i = 0; i < count; ++i) if (sizes[i] == *value) idx = i;
                if (ImGui::Combo(label, &idx, names, count)) *value = sizes[idx];
            };
            sizeCombo("Cascade map", st.pendingCascadeSize, kCascadeSizes, kCascadeNames, 5);
            help("Resolution of each near-field cascade slice. Affects the "
                 "sharpness of shadows close to the camera.");
            sizeCombo("Static world map", st.pendingWorldSize, kWorldSizes, kWorldNames, 6);
            help("Resolution of the area-wide static map. It is drawn once per "
                 "area, so this is nearly free at runtime -- it costs VRAM and a "
                 "slightly longer rebuild, not frame time.\n\n"
                 "Mega High (16384) is about 1 GB.");

            if (st.localDynOnly) {
                bool dyn = *st.localDynOnly;
                if (ImGui::Checkbox("Dynamic casters only", &dyn)) *st.localDynOnly = dyn;
                help("Local lights shadow only MOVING geometry (creatures, the "
                     "player) rather than the whole world.\n\n"
                     "Loses little: a lamp fixed to a "
                     "wall throws only contact shadows off the static geometry "
                     "around it, which start at each object's own base and are "
                     "mostly hidden by that object from this camera.\n\n"
                     "Off = every bucket, for comparison.\n\n"
                     "This is a real saving now: the static buckets are simply not "
                     "replayed into the light's map, so it is two fewer passes per "
                     "light per frame. It could not work that way before -- the old "
                     "capture had to draw them anyway and wipe the depth.");
            }
            if (st.localMapSize) {
                // 256 and 512 are gone: with one wide face they were never good
                // enough to pick, and the ladder now starts where it is usable.
                static const int   kSizes[] = { 1024, 2048, 4096, 8192 };
                static const char* kNames[] = { "Low (1024)", "Medium (2048)",
                                                "High (4096)", "Ultra (8192)" };
                int idx = 0;
                for (int i = 0; i < 4; ++i) if (kSizes[i] == *st.localMapSize) idx = i;
                if (ImGui::Combo("Light casted shadow", &idx, kNames, 4))
                    *st.localMapSize = kSizes[idx];
                help("Resolution of each LIGHT's shadow map.\n\n"
                     "Every shadow-casting light gets its own map, so the cost "
                     "is this squared, times four bytes, times the number of "
                     "lights -- the figure below. Applied by the button "
                     "below, like the other two resolutions.\n\n"
                     "The figure below is the REAL allocation: one layer per light.\n\n"
                     "Ultra (8192) is 256 MB per light -- 768 MB at three. It is the "
                     "sharpest option and the intended top setting; watch the VRAM "
                     "readout below before applying it.");
                const unsigned layers = st.localSlots ? st.localSlots : 1;
                const double mb = (double)(*st.localMapSize) * (*st.localMapSize)
                                * 4.0 * (double)layers / (1024.0*1024.0);
                ImGui::TextDisabled("  %.0f MB for %u light(s)", mb, layers);
            }

            const bool dirty =
                (st.pendingCascadeSize && *st.pendingCascadeSize != st.cascadeSizeLive) ||
                (st.pendingWorldSize   && *st.pendingWorldSize   != st.worldSizeLive) ||
                (st.worldExtent        && *st.worldExtent        != st.worldExtentLive) ||
                (st.localMapSize       && *st.localMapSize       != st.localMapLive);
            ImGui::BeginDisabled(!dirty);
            if (ImGui::Button("Apply") && st.applyResolution) *st.applyResolution = true;
            ImGui::EndDisabled();
            if (dirty) { ImGui::SameLine(); ImGui::TextDisabled("pending"); }
            ImGui::Text("live: cascade %d, world %d, extent %.0f, light %d",
                        st.cascadeSizeLive, st.worldSizeLive, st.worldExtentLive,
                        st.localMapLive);
            ImGui::Text("Injector shadow targets: %.0f MiB", st.injectorTargetMiB);
            help("Exact storage estimate for the shadow textures owned by this injector: "
                 "static/dynamic cascades, the world map, copied scene depth, and "
                 "the local-light target when allocated.\n\n"
                 "It does not include NWN's textures, framebuffer attachments, driver "
                 "overhead, or other applications.");
            if (st.driverVramKnown) {
                const int used = st.driverVramTotalMiB > st.driverVramAvailableMiB
                    ? st.driverVramTotalMiB - st.driverVramAvailableMiB : 0;
                ImGui::Text("NVIDIA driver VRAM: %d / %d MiB used (%d MiB available)",
                            used, st.driverVramTotalMiB, st.driverVramAvailableMiB);
                help("NVIDIA's GL_NVX_gpu_memory_info driver pool. This is a driver-wide "
                     "figure, not exact memory used by NWN alone; it can include other GL "
                     "contexts and driver allocations.");
            } else {
                ImGui::TextDisabled("Driver VRAM: unavailable (requires NVIDIA NVX extension)");
            }
#if !NWN_SHIP
            ImGui::Text("Static map renders so far: %u", st.worldRenders);

            ImGui::Separator();
            ImGui::Text("Frame cost (GPU ms, last measured frame):");
            ImGui::Text("  receiver %.2f   world map %.2f", st.msReceiver, st.msWorldMap);
            ImGui::Text("  replay   %.2f   depth copy %.2f", st.msReplay, st.msSceneCopy);
            ImGui::Text("  engine draws this frame: %u", st.engineDraws);
            ImGui::Text("  replay %u call(s), %u draws", st.replayCalls, st.replayDraws);
            ImGui::Text("  fit cache: %u hit / %u refit%s",
                        st.fitCacheHits, st.fitRefits,
                        (st.fitCacheHits == 0 && st.fitRefits > 0)
                            ? "   <-- never hitting" : "");
#endif   // !NWN_SHIP -- frame cost
        }

#if !NWN_SHIP
        if (ImGui::CollapsingHeader("Diagnostics")) {
            if (st.receiverDebug) {
                const char* modes[] = {
                    "0 - off (normal shadows)",
                    "1 - solid green after background test",
                    "2 - red=sun, green=local, blue=local coverage",
                    "3 - solid magenta before any sampling",
                    "4 - local compare: red=occluded, green/blue=operands",
                    "5 - yellow local-shadow cutoff ring",
                };
                int mode = *st.receiverDebug;
                if (mode < 0 || mode > 5) mode = 0;
                if (ImGui::Combo("Receiver debug", &mode, modes, 6))
                    *st.receiverDebug = mode;
                ImGui::TextDisabled("3 proves the pass reaches the screen; 1/2 sit after\n"
                                    "the background discard and cannot show that.");
            }
        }
#endif   // !NWN_SHIP -- Diagnostics
        ImGui::Separator();
        if (st.resetDefaults) {
            if (ImGui::Button("Restore defaults")) *st.resetDefaults = true;
            help("Puts every setting back to the built-in values and SAVES that "
                 "immediately, so the reset survives a restart just like any "
                 "other edit.\n\n"
                 "These defaults are measured preferences, not neutral ones: "
                 "3+3 cascades looked better than 4+4 as well as costing less, "
                 "and world extent 128 keeps small objects' shadows that 256 "
                 "lost.\n\n"
                 "Deleting nwn_shadowmap_settings.ini beside the log does the "
                 "same thing from outside the game.");
        }
        ImGui::TextDisabled("Settings are saved automatically.");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
