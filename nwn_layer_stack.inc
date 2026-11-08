// Opt-in ordered continuous-alpha layers. Linux is the visual reference;
// Windows uses separately verified iterator exports/layout and hook readiness.
// No geometry, colour, blend function, depth function or cull state is changed.
#include "nwn_layer_stack_plan.h"

namespace layer_stack {
static void* current_part = nullptr;
static int submission_pass = 0;
static int draw_pass = 0;
static void* active_scene = nullptr;
static int active_bucket = -1;
static nwn_layer_stack::Registry registry;
#ifndef _WIN32
static subhook_t part_draw_hook = nullptr;
#endif
#ifdef _WIN32
static eng::LayerStackPartDraw_t windows_part_draw = nullptr;
static bool install_windows_part_draw();
#endif
static GLint alpha_location = -1;
static GLuint alpha_program = 0;
static GLfloat saved_alpha = 0;
static GLboolean saved_depth_mask = GL_TRUE;
static bool changed = false;
struct BucketScope {
    void* scene; int bucket; void* part; int pass;
    BucketScope(void* next_scene, int next_bucket)
        : scene(active_scene), bucket(active_bucket), part(current_part), pass(submission_pass) {
        active_scene = next_scene; active_bucket = next_bucket;
        current_part = nullptr; submission_pass = 0;
        if (next_bucket == 1 && !g_inOurPass && !g_cascadeReplayActive &&
            !g_localLightPassActive && !nwn_core::g_ownedPass)
            registry.begin(next_scene);
    }
    ~BucketScope() {
        active_scene = scene; active_bucket = bucket;
        current_part = part; submission_pass = pass;
    }
};

static bool visible_context() {
#ifdef _WIN32
    // GetFirstItem consumes an item on Windows. Do not enable scheduling with
    // only half the required adapter installed.
    if (!windows_part_draw || !g_hookDrawBucketFirst || !g_hookDrawBucketNext)
        return false;
#endif
    // The native bucket call is authoritative even when optional shadow trace
    // mode is disabled and g_areaScene has not been selected. Only publishing
    // shaders can opt in; never infer a layer from viewport size or distance.
    return active_scene && active_bucket == 1 && !g_inOurPass &&
        !g_overlayPassActive && !g_inStencilShadow && !g_cascadeReplayActive &&
        !g_localLightPassActive && !nwn_core::g_ownedPass;
}
static int priority(void* part) {
    return registry.priority(active_scene, part);
}
static void part_draw_detour(void* part) {
    if (visible_context() && !nwn_layer_stack::submit_cached_pass(priority(part), submission_pass))
        return;
    void* previous_part = current_part;
    const int previous_pass = draw_pass;
    current_part = part; draw_pass = submission_pass;
#ifdef _WIN32
    if (windows_part_draw) windows_part_draw(part);
#else
    if (void* trampoline = subhook_get_trampoline(part_draw_hook))
        reinterpret_cast<eng::LayerStackPartDraw_t>(trampoline)(part);
    else {
        subhook_remove(part_draw_hook);
        eng::LayerStackPartDraw(part);
        subhook_install(part_draw_hook);
    }
#endif
    current_part = previous_part; draw_pass = previous_pass;
}
#ifdef _WIN32
static bool install_windows_part_draw() {
    // The exported Draw prologue cannot be safely relocated by Subhook.
    // v89.8193.37-17 has exactly one pointer to this export in .rdata: the
    // PartTriMesh virtual Draw slot (RVA0xd883d0 in the verified executable).
    // Discover that unique slot rather than changing code or assuming an RVA.
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base || !eng::LayerStackPartDraw) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
    auto* sections = IMAGE_FIRST_SECTION(nt);
    void** slot = nullptr;
    const void* target = reinterpret_cast<void*>(eng::LayerStackPartDraw);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        if (std::memcmp(section.Name, ".rdata", 6) ||
            section.VirtualAddress >= nt->OptionalHeader.SizeOfImage ||
            section.Misc.VirtualSize > nt->OptionalHeader.SizeOfImage - section.VirtualAddress)
            continue;
        auto* data = base + section.VirtualAddress;
        for (size_t at = sizeof(void*); at + 2*sizeof(void*) <= section.Misc.VirtualSize; at += sizeof(void*)) {
            void* value = nullptr; std::memcpy(&value, data + at, sizeof(value));
            if (value != target) continue;
            if (slot) return false; // ambiguity is a native fallback
            slot = reinterpret_cast<void**>(data + at);
        }
    }
    if (!slot) return false;
    DWORD previous = 0, ignored = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) return false;
    windows_part_draw = eng::LayerStackPartDraw;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),
                               reinterpret_cast<void*>(part_draw_detour));
    if (!VirtualProtect(slot, sizeof(*slot), previous, &ignored)) {
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), const_cast<void*>(target));
        VirtualProtect(slot, sizeof(*slot), previous, &ignored);
        windows_part_draw = nullptr;
        return false;
    }
    fprintf(stderr, "[shadowmap][layer-stack] Windows virtual Draw slot active at %p; native code unchanged\n", slot);
    return true;
}
#endif
static bool iterator(void* self, int bucket, int ordering, const void** item, bool first) {
    submission_pass = 0;
    if (!self || bucket != 1 || ordering < 0 || ordering > 1 || !visible_context()) return false;
    // v89 Linux and Windows both have 16-byte lists and cursor+0x308.
    // Windows GetFirstItem RVA0xd94f0 also resets+0x300 before consuming item0.
    struct List { void** items; int count; int capacity; };
    static_assert(sizeof(List) == 16, "Linux draw-list ABI");
    auto manager = static_cast<unsigned char*>(self);
#ifdef _WIN32
    if (first) {
        const uint64_t zero = 0;
        std::memcpy(manager + 0x300, &zero, sizeof(zero));
        const int start = 0;
        std::memcpy(manager + 0x308, &start, sizeof(start));
    }
#else
    (void)first;
#endif
    int cursor = 0; std::memcpy(&cursor, manager + 0x308, sizeof(cursor));
    static void* owner = nullptr;
    static int active_ordering = -1;
    static std::vector<nwn_layer_stack::Draw> draws;
    if (cursor == 0) {
        GLboolean colour[4] = {};
        gl::GetBooleanv(GL_COLOR_WRITEMASK, colour);
        if (!colour[0] && !colour[1] && !colour[2]) {
            owner = nullptr; active_ordering = -1; return false;
        }
        List lists[2] = {};
        for (int j = 0; j < 2; ++j) {
            std::memcpy(&lists[j], manager + 16 * (3 * bucket + j), sizeof(List));
            if (lists[j].count < 0 || lists[j].count > 65536 ||
                (lists[j].count && !lists[j].items)) return false;
        }
        std::vector<nwn_layer_stack::Item> input[2];
        bool opted = false;
        for (int j = 0; j < 2; ++j) for (int i = 0; i < lists[j].count; ++i) {
            void* p = lists[j].items[i]; int hint = priority(p);
            opted |= hint >= 0; input[j].push_back({p, hint});
        }
        if (!opted) { owner = nullptr; return false; }
        draws = nwn_layer_stack::plan(input[0], input[1], ordering);
        owner = self; active_ordering = ordering;
    }
    if (owner != self || active_ordering != ordering || cursor < 0) return false;
    static void* end = nullptr;
    if (cursor >= int(draws.size())) { *item = &end; return true; }
    submission_pass = draws[cursor].pass;
    *item = &draws[cursor].part;
    ++cursor; std::memcpy(manager + 0x308, &cursor, sizeof(cursor));
    return true;
}
#if !NWN_SHIP
// Optional one-frame native evidence. No framebuffer readback or rendering
// decision uses these files. Absent the env flag this is a cheap null check.
static void trace_draw(GLuint program, int hint) {
    static const char* directory = shadow_getenv("NWN_LAYER_STACK_TRACE");
    if (!directory) return;
    static long polled_frame = -1, initial_frame = -1, capture_frame = -1;
    static char active[32] = {};
    static bool complete = true;
    char path[4096];
    if (polled_frame != g_frames) {
        polled_frame = g_frames;
        std::snprintf(path, sizeof(path), "%s/request.txt", directory);
        FILE* request = std::fopen(path, "r"); char label[32] = {};
        const bool parsed = request && std::fscanf(request, "%31s", label) == 1;
        if (request) std::fclose(request);
        if (parsed) {
            for (char c : label) if (c && !((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return;
            if (std::strcmp(active, label)) {
                std::strcpy(active, label); initial_frame = g_frames;
                capture_frame = -1; complete = false;
            }
        }
    }
    if (complete || g_frames == initial_frame) return;
    if (capture_frame < 0) capture_frame = g_frames;
    if (g_frames > capture_frame) { complete = true; return; }
    std::snprintf(path, sizeof(path), "%s/%s.jsonl", directory, active);
    FILE* file = std::fopen(path, "a"); if (!file) return;
    GLboolean mask = GL_TRUE; gl::GetBooleanv(GL_DEPTH_WRITEMASK, &mask);
    std::fprintf(file, "{\"frame\":%ld,\"priority\":%d,\"pass\":%d,\"depth_write\":%d,\"depth_test\":%d,\"program\":%u",
                 g_frames, hint, draw_pass, int(mask), int(gl::IsEnabled(GL_DEPTH_TEST)), program);
    for (const char* name : {"lightAreaAmbient", "lightAreaDiffuse", "numLights", "staticLighting", "lightPosition[0]", "lightColor[0]"}) {
        const GLint at = gl::GetUniformLocation(program, name); if (at < 0) continue;
        GLfloat value[4] = {}; gl::GetUniformfv(program, at, value);
        const int count = !std::strncmp(name, "lightArea", 9) ? 3 :
            (!std::strcmp(name, "numLights") || !std::strcmp(name, "staticLighting")) ? 1 : 4;
        std::fprintf(file, ",\"%s\":[", name);
        for (int i = 0; i < count; ++i) std::fprintf(file, "%s%.8g", i ? "," : "", value[i]);
        std::fprintf(file, "]");
    }
    for (const char* name : {"m_mv", "m_proj"}) {
        const GLint at = gl::GetUniformLocation(program, name); if (at < 0) continue;
        GLfloat value[16] = {}; gl::GetUniformfv(program, at, value);
        std::fprintf(file, ",\"%s\":[", name);
        for (int i = 0; i < 16; ++i) std::fprintf(file, "%s%.9g", i ? "," : "", value[i]);
        std::fprintf(file, "]");
    }
    std::fprintf(file, "}\n"); std::fclose(file);
}
#endif
static void before_draw() {
    if (changed || !visible_context() || !current_part || !gl::GetUniformLocation || !gl::GetUniformfv) return;
    GLint program = 0; gl::GetIntegerv(GL_CURRENT_PROGRAM, &program);
    GLint loc = gl::GetUniformLocation(program, "wmLayerAlphaPass");
    if (loc < 0) loc = gl::GetUniformLocation(program, "wmDiagAlphaPass"); // accepted prototype
    const GLint meta = gl::GetUniformLocation(program, "wmStackPriority");
    int hint = -1;
    if (loc >= 0 && meta >= 0) {
        GLfloat value = -1; gl::GetUniformfv(program, meta, &value);
        if (value >= 0 && value <= 9 && value == GLfloat(int(value))) hint = int(value);
    }
    GLboolean colour[4] = {};
    gl::GetBooleanv(GL_COLOR_WRITEMASK, colour);
    if (!colour[0] && !colour[1] && !colour[2]) return;
    registry.remember(active_scene, current_part, hint);
    static bool reported = false;
    if (hint >= 0 && !reported) {
        reported = true;
        fprintf(stderr, "[shadowmap][layer-stack] explicit0..9 shader layers active; native ties; core then fade per hint\n");
    }
    // One native warm-up frame discovers shader capabilities. Refresh above on
    // every draw so a resource/save reload cannot freeze an old material hint.
    if (hint < 0 || !draw_pass || !gl::DepthMask || !gl::Uniform1f) return;
    gl::GetUniformfv(program, loc, &saved_alpha);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &saved_depth_mask);
    alpha_program = GLuint(program); alpha_location = loc; changed = true;
    gl::Uniform1f(loc, GLfloat(draw_pass));
    gl::DepthMask(draw_pass == 1 ? GL_TRUE : GL_FALSE);
#if !NWN_SHIP
    trace_draw(GLuint(program), hint);
#endif
}
static void after_draw() {
    if (!changed) return;
    GLint program = 0; gl::GetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (GLuint(program) != alpha_program) restore_engine_program(alpha_program);
    gl::Uniform1f(alpha_location, saved_alpha);
    if (GLuint(program) != alpha_program) restore_engine_program(GLuint(program));
    gl::DepthMask(saved_depth_mask);
    changed = false; alpha_location = -1;
}
} // namespace layer_stack
