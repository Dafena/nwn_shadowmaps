#include "../nwn_shadowmap.cpp"
#include <cassert>
int main() {
    EngineShadowLightEntry e{};
    e.key = reinterpret_cast<void*>(123);
    e.info.radius = 20;
    e.info.fade = 1;
    assert(local_slot_fade(0) == 0);
    assert(local_slot_fade(.5f) == .5f);
    assert(local_slot_fade(1) == 1);
    assert(local_slot_fade(NAN) == 1);
    g_localJoinLast = 1;
    update_local_join_fades(&e, 1, 1, 10);
    assert(g_localJoin[0].level == 0); // New source must not inherit idle time.
    update_local_join_fades(&e, 1, 1, 10.175);
    assert(g_localJoin[0].level > .49f && g_localJoin[0].level < .51f);
    update_local_join_fades(nullptr, 0, 1, 10.25);
    assert(!g_localJoin[0].live && g_localJoin[0].level > 0);
    update_local_join_fades(nullptr, 0, 1, 10.6);
    assert(!g_localJoin[0].key);
    reset_engine_shadow_light_anchor();
    assert(g_localJoinLast == 0);
}
