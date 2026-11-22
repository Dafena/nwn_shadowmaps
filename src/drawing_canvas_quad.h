#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace drawing_probe {
struct CanvasVertex {float x,y,u,v;};
struct CanvasRect {float x=0,y=0,w=0,h=0;bool bottomUp=false;};
// Project the actual native NUI image quad. No widget margins or DPI constants.
inline bool canvas_rect(const std::array<CanvasVertex,6>& vertices,const float* matrix,
                        const int* viewport,CanvasRect& result) {
    if(viewport[0]!=0 || viewport[1]!=0 || viewport[2]<=0 || viewport[3]<=0)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    std::array<CanvasVertex,6> projected{};
    float left=65536,top=65536,right=-65536,bottom=-65536;
    for(unsigned i=0;i<6;++i){
        const auto& p=vertices[i];
        float w=matrix[3]*p.x+matrix[7]*p.y+matrix[15];
        if(!std::isfinite(w) || w<=0)return false;
        float x=((matrix[0]*p.x+matrix[4]*p.y+matrix[12])/w+1)*viewport[2]*0.5f;
        float y=(1-(matrix[1]*p.x+matrix[5]*p.y+matrix[13])/w)*viewport[3]*0.5f;
        if(!std::isfinite(x) || !std::isfinite(y) || std::fabs(x)>65536 || std::fabs(y)>65536 ||
           !std::isfinite(p.u) || !std::isfinite(p.v))return false;
        projected[i]={x,y,p.u,p.v};left=std::min(left,x);right=std::max(right,x);
        top=std::min(top,y);bottom=std::max(bottom,y);
    }
    float w=right-left,h=bottom-top;
    if(w<1 || h<1 || w>16384 || h>16384)return false;
    const bool flip=(projected[0].y-top)/h<0.5f?projected[0].v>0.5f:projected[0].v<0.5f;
    unsigned corners=0;
    for(const auto& p:projected){
        float u=(p.x-left)/w,v=(p.y-top)/h;
        if(std::fabs(p.u-u)>0.002f || std::fabs(p.v-(flip?1-v:v))>0.002f ||
           std::min(std::fabs(u),std::fabs(1-u))>0.002f ||
           std::min(std::fabs(v),std::fabs(1-v))>0.002f)return false;
        corners|=1u<<((u>0.5f?1:0)+(v>0.5f?2:0));
    }
    if(corners!=15)return false;
    result={left,top,w,h,flip};return true;
}
}
