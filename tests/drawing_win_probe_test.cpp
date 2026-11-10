#include <windows.h>
#include <cassert>
#include <cstring>
#include "../drawing_probe.h"
int main() {
    auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,128,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(code);
    unsigned char prefix[]={0x85,0xd2,0x0f,0x88,56,0,0,0,0x48,0x8b,0xc4,0x57,0x41,0x54};
    unsigned char body[]={0x41,0x5c,0x5f,0xb8,42,0,0,0,0xc3}; // pop r12; pop rdi; return 42
    unsigned char negative[]={0xb8,7,0,0,0,0xc3};
    std::memcpy(code,prefix,sizeof(prefix));std::memcpy(code+14,body,sizeof(body));
    std::memcpy(code+64,negative,sizeof(negative));
    DWORD old=0;assert(VirtualProtect(code,128,PAGE_EXECUTE_READ,&old));
    void* stub=nwn_win_drawing_token_trampoline(code);assert(stub);
    using Fn=int(*)(void*,int,void*);
    auto fn=reinterpret_cast<Fn>(stub);
    assert(fn(nullptr,1,nullptr)==42);assert(fn(nullptr,0,nullptr)==42);assert(fn(nullptr,-1,nullptr)==7);
    VirtualFree(stub,0,MEM_RELEASE);
    assert(VirtualProtect(code,128,PAGE_READWRITE,&old));code[0]=0x90;
    assert(VirtualProtect(code,128,PAGE_EXECUTE_READ,&old));
    assert(!nwn_win_drawing_token_trampoline(code));
    VirtualFree(code,0,MEM_RELEASE);
}
