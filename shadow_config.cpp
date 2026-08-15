#include "shadow_config.h"

#include <cstdlib>
#include <cstring>

#include "nwn_platform.h"

#if NWN_SHIP
// Shipping defaults are consulted only when the process environment does not
// provide a value. The environment therefore remains authoritative.
static const char* const kShippingDefaultEnv[][2] = {
    {"NWN_SHADOWMAP_TRACE",                         "1"},
    {"NWN_SHADOWMAP_CASCADE_LIGHT_CAPTURE",         "1"},
    {"NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET",          "0"},
    {"NWN_SHADOWMAP_STATIC_RECEIVER",               "1"},
    {"NWN_SHADOWMAP_STATIC_ALPHA_RECEIVER",         "1"},
    {"NWN_SHADOWMAP_STATIC_ALPHA_BUCKET",           "1"},
    {"NWN_SHADOWMAP_CASTER_FULL_BSP_NATIVE_SUBMIT", "1"},
    {"NWN_SHADOWMAP_CSM_STATIC_RECEIVER",           "1"},
    {"NWN_SHADOWMAP_CSM_DYNAMIC_RECEIVER",          "1"},
    {"NWN_SHADOWMAP_CSM_ALPHA_RECEIVER",            "1"},
    {"NWN_SHADOWMAP_CSM_COMPOSITE",                 "1"},
    {"NWN_SHADOWMAP_CSM_BUCKET_REPLAY",             "1"},
    {"NWN_SHADOWMAP_CSM_STRENGTH",                  "0.42"},
    {"NWN_SHADOWMAP_CSM_BLEND",                     "0.75"},
    {"NWN_SHADOWMAP_CSM_PCF_RADIUS",                "0.75"},
    {"NWN_SHADOWMAP_CSM_CASCADES",                  "3"},
    {"NWN_SHADOWMAP_CSM_DYNAMIC_CASCADES",          "3"},
    {"NWN_SHADOWMAP_STATIC_NEAR_CASCADES",          "4"},
    {"NWN_SHADOWMAP_SIZE",                          "2048"},
    {"NWN_SHADOWMAP_STATIC_WORLD",                  "1"},
    {"NWN_SHADOWMAP_STATIC_WORLD_SIZE",             "8192"},
    {"NWN_SHADOWMAP_STATIC_WORLD_EXTENT",           "128"},
    {"NWN_SHADOWMAP_LOCAL_LIGHT_TRACE",             "1"},
    {"NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE",           "1"},
    {"NWN_SHADOWMAP_LOCAL_LIGHT_RECEIVER",          "1"},
    {"NWN_SHADOWMAP_MAX_LAMPS",                     "8"},
    {"NWN_SHADOWMAP_LOCAL_LIGHT_SIZE",              "256"},
};
#endif

unsigned shadow_default_env_count() {
#if NWN_SHIP
    return static_cast<unsigned>(sizeof(kShippingDefaultEnv) /
                                 sizeof(kShippingDefaultEnv[0]));
#else
    return 0;
#endif
}

static const char* shadow_getenv_uncached(const char* name) {
    if (const char* value = std::getenv(name)) return value;
#if NWN_SHIP
    for (const auto& item : kShippingDefaultEnv)
        if (std::strcmp(item[0], name) == 0) return item[1];
#endif
    return nullptr;
}

const char* shadow_getenv(const char* name) {
    if (!name) return nullptr;

    struct Entry {
        const char* key;
        const char* value;
    };
    static Entry cache[256];
    static unsigned used = 0;

    // Call sites overwhelmingly pass string literals, so pointer identity is
    // the fast path. strcmp handles identical literals emitted at distinct
    // addresses and aliases that pointer for future calls.
    for (unsigned i = 0; i < used; ++i)
        if (cache[i].key == name) return cache[i].value;
    for (unsigned i = 0; i < used; ++i)
        if (std::strcmp(cache[i].key, name) == 0) {
            const char* value = cache[i].value;
            if (used < sizeof(cache) / sizeof(cache[0]))
                cache[used++] = {name, value};
            return value;
        }

    const char* value = shadow_getenv_uncached(name);
    if (used < sizeof(cache) / sizeof(cache[0]))
        cache[used++] = {name, value};
    return value;
}
