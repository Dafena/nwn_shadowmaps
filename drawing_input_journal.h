#pragma once
#include <cstdint>

namespace drawing_probe {
// Diagnostic sampling only. Movement cannot consume the separate control budget.
// The runtime serializes this state; there is no input interception or ink here.
struct InputJournal {
    static constexpr unsigned movement_limit=4096, control_limit=1024;
    static constexpr std::uint64_t movement_interval_us=8333; // at most 120 Hz
    unsigned movements=0, controls=0;
    std::uint64_t lastMove=0;
    bool held=false;

    bool record(int kind,std::uint64_t time) {
        if(kind==0) {
            if(!held || movements>=movement_limit || time<lastMove ||
               time-lastMove<movement_interval_us)return false;
            ++movements;lastMove=time;return true;
        }
        if(kind<1 || kind>3 || controls>=control_limit)return false;
        // Linux's event and polling adapters can observe the same transition.
        if((kind==1 && held) || (kind==2 && !held))return false;
        held=kind==1;++controls;lastMove=time;return true;
    }
};
}
