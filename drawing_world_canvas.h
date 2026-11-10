#pragma once
#include "drawing_pad_marker.h"
#include <cstdint>
#include <vector>

namespace drawing_probe {
// Local proof on the dormitory's dedicated emission map. The portrait atlas
// becomes a 3.736 x 2.412 metre panel. This inset keeps a 4:3 pen canvas inside
// its large sketch workspace, clear of the title, notes and lower toolbar.
constexpr unsigned world_side=2048, world_x=420, world_y=640, world_w=856, world_h=992;
inline int world_marker(const unsigned char* rgba) {
    if(!rgba)return -1;
    auto row=[](const unsigned char* p,bool bottom){
        for(unsigned i=0;i<16;++i){
            for(unsigned c=0;c<3;++c)
                if(p[i*4+c]!=static_cast<unsigned char>(marker_channel(i,c,bottom)+113))return false;
            if(p[i*4+3]!=255)return false;
        }
        return true;
    };
    const auto last=rgba+(world_side-1)*world_side*4;
    if(row(rgba,false) && row(last,true))return 0;
    if(row(rgba,true) && row(last,false))return 1;
    return -1;
}
inline std::vector<unsigned char> world_composite(const std::vector<unsigned char>& base,
                                                 const std::vector<unsigned char>& ink,bool bottomUp,bool mirrorX=false) {
    if(base.size()!=world_side*world_side*4 || ink.size()!=1024*1024*4)return {};
    auto result=base;
    for(unsigned y=0;y<world_h;++y)for(unsigned x=0;x<world_w;++x){
        const auto source=((y*1024/world_h)*1024+(x*1024/world_w))*4;
        const unsigned alpha=ink[source+3];if(!alpha)continue;
        const unsigned row=bottomUp?world_side-1-(world_y+y):world_y+y;
        // The exported texture is horizontally corrected for this screen.
        // Apply the same mapping to fresh pen strokes without flipping the
        // already-corrected base artwork a second time.
        const unsigned column=mirrorX?world_side-1-(world_x+x):world_x+x;
        const auto target=(row*world_side+column)*4;
        for(unsigned c=0;c<3;++c)
            result[target+c]=static_cast<unsigned char>((ink[source+c]*alpha+base[target+c]*(255-alpha)+127)/255);
        // Preserve the authored texture's alpha, including outside the ink.
    }
    return result;
}
}
