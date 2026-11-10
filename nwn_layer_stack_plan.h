#pragma once
#include <vector>
#include <unordered_map>

// Pure scheduling policy, shared by the runtime module and regression test.
namespace nwn_layer_stack {
struct Item { void* part; int priority; }; // negative: unmarked native item
struct Draw { void* part; int pass; int priority; };
// Each scene advances only when its own native bucket is visited. GUI/skybox
// renders cannot age the area's discoveries. Keep a bounded LRU of scenes.
class Registry {
    struct Entry { int priority; unsigned long generation; };
    struct SceneEntries {
        unsigned long generation = 0, visited = 0;
        std::unordered_map<void*, Entry> parts;
    };
    unsigned long visit_ = 0;
    std::unordered_map<void*, SceneEntries> scenes_;
public:
    void begin(void* scene) {
        if (!scenes_.count(scene) && scenes_.size() >= 32) {
            auto oldest = scenes_.begin();
            for (auto it = scenes_.begin(); it != scenes_.end(); ++it)
                if (it->second.visited < oldest->second.visited) oldest = it;
            scenes_.erase(oldest);
        }
        auto& state = scenes_[scene];
        state.visited = ++visit_;
        ++state.generation;
        for (auto it = state.parts.begin(); it != state.parts.end(); )
            if (state.generation > it->second.generation + 1) it = state.parts.erase(it);
            else ++it;
    }
    int priority(void* scene, void* part) const {
        auto state = scenes_.find(scene);
        if (state == scenes_.end()) return -1;
        auto it = state->second.parts.find(part);
        return it == state->second.parts.end() ? -1 : it->second.priority;
    }
    void remember(void* scene, void* part, int priority) {
        auto state = scenes_.find(scene);
        if (state == scenes_.end()) return;
        if (priority < 0 || priority > 9) state->second.parts.erase(part);
        else state->second.parts[part] = {priority, state->second.generation};
    }
    std::size_t size(void* scene) const {
        auto state = scenes_.find(scene);
        return state == scenes_.end() ? 0 : state->second.parts.size();
    }
    std::size_t scene_count() const { return scenes_.size(); }
};
inline bool submit_cached_pass(int priority, int pass) {
    // A shader which lost its opt-in during the first submission must not be
    // drawn natively a second time by an already-constructed plan.
    return pass != 2 || priority >= 0;
}
inline std::vector<Draw> plan(const std::vector<Item>& first,
                              const std::vector<Item>& second, int ordering) {
    std::vector<Draw> result;
    const auto& current = ordering == 0 ? first : second;
    for (auto i : current)
        if (i.priority < 0 || i.priority > 9) result.push_back({i.part, 0, -1});
    // The engine visits two sublists. Native items retain their sublist/order.
    // Finish each opted-in priority (opaque core, then fade) before the next.
    // Equal hints keep the engine's tie order, without an authored tie-break.
    if (ordering == 1)
        for (int priority = 0; priority <= 9; ++priority)
            for (int pass = 1; pass <= 2; ++pass)
                for (const auto* list : {&first, &second})
                    for (auto i : *list)
                        if (i.priority == priority) result.push_back({i.part, pass, priority});
    return result;
}
}
