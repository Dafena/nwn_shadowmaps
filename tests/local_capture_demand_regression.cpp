#define main checkpoint_tag_state_main
#include "sun_receiver_tag_state_regression.cpp"
#undef main
#define REQUIRE(condition,code) do{if(!(condition)){fprintf(stderr,"capture demand failure %d: %s\n",code,#condition);return code;}}while(0)
static void seed_local_work(){
 g_localInlinePrepared=true;g_localInlineSlotCount=2;
 g_localVisibleDynamicPrepared=true;g_localVisibleDynamicCaptureActive=true;
 g_localVisibleFallbackNext=true;g_localVisibleSlotCount=2;
 g_haveLocalLightVP=true;g_localLightSlotCount=2;g_localLightVPFrame=42;
 g_localObjectMapReady=true;g_localLightLastCaptureTime=100;
 g_localLightCompletedPolicyEpoch=g_localLightCapturePolicyEpoch;
}
static bool no_local_work(){return !g_localInlinePrepared && !g_localInlineSlotCount &&
 !g_localVisibleDynamicPrepared && !g_localVisibleDynamicCaptureActive &&
 !g_localVisibleFallbackNext && !g_localVisibleSlotCount && !g_haveLocalLightVP &&
 !g_localLightSlotCount && !g_localLightVPFrame && !g_localObjectMapReady &&
 g_localLightLastCaptureTime<0 && !g_localLightCompletedPolicyEpoch;}
int main(){
 namespace d=local_capture_demand;
 g_areaScene=(void*)0x101;g_receiverEnabled=true;g_localLightCapture=true;
 g_localLightEmittersCast=true;g_localLightReceiver=false;g_localLightDumpAt=-1;g_localLightTrace=false;
 seed_local_work();REQUIRE(d::allow() && g_haveLocalLightVP,1);
 g_localJoin[0].key=(void*)0x200;g_localJoin[0].level=.8f;g_localJoinLast=100;
 d::set_enabled(true);REQUIRE(!d::allow() && no_local_work(),2);
 unsigned epoch=g_localLightCapturePolicyEpoch;REQUIRE(!d::allow() && g_localLightCapturePolicyEpoch==epoch,3);
 REQUIRE(g_localJoin[0].key==(void*)0x200 && g_localJoin[0].level==.8f && g_localJoinLast==100,4);
 // Each producer independently cancels already-prepared work before GL/replay.
 d::suspended=false;seed_local_work();capture_local_light_shadow(g_areaScene);REQUIRE(no_local_work(),5);
 d::suspended=false;seed_local_work();replay_bucket_into_local_light_layers(g_areaScene,0);REQUIRE(no_local_work(),6);
 g_localLightDumpAt=0;REQUIRE(d::allow() && no_local_work(),7);
 seed_local_work();REQUIRE(d::allow() && g_haveLocalLightVP,8);
 g_localLightDumpAt=-1;REQUIRE(!d::allow() && no_local_work(),9);
 g_localLightTrace=true;REQUIRE(d::allow(),10);g_localLightTrace=false;
 g_localLightReceiver=true;g_dynamicCastersEnabled=false;g_staticCastersEnabled=false;
 REQUIRE(d::allow(),11);seed_local_work();REQUIRE(d::allow() && g_haveLocalLightVP,12);
 g_receiverEnabled=false;REQUIRE(!d::allow() && no_local_work(),13);
 g_receiverEnabled=true;REQUIRE(d::allow() && no_local_work(),14);
 g_localLightEmittersCast=false;REQUIRE(!d::allow(),15);
 g_localLightEmittersCast=true;REQUIRE(d::allow(),16);
 g_localLightCapture=false;REQUIRE(!d::allow(),17);g_localLightCapture=true;
 g_areaScene=(void*)0x102;REQUIRE(d::allow() && no_local_work() && !g_localJoin[0].key && g_localJoinLast==0,18);
 g_localLightReceiver=false;REQUIRE(!d::allow(),19);d::set_enabled(false);
 REQUIRE(d::allow() && no_local_work(),20);seed_local_work();REQUIRE(d::allow() && g_haveLocalLightVP,21);
 puts("Local demand: baseline intact, hidden replay/staging cancelled, diagnostic consumers, fresh resume, independent casters, area reset and fallback: PASS");
}
