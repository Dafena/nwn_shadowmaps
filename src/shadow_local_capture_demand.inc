// Experimental Linux-only demand gating; normal builds retain prior behavior.
namespace local_capture_demand {
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
static bool enabled=false,suspended=false;
static void* sceneOwner=nullptr;
static void invalidate(bool areaChanged){
    g_localInlinePrepared=false;g_localInlineSlotCount=0;g_localInlinePolicyEpoch=0;
    g_localVisibleDynamicPrepared=false;g_localVisibleDynamicCaptureActive=false;
    g_localVisibleFallbackNext=false;g_localVisibleSlotCount=0;
    g_localVisiblePreparedFrame=0;g_localVisibleCompletedFrame=0;g_localVisiblePolicyEpoch=0;
    g_localObjectMapReady=false;g_localObjectCaptureLayer=-1;
    g_haveLocalLightVP=false;g_localLightSlotCount=0;g_localLightVPFrame=0;
    g_localLightLastCaptureTime=-1.0;g_localLightCompletedPolicyEpoch=0;
    std::fill_n(g_localInlineLayerFrame,kLocalSlotsMax,0u);
    std::fill_n(g_localObjectLayerFrame,kLocalSlotsMax,0u);
    std::fill_n(g_localVisibleDynamicDraws,kLocalLightFaces,0u);
    ++g_localLightCapturePolicyEpoch;
    if(areaChanged){
        for(auto& join:g_localJoin)join=LocalJoinFade{};
        g_localJoinLast=0;g_localLightRadiusStable=0;g_localLightRadiusOwner=nullptr;
    }
    // In the same area keep fade history/time; normal authority reconciles it
    // on resumption. No previous depth generation is published as fresh.
}
static void set_enabled(bool value){
    if(enabled==value)return;
    if(enabled || value)invalidate(false);
    enabled=value;suspended=false;sceneOwner=g_areaScene;
}
static bool allow(){
    if(!enabled)return true;
    if(sceneOwner!=g_areaScene){invalidate(true);sceneOwner=g_areaScene;suspended=false;}
    const bool consumer=g_localLightReceiver || g_localLightDumpAt>=0 || g_localLightTrace;
    const bool wanted=g_receiverEnabled && g_localLightCapture &&
                      g_localLightEmittersCast && consumer;
    if(!wanted){if(!suspended)invalidate(false);suspended=true;return false;}
    if(suspended){invalidate(false);suspended=false;}
    return true;
}
#else
static inline bool allow(){return true;}
#endif
}
