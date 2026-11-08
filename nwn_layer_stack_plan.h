#pragma once
#include <vector>

// Pure scheduling policy, shared by the runtime module and regression test.
namespace nwn_layer_stack {
struct Item { void* part; int priority; }; // negative: unmarked native item
struct Draw { void* part; int pass; int priority; };
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
