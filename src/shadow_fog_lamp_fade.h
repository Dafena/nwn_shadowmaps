#pragma once
#include <algorithm>
#include <cmath>

// Fog presentation only: engine light ownership/capture are never changed.
namespace fog_lamp_fade {
inline constexpr unsigned kCapacity = 128;
struct Light {
    const void* key = nullptr;
    float pos[3] = {}, radius = 0, color[3] = {};
};
struct Slot { Light light; float weight = 0; bool confirmed = false; };
struct History {
    Slot slots[kCapacity] = {};
    const void* scene = nullptr;
    const void* area = nullptr;
    double last = -1;
    void reset() { *this = History{}; }
    void update(const Light* targets, unsigned count, double now,
                const void* newScene, const void* newArea) {
        count=std::min(count,kCapacity);
        if (!std::isfinite(now)) { reset(); return; }
        if (scene != newScene || area != newArea || last < 0 ||
            now < last || now-last > 1.0) {
            reset(); scene=newScene; area=newArea; last=now;
        }
        // Bound pause/hitch catch-up so a resumed frame cannot pop to full gain.
        const float dt=static_cast<float>(std::max(0.0,std::min(.05,now-last)));
        last=now;
        for (auto& slot:slots) {
            if (!slot.light.key) continue;
            const Light* found=nullptr;
            for(unsigned i=0;i<count;++i) if(targets[i].key==slot.light.key){found=&targets[i];break;}
            if(found){
                slot.light=*found; // Current engine position: do not trail moving torches.
                if(slot.confirmed) slot.weight=std::min(1.f,slot.weight+dt/.5f);
                else slot.confirmed=true; // Two observations before admitting any radiance.
            } else {
                slot.weight=std::max(0.f,slot.weight-dt/.35f);
                slot.confirmed=false;
                if(slot.weight<=0) slot=Slot{};
            }
        }
        for(unsigned i=0;i<count;++i){
            if(!targets[i].key)continue;
            bool exists=false;
            for(const auto& slot:slots)if(slot.light.key==targets[i].key){exists=true;break;}
            if(exists)continue;
            // Retiring lights keep their slot until dark: never replace one at full gain.
            for(auto& slot:slots)if(!slot.light.key){slot.light=targets[i];break;}
        }
    }
};
}
