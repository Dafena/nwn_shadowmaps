#pragma once
#include <vector>
#include <unordered_map>

// Pure scheduling policy, shared by the runtime module and regression test.
namespace nwn_layer_stack {
struct Item { void* part; int priority; }; // negative: unmarked native item
struct Draw { void* part; int pass; int priority; };
// Discovery belongs to the currently rendered scene, not the process lifetime.
// Parts not seen in the preceding frame are discarded before pointer reuse.
class Registry {
    struct Entry { int priority; unsigned long frame; };
    void* scene_ = nullptr;
    unsigned long frame_ = 0;
    std::unordered_map<void*, Entry> entries_;
public:
    void begin(void* scene, unsigned long frame) {
        if (scene != scene_ || frame < frame_) {
            entries_.clear(); scene_ = scene;
        } else if (frame != frame_) {
            for (auto it = entries_.begin(); it != entries_.end(); )
                if (frame > it->second.frame + 1) it = entries_.erase(it);
                else ++it;
        }
        frame_ = frame;
    }
    int priority(void* part) const {
        auto it = entries_.find(part);
        return it == entries_.end() ? -1 : it->second.priority;
    }
    void remember(void* part, int priority) {
        if (priority < 0 || priority > 9) entries_.erase(part);
        else entries_[part] = {priority, frame_};
    }
    std::size_t size() const { return entries_.size(); }
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
