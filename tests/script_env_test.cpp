// Offline test of the script-control message parser (src/shadow_script_env.inc).
// Builds the real module against a stub shadow_getenv; no game or GL needed.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static const char* shadow_getenv(const char* name) { return std::getenv(name); }
#include "../src/shadow_script_env.inc"

using namespace script_env;

static void send(const char* text) {
    // Same CExoString layout the hook reads: char* then uint32 length.
    struct { const char* p; uint32_t n; } exo{text, (uint32_t)std::strlen(text)};
    receive(&exo);
}

int main() {
    assert(f(FogDensity, 0.32f) == 0.32f);
    send("EOMENV1|fog=1|fog_density=0.6|heat=on|heat_strength = 1.5");
    assert(b(FogOn, false));
    assert(std::fabs(f(FogDensity, 0.32f) - 0.6f) < 1e-6f);
    assert(b(HeatOn, false));
    assert(std::fabs(f(HeatStrength, 1.0f) - 1.5f) < 1e-6f);
    // Clamped to range.
    send("EOMENV1;rain_wind_x=99;fog_density=-3");
    assert(f(RainWindX, 0.0f) == 8.0f);
    assert(f(FogDensity, 0.32f) == 0.0f);
    // A malformed message changes nothing.
    send("EOMENV1|fog_density=0.9|nonsense_key=1");
    assert(f(FogDensity, 0.32f) == 0.0f);
    send("EOMENV1|fog_density=abc");
    assert(f(FogDensity, 0.32f) == 0.0f);
    send("NOTENV|fog_density=0.9");
    assert(f(FogDensity, 0.32f) == 0.0f);
    // clear and reset return the player's own value.
    send("EOMENV1|clear=fog_density");
    assert(f(FogDensity, 0.32f) == 0.32f);
    assert(b(FogOn, false));
    send("EOMENV1|reset|rain=0");
    assert(!b(FogOn, false) && !b(HeatOn, false));
    assert(!b(RainOn, true));
    char text[640];
    summary(text, sizeof(text));
    assert(std::strcmp(text, "rain=off") == 0);
    send("EOMENV1|reset");
    summary(text, sizeof(text));
    assert(std::strcmp(text, "none") == 0);
    std::printf("PASS: script control parser\n");
    return 0;
}
