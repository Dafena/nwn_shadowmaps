#include "shadow_verified_gateway.h"
#include "subhook.h"
#include <array>
#include <cassert>
#include <cstdio>
struct Part {uint64_t renderable=0,draws=0;};
static unsigned predicates=0,partHooks=0,nextHooks=0;
extern "C" bool gateway_fixture_predicate(void* value){++predicates;return ((Part*)value)->renderable!=0;}
extern "C" void gateway_fixture_part(void*);
extern "C" const void* gateway_fixture_next(void*,int,int);
static verified_gateway::Gateway part,nextItem;
extern "C" void part_hook(void* value){++partHooks;((void(*)(void*))part.code)(value);}
extern "C" const void* next_hook(void* self,int bucket,int order){++nextHooks;return ((const void*(*)(void*,int,int))nextItem.code)(self,bucket,order);}
int main(){
 assert(verified_gateway::make_part(part,(void*)gateway_fixture_part,(void*)gateway_fixture_predicate));
 assert(verified_gateway::make_next(nextItem,(void*)gateway_fixture_next));
 unsigned char originalPart[17],originalNext[17];std::memcpy(originalPart,(void*)gateway_fixture_part,17);std::memcpy(originalNext,(void*)gateway_fixture_next,17);
 Part value;
 for(unsigned i=0;i<1000;++i){value.renderable=i%2;((void(*)(void*))part.code)(&value);}
 assert(value.draws==500 && predicates==1000);
 alignas(16) std::array<unsigned char,1024> manager{};
 struct List {void** items;int count,capacity;};
 std::array<void*,3> items{(void*)0x101,(void*)0x202,(void*)0x303};
 for(int b=0;b<2;++b)for(int o=0;o<3;++o){
  List list{items.data(),3,3};std::memcpy(manager.data()+16*(3*b+o),&list,sizeof(list));
  for(int cursor=0;cursor<=3;++cursor){
   std::memcpy(manager.data()+0x308,&cursor,4);const void* reference=gateway_fixture_next(manager.data(),b,o);int after;std::memcpy(&after,manager.data()+0x308,4);
   std::memcpy(manager.data()+0x308,&cursor,4);const void* actual=((const void*(*)(void*,int,int))nextItem.code)(manager.data(),b,o);int actualAfter;std::memcpy(&actualAfter,manager.data()+0x308,4);
   assert(reference==actual && after==actualAfter);
  }
 }
 auto ph=subhook_new((void*)gateway_fixture_part,(void*)part_hook,SUBHOOK_64BIT_OFFSET);
 auto nh=subhook_new((void*)gateway_fixture_next,(void*)next_hook,SUBHOOK_64BIT_OFFSET);
 assert(ph && nh && subhook_install(ph)==0 && subhook_install(nh)==0);
 unsigned char patchedPart[17],patchedNext[17];std::memcpy(patchedPart,(void*)gateway_fixture_part,17);std::memcpy(patchedNext,(void*)gateway_fixture_next,17);
 value.renderable=1;const auto previous=value.draws;
 for(int i=0;i<1000;++i){gateway_fixture_part(&value);int cursor=0;std::memcpy(manager.data()+0x308,&cursor,4);assert(gateway_fixture_next(manager.data(),0,0)==items.data());}
 assert(partHooks==1000 && nextHooks==1000 && value.draws==previous+1000 && predicates==2000);
 assert(!std::memcmp(patchedPart,(void*)gateway_fixture_part,17) && !std::memcmp(patchedNext,(void*)gateway_fixture_next,17));
 assert(subhook_remove(ph)==0 && subhook_remove(nh)==0);subhook_free(ph);subhook_free(nh);
 assert(!std::memcmp(originalPart,(void*)gateway_fixture_part,17) && !std::memcmp(originalNext,(void*)gateway_fixture_next,17));
 unsigned char bad[17];std::memcpy(bad,originalNext,17);bad[0]=0x90;
 verified_gateway::Gateway rejected;assert(!verified_gateway::make_next(rejected,bad) && !rejected.code);
 assert(!verified_gateway::make_part(rejected,(void*)gateway_fixture_part,(void*)gateway_fixture_next) && !rejected.code);
 std::puts("PASS: verified prefixes, original predicate/ABI, iterator cursor/end, installed-hook calls with unchanged code bytes, mismatch rejection");
}
