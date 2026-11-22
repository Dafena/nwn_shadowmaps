#pragma once
// Exact-prefix x86-64 gateways; no instruction decoder or engine-layout clone.
#if defined(__linux__) && defined(__x86_64__)
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
namespace verified_gateway {
struct Gateway {
    void* code=nullptr;size_t bytes=0;
    Gateway()=default;
    Gateway(const Gateway&)=delete;
    Gateway& operator=(const Gateway&)=delete;
    void reset(){if(code){munmap(code,bytes);code=nullptr;bytes=0;}}
    ~Gateway(){reset();}
    bool allocate(){const long page=sysconf(_SC_PAGESIZE);if(page<=0)return false;bytes=size_t(page);code=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(code==MAP_FAILED){code=nullptr;bytes=0;return false;}return true;}
    bool seal(){__builtin___clear_cache((char*)code,(char*)code+bytes);if(mprotect(code,bytes,PROT_READ|PROT_EXEC)!=0){reset();return false;}return true;}
};
inline void jump(unsigned char* out,const void* target){
    const unsigned char instruction[]={0xff,0x25,0,0,0,0};std::memcpy(out,instruction,6);
    const uintptr_t address=(uintptr_t)target;std::memcpy(out+6,&address,8);
}
inline bool make_next(Gateway& gateway,const void* source){
    static const unsigned char prefix[]={0x48,0x63,0xf6,0x48,0x63,0xc2,0x48,0x63,0x8f,0x08,0x03,0,0,0x48,0x8d,0x14,0x76};
    gateway.reset();if(!source || std::memcmp(source,prefix,sizeof(prefix)))return false;
    if(!gateway.allocate())return false;
    std::memcpy(gateway.code,source,sizeof(prefix));jump((unsigned char*)gateway.code+sizeof(prefix),(const unsigned char*)source+sizeof(prefix));
    return gateway.seal();
}
inline bool make_part(Gateway& gateway,const void* source,const void* expectedCallee){
    static const unsigned char prefix[]={0x55,0x48,0x89,0xe5,0x53,0x48,0x89,0xfb,0x48,0x83,0xec,0x18};
    gateway.reset();if(!source || !expectedCallee || std::memcmp(source,prefix,sizeof(prefix)))return false;
    const auto* original=(const unsigned char*)source;if(original[12]!=0xe8)return false;
    int32_t relative=0;std::memcpy(&relative,original+13,4);
    const auto callee=(const void*)((uintptr_t)(original+17)+(intptr_t)relative);
    if(callee!=expectedCallee || !gateway.allocate())return false;
    auto* out=(unsigned char*)gateway.code;std::memcpy(out,prefix,sizeof(prefix));
    // Indirect CALL, then short JMP over its pointer. Preserves registers,
    // flags, stack alignment and the original non-throwing predicate call.
    const unsigned char call[]={0xff,0x15,2,0,0,0,0xeb,8};std::memcpy(out+12,call,8);
    const uintptr_t address=(uintptr_t)callee;std::memcpy(out+20,&address,8);
    jump(out+28,original+17);return gateway.seal();
}
}
#endif
