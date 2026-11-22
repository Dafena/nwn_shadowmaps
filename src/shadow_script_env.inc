// ---------------------------------------------------------------------------
// Script control of rain, fog and heat haze.
//
// A server script sends a per-player TLK custom token (NWNX
// NWNX_Player_SetCustomToken) on the reserved number kTokenNumber. The client
// stores it through CTlkTable::SetCustomToken, which the token hook in
// shadow_drawing_probe.inc observes; the original setter always runs first.
//
// Message format (ASCII, '|' or ';' separated, spaces ignored):
//     EOMENV1|reset|fog=1|fog_density=0.6|clear=rain_wind_x
//   reset        drop every script override (the player's own settings return)
//   clear=key    drop one override
//   key=value    override one setting; value is clamped to the key's range,
//                switches accept 0/1/on/off/true/false
// A message is applied whole or not at all, so a typo cannot half-apply it.
//
// Overrides change the LOOK of an area. Quality settings that cost
// performance (volume resolution, samples, rain budget) stay the player's.
// Script values are never written to the player's settings file: the panel
// and the .ini keep the player's values, and every renderer reads the
// effective value through f()/b() below.
// ---------------------------------------------------------------------------
#include <mutex>

namespace script_env {

constexpr int kTokenNumber = 910010;

enum Id {
    FogOn, FogDensity, FogBrightness, FogHeightFalloff, FogHeightBase,
    FogShafts, FogAnisotropy, FogLampGlow,
    RainOn, RainWindX, RainWindY, RainSplashes, RainRadius,
    HeatOn, HeatStrength, HeatScale, HeatSpeed, HeatStart, HeatBase,
    HeatFalloff, HeatFire,
    Count
};

struct Entry { const char* key; float lo, hi; bool flag; };
static constexpr Entry kEntries[Count] = {
    {"fog",                0.0f,   1.0f, true },
    {"fog_density",        0.0f,   1.0f, false},
    {"fog_brightness",     0.0f,   1.5f, false},
    {"fog_height_falloff", 0.0f,   0.5f, false},
    {"fog_height_base",  -50.0f, 100.0f, false},
    {"fog_shafts",         0.0f,   4.0f, false},
    {"fog_anisotropy",     0.0f,   0.9f, false},
    {"fog_lamp_glow",      0.0f,   3.0f, false},
    {"rain",               0.0f,   1.0f, true },
    {"rain_wind_x",       -8.0f,   8.0f, false},
    {"rain_wind_y",       -8.0f,   8.0f, false},
    {"rain_splashes",      0.0f,   2.0f, false},
    {"rain_radius",        8.0f,  64.0f, false},
    {"heat",               0.0f,   1.0f, true },
    {"heat_strength",      0.0f,   2.0f, false},
    {"heat_scale",         0.25f,  4.0f, false},
    {"heat_speed",         0.0f,   3.0f, false},
    {"heat_start",         0.0f, 100.0f, false},
    {"heat_base",        -50.0f, 100.0f, false},
    {"heat_falloff",       0.0f,   1.0f, false},
    {"heat_fire",          0.0f,   2.0f, false},
};

static std::mutex g_mutex;
static bool  g_set[Count] = {};
static float g_value[Count] = {};
static char  g_summary[640] = "none";
static unsigned g_accepted = 0, g_rejected = 0;

static bool enabled() {
    static const bool on = [] {
        const char* v = shadow_getenv("NWN_SHADOWMAP_SCRIPT_CONTROL");
        return !(v && std::strcmp(v, "0") == 0);
    }();
    return on;
}

static float f(Id id, float user) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_set[id] ? g_value[id] : user;
}
static bool b(Id id, bool user) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_set[id] ? g_value[id] > 0.5f : user;
}
// Panel readout. Copied under the lock into the caller's buffer.
static void summary(char* out, size_t size) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::snprintf(out, size, "%s", g_summary);
}

static void rebuild_summary_locked() {
    size_t used = 0;
    g_summary[0] = '\0';
    for (int i = 0; i < Count; ++i) {
        if (!g_set[i]) continue;
        char item[64];
        if (kEntries[i].flag)
            std::snprintf(item, sizeof(item), "%s%s=%s", used ? ", " : "",
                          kEntries[i].key, g_value[i] > 0.5f ? "on" : "off");
        else
            std::snprintf(item, sizeof(item), "%s%s=%.3g", used ? ", " : "",
                          kEntries[i].key, g_value[i]);
        const size_t len = std::strlen(item);
        if (used + len + 4 >= sizeof(g_summary)) {
            std::snprintf(g_summary + used, sizeof(g_summary) - used, ", ...");
            return;
        }
        std::memcpy(g_summary + used, item, len + 1);
        used += len;
    }
    if (!used) std::snprintf(g_summary, sizeof(g_summary), "none");
}

static int find_key(const char* key) {
    for (int i = 0; i < Count; ++i)
        if (std::strcmp(kEntries[i].key, key) == 0) return i;
    return -1;
}

static bool parse_flag(const char* s, float& out) {
    char low[8] = {};
    for (int i = 0; i < 7 && s[i]; ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        low[i] = c;
    }
    if (!std::strcmp(low, "1") || !std::strcmp(low, "on") || !std::strcmp(low, "true")) { out = 1.0f; return true; }
    if (!std::strcmp(low, "0") || !std::strcmp(low, "off") || !std::strcmp(low, "false")) { out = 0.0f; return true; }
    return false;
}

// Parses and applies one message. Returns false (and changes nothing) on any
// malformed item.
static bool apply(const char* text) {
    if (std::strncmp(text, "EOMENV1", 7) != 0) return false;
    bool reset = false;
    bool clear[Count] = {};
    bool set[Count] = {};
    float value[Count] = {};
    const char* p = text + 7;
    char item[96];
    while (*p) {
        while (*p == '|' || *p == ';' || *p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        size_t n = 0;
        while (p[n] && p[n] != '|' && p[n] != ';') ++n;
        if (n >= sizeof(item)) { ++g_rejected; return false; }
        // Copy without spaces so "fog_density = 0.6" also works.
        size_t k = 0;
        for (size_t i = 0; i < n; ++i)
            if (p[i] != ' ' && p[i] != '\t') item[k++] = p[i];
        item[k] = '\0';
        p += n;
        if (!k) continue;
        if (!std::strcmp(item, "reset")) { reset = true; continue; }
        char* eq = std::strchr(item, '=');
        if (!eq) { ++g_rejected; return false; }
        *eq = '\0';
        const char* key = item;
        const char* val = eq + 1;
        if (!std::strcmp(key, "clear")) {
            const int id = find_key(val);
            if (id < 0) { ++g_rejected; return false; }
            clear[id] = true; set[id] = false;
            continue;
        }
        const int id = find_key(key);
        if (id < 0 || !*val) { ++g_rejected; return false; }
        float v = 0.0f;
        if (kEntries[id].flag) {
            if (!parse_flag(val, v)) { ++g_rejected; return false; }
        } else {
            char* end = nullptr;
            v = std::strtof(val, &end);
            if (!end || *end || !std::isfinite(v)) { ++g_rejected; return false; }
            v = std::max(kEntries[id].lo, std::min(kEntries[id].hi, v));
        }
        set[id] = true; clear[id] = false; value[id] = v;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (reset)
            for (int i = 0; i < Count; ++i) g_set[i] = false;
        for (int i = 0; i < Count; ++i) {
            if (clear[i]) g_set[i] = false;
            if (set[i]) { g_set[i] = true; g_value[i] = value[i]; }
        }
        rebuild_summary_locked();
        ++g_accepted;
    }
    return true;
}

// Called from the token hook with the raw CExoString (pointer, uint32 size).
static void receive(const void* text) {
    if (!enabled() || !text) return;
    const char* value = nullptr;
    uint32_t size = 0;
    std::memcpy(&value, text, sizeof(value));
    std::memcpy(&size, static_cast<const char*>(text) + sizeof(value), sizeof(size));
    if (!value || !size || size > 2048) return;
    char bounded[2049] = {};
    std::memcpy(bounded, value, size);
    bounded[size] = '\0';
    const bool ok = apply(bounded);
    char current[640];
    summary(current, sizeof(current));
    // Bounded log: the first few messages, then every rejection up to a cap.
    static unsigned logged = 0;
    if (logged < 32) {
        ++logged;
        fprintf(stderr, "[shadowmap][script] %s token %d (%u bytes); overrides: %s\n",
                ok ? "applied" : "REJECTED", kTokenNumber, (unsigned)size, current);
    }
}

} // namespace script_env
