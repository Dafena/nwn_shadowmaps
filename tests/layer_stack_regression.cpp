#include "nwn_layer_stack_plan.h"
#include <cassert>
#include <iostream>
#include <tuple>

int main() {
    int base, overlay, high, solid, native;
    // Exact failure: native base core, overlay core, base fade, overlay fade.
    // Required: finish the whole lower hint before starting the higher hint.
    for (int hint : {1, 2, 9}) {
        auto a = nwn_layer_stack::plan({{&base, 0}, {&solid, -1}},
                               {{&overlay, hint}, {&native, -1}}, 0);
        auto b = nwn_layer_stack::plan({{&base, 0}, {&solid, -1}},
                               {{&overlay, hint}, {&native, -1}}, 1);
        a.insert(a.end(), b.begin(), b.end());
        assert(a.size() == 6);
        std::vector<nwn_layer_stack::Draw> layers;
        for (auto d : a) if (d.priority >= 0) layers.push_back(d);
        assert(layers[0].part == &base && layers[0].pass == 1);
        assert(layers[1].part == &base && layers[1].pass == 2);
        assert(layers[2].part == &overlay && layers[2].pass == 1);
        assert(layers[3].part == &overlay && layers[3].pass == 2);
        assert(a[0].part == &solid && a[0].pass == 0);
        assert(a[1].part == &native && a[1].pass == 0);
        assert(a[2].part == &base && a[2].pass == 1);
        assert(a[3].part == &base && a[3].pass == 2);
        assert(a[4].part == &overlay && a[4].pass == 1);
        assert(a[5].part == &overlay && a[5].pass == 2);
    }
    auto draws = nwn_layer_stack::plan({{&high, 9}, {&base, 0}}, {{&overlay, 1}}, 1);
    int previous = -1;
    for (auto d : draws) { assert(d.priority >= previous); previous = d.priority; }
    auto tied = nwn_layer_stack::plan({}, {{&base, 1}, {&overlay, 1}}, 1);
    assert(tied.size() == 4);
    assert(tied[0].pass == 1 && tied[1].pass == 1);
    assert(tied[2].pass == 2 && tied[3].pass == 2);
    assert(nwn_layer_stack::plan({}, {}, 1).empty());
    std::cout << "PASS: complete ascending hint groups; equal hints remain tied; native solids once; no lost/extra Parts\n";
}
