#ifdef _WIN32
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "drawing_probe.h"

// Verified v89.8193.37-17 entry: test edx,edx; js rel32; mov rax,rsp;
// push rdi; push r12. Subhook cannot relocate its conditional branch when its
// allocation is more than 2 GiB from nwmain. Expand that one known branch into
// an absolute jump, preserving flags, registers and the original stack effects.
// Every other prologue fails closed. This does not change shared/Linux hooking.
void* nwn_win_drawing_token_trampoline(void* source) {
    auto p=static_cast<const unsigned char*>(source);
    MEMORY_BASIC_INFORMATION region{},branchRegion{};
    if(!VirtualQuery(p,&region,sizeof(region)) || region.State!=MEM_COMMIT ||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
       !(region.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
       reinterpret_cast<uintptr_t>(p)+14 > reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize)
        return nullptr;
    const unsigned char prefix[]={0x85,0xd2,0x0f,0x88};
    const unsigned char tail[]={0x48,0x8b,0xc4,0x57,0x41,0x54};
    if(std::memcmp(p,prefix,4) || std::memcmp(p+8,tail,6))return nullptr;
    int32_t relative=0;std::memcpy(&relative,p+4,4);
    auto target=reinterpret_cast<const void*>(reinterpret_cast<intptr_t>(p)+8+relative);
    if(!VirtualQuery(target,&branchRegion,sizeof(branchRegion)) ||
       branchRegion.AllocationBase!=region.AllocationBase || branchRegion.State!=MEM_COMMIT ||
       (branchRegion.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
       !(branchRegion.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
        return nullptr;
    auto stub=static_cast<unsigned char*>(VirtualAlloc(nullptr,38,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!stub)return nullptr;
    auto jump=[](unsigned char* at,const void* destination) {
        const unsigned char instruction[]={0xff,0x25,0,0,0,0};
        std::memcpy(at,instruction,6);std::memcpy(at+6,&destination,8);
    };
    stub[0]=0x85;stub[1]=0xd2;stub[2]=0x79;stub[3]=14; // jns skips the absolute negative branch
    jump(stub+4,target);
    std::memcpy(stub+18,tail,6);
    jump(stub+24,p+14);
    DWORD old=0;
    if(!VirtualProtect(stub,38,PAGE_EXECUTE_READ,&old)) {VirtualFree(stub,0,MEM_RELEASE);return nullptr;}
    FlushInstructionCache(GetCurrentProcess(),stub,38);
    return stub;
}
#endif
