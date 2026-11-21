#include "shadow_fog_lamp_fade.h"
#include <cassert>
#include <cmath>
int main(){
 using namespace fog_lamp_fade;
 History h; Light a; a.key=&a;a.radius=6;a.color[0]=1;
 h.update(&a,1,0,&h,&h);assert(h.slots[0].weight==0);
 h.update(&a,1,.01,&h,&h);assert(h.slots[0].weight==0);
 h.update(&a,1,.02,&h,&h);assert(h.slots[0].weight>0 && h.slots[0].weight<.1);
 for(int i=3;i<70;++i)h.update(&a,1,i*.01,&h,&h);
 assert(h.slots[0].weight==1);
 a.pos[0]=3;h.update(&a,1,.70,&h,&h);assert(h.slots[0].light.pos[0]==3);
 h.update(nullptr,0,.71,&h,&h);assert(h.slots[0].weight<1 && h.slots[0].weight>.9);
 for(int i=72;i<120;++i)h.update(nullptr,0,i*.01,&h,&h);
 assert(!h.slots[0].light.key);
 h.update(&a,1,1.2,&h,&h);assert(h.slots[0].weight==0);
 h.update(&a,1,5,&h,&h);assert(h.slots[0].weight==0);
 int area;h.update(&a,1,5.01,&h,&area);assert(h.slots[0].weight==0);
 // Ordinary-light settings above eight must admit all their targets.
 Light many[32];for(unsigned i=0;i<32;++i){many[i]=a;many[i].key=&many[i];}
 h.reset();for(int i=0;i<70;++i)h.update(many,32,i*.01,&h,&h);
 unsigned active=0;for(const auto& s:h.slots)if(s.weight>0)++active;
 assert(active==32);
 // Lowering the limit retires excess lights smoothly.
 h.update(many,4,.70,&h,&h);
 assert(h.slots[31].weight>.9 && h.slots[31].weight<1);
 for(int i=71;i<115;++i)h.update(many,4,i*.01,&h,&h);
 active=0;for(const auto& s:h.slots)if(s.weight>0)++active;
 assert(active==4);
 // A full set must fade out before any new identities can take its slots.
 Light group[kCapacity];for(unsigned i=0;i<kCapacity;++i){group[i]=a;group[i].key=&group[i];}
 h.reset();h.update(group,kCapacity,0,&h,&h);
 for(int i=1;i<70;++i)h.update(group,kCapacity,i*.01,&h,&h);
 h.update(&a,1,.7,&h,&h);
 for(auto& s:h.slots)assert(s.light.key!=a.key && s.weight>.9);
}
