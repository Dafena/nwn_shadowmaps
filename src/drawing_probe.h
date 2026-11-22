#pragma once
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>

// Capability instrumentation only. Never consumes input or accepts shared-board writes.
namespace drawing_probe {
constexpr int token_number=910001;
struct Message {
    std::array<char,65> nonce{};
    char kind=0;
    unsigned sequence=0;
    float x=0,y=0,w=0,h=0;
};
inline bool parse(const char* value, Message& result) {
    if(!value || std::strlen(value)>256) return false;
    Message next; int consumed=0;
    if(std::sscanf(value,"EOMDRAW1|%64[0123456789abcdef]|%c|%u|%f|%f|%f|%f%n",
        next.nonce.data(),&next.kind,&next.sequence,&next.x,&next.y,&next.w,&next.h,&consumed)!=7)
        return false;
    if(std::strlen(next.nonce.data())!=64 || value[consumed]!='\0' ||
        !std::strchr("ODUGCB",next.kind) || next.sequence==0 || next.sequence>4096 ||
        !std::isfinite(next.x) || !std::isfinite(next.y) ||
        !std::isfinite(next.w) || !std::isfinite(next.h) ||
        std::fabs(next.x)>65536 || std::fabs(next.y)>65536 || next.w<0 || next.h<0 ||
        next.w>16384 || next.h>16384) return false;
    result=next; return true;
}
}

void nwn_drawing_probe_input(int kind,float x,float y);
bool nwn_drawing_probe_active();
void nwn_drawing_probe_frame();
void nwn_drawing_probe_install_texture_observer();
std::uintptr_t nwn_drawing_probe_context();
void nwn_drawing_probe_texture(unsigned texture,std::uintptr_t context,int orientation);
unsigned nwn_drawing_probe_texture_for_context(std::uintptr_t context);
void nwn_drawing_probe_geometry(float x,float y,float w,float h,bool bottomUp);
void nwn_drawing_probe_draw(unsigned mode,int count,unsigned type,const void* indices);
#ifdef _WIN32
void* nwn_win_drawing_token_trampoline(void* source);
#endif
