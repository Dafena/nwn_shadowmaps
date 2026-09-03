// ===========================================================================
//  Injector-owned weather-effects runtime -- Linux reference implementation
//
//  This module owns clear/rain/snow discovery, conservative receiver
//  classification, and stock-material weather branches. Effects are injected
//  into shaders already used by NWN and remain inert unless the draw funnel
//  marks the current draw as eligible static area scenery.
//
//  Windows remains a no-op until its separately requested port. Keep this
//  interface platform-neutral so that port only supplies bindings proven to
//  differ from the accepted Linux behaviour.
// ===========================================================================

#ifndef _WIN32

namespace weather_runtime {

static constexpr int kWeatherClear = 0;
static constexpr int kWeatherRain  = 1;
static constexpr int kWeatherSnow  = 2;
static constexpr unsigned kProgramCapacity = 256;
static constexpr int kTrackedActorCapacity = 16;
static constexpr int kSnowTracksPerActor = 64;
static constexpr int kRetiringSnowTrackCapacity = 128;
static constexpr int kRetiringPlayerTrackCapacity = 64;
static constexpr int kSnowTrackCapacity =
    kTrackedActorCapacity * kSnowTracksPerActor;
static constexpr int kActorSampleCapacity = kTrackedActorCapacity;
static constexpr int kContactCandidateCapacity = 384;
static constexpr int kSnowLightCapacity = kMaxLampLights;
static constexpr int kSnowDeformationSize = 512;
static constexpr int kSnowDeformationUnit = 25;
static constexpr int kSnowLightDataUnit = 26;
static constexpr int kPrecipitationOcclusionUnit = 27;
// NWN reserves these units for the same framebuffer snapshots consumed by
// fswater.shd. They are engine-owned; rain only samples them and never changes
// their bindings.
static constexpr int kFramebufferColorUnit = 11;
static constexpr int kFramebufferDepthUnit = 12;
static constexpr int kWaterNoiseUnit = 15;
static constexpr int kWeatherCaptureColorUnit = 23;
static constexpr int kWeatherCaptureDepthUnit = 24;

struct ProgramState {
    GLuint program = 0;
    GLint skinmesh = -1;
    GLint areaFlags = -1;
    GLint waterTime = -1;
    GLint waterWind = -1;
    GLint waterNoise = -1;
    GLint rainAmount = -1;
    GLint snowAmount = -1;
    GLint weatherEligible = -1;
    GLint weatherTime = -1;
    GLint weatherFbColor = -1;
    GLint weatherFbDepth = -1;
    GLint weatherReflectionReady = -1;
    GLint weatherFbView = -1;
    GLint weatherFbProj = -1;
    GLint weatherFbProjInv = -1;
    GLint weatherNoiseTex = -1;
    GLint weatherNoiseReady = -1;
    GLint snowDeformationTex = -1;
    GLint snowDeformationTransform = -1;
    GLint snowAreaLight = -1;
    GLint snowLightFalloff = -1;
    GLint snowLightData = -1;
    GLint snowLightCount = -1;
    GLint precipitationOcclusionTex = -1;
    GLint precipitationOcclusionVP = -1;
    GLint precipitationOcclusionReady = -1;
    GLint skinmeshValue = 0;
    bool skinmeshKnown = false;
};

struct SnowTrack {
    float startX = 0.0f;
    float startY = 0.0f;
    float startZ = 0.0f;
    float endX = 0.0f;
    float endY = 0.0f;
    float endZ = 0.0f;
    float radius = 0.0f;
    float depth = 0.0f;
    double born = 0.0;
    double retireStarted = 0.0;
};

struct ActorSample {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float stampX = 0.0f;
    float stampY = 0.0f;
    float stampZ = 0.0f;
    double lastSeen = 0.0;
    unsigned nextTrack = 0;
    bool valid = false;
};

struct ContactCandidate {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct RuntimeState {
    ProgramState programs[kProgramCapacity] = {};
    unsigned programCount = 0;
    unsigned frameSerial = 0;
    unsigned sceneDraws = 0;
    unsigned eligibleDraws = 0;
    unsigned skinnedDraws = 0;
    unsigned waterDraws = 0;
    unsigned nonSceneDraws = 0;
    unsigned unclassifiedDraws = 0;
    unsigned actorPositions = 0;
    unsigned snowTrackStamps = 0;
    int weatherType = kWeatherClear;
    float weatherDensity = 0.0f;
    float rainAmount = 0.0f;
    float snowAmount = 0.0f;
    void* weatherArea = nullptr;
    bool weatherObserved = false;
    bool raining = false;
    bool snowing = false;
    bool areaFlagsKnown = false;
    bool areaInterior = false;
    int areaFlags = 0;
    bool reflectionReady = false;
    bool noiseReady = false;
    unsigned reflectionProbeFrame = ~0u;
    bool framebufferCameraValid = false;
    unsigned framebufferCameraFrame = 0;
    void* framebufferCameraScene = nullptr;
    float framebufferView[16] = {};
    float framebufferProjection[16] = {};
    float framebufferProjectionInverse[16] = {};
    GLuint framebufferColorTexture = 0;
    GLuint framebufferDepthTexture = 0;
    SnowTrack snowTracks[kTrackedActorCapacity][kSnowTracksPerActor] = {};
    SnowTrack retiringSnowTracks[kRetiringSnowTrackCapacity] = {};
    SnowTrack retiringPlayerTracks[kRetiringPlayerTrackCapacity] = {};
    ActorSample actorSamples[kActorSampleCapacity] = {};
    ContactCandidate contactCandidates[kContactCandidateCapacity] = {};
    unsigned contactCandidateCount = 0;
    unsigned nextRetiringSnowTrack = 0;
    unsigned nextRetiringPlayerTrack = 0;
    GLuint snowDeformationTexture = 0;
    GLuint snowLightDataTexture = 0;
    unsigned snowLightDataFrame = ~0u;
    int snowLightDataCount = 0;
    std::vector<float> snowLightDataPixels;
    GLuint precipitationOcclusionTexture = 0;
    GLuint precipitationOcclusionFbo = 0;
    int precipitationOcclusionSize = 0;
    float precipitationOcclusionExtent = 0.0f;
    bool precipitationOcclusionUsable = false;
    bool precipitationOcclusionValid = false;
    bool precipitationOcclusionDirty = true;
    unsigned precipitationOcclusionRefreshSerial = 0;
    float precipitationOcclusionVP[16] = {};
    float precipitationOcclusionCenter[3] = {};
    void* precipitationOcclusionScene = nullptr;
    const void* precipitationOcclusionArea = nullptr;
    float snowDeformationOriginX = 0.0f;
    float snowDeformationOriginY = 0.0f;
    float snowDeformationInvSpan = 0.0f;
    bool snowDeformationReady = false;
    bool snowDeformationDirty = true;
    double nextSnowDeformationUpdate = 0.0;
    // R stores deformation depth and G stores the contacted world height.
    // Height makes a trail belong to its floor instead of every roof/bridge
    // sharing the same XY coordinates.
    std::vector<float> snowDeformationPixels;
    double lastTick = 0.0;
    double nextStatus = 0.0;
};

static RuntimeState g;

using Uniform1i_t = void (*)(GLint, GLint);
static Uniform1i_t g_realUniform1i = nullptr;
static subhook_t g_weatherHook = nullptr;
static eng::AreaSetWeather_t g_weatherTrampoline = nullptr;

static bool area_draw_active() {
    const bool traceArea = g_traceEnabled && g_traceFrameActive &&
                           g_traceCurrentScene &&
                           g_traceCurrentScene == g_areaScene;
    const bool legacyArea = g_inSceneRender && g_renderingScene &&
                            g_renderingScene == g_areaScene;
    return traceArea || legacyArea;
}

static ProgramState* program_state(GLuint program) {
    if (!program || !gl::GetUniformLocation) return nullptr;
    for (unsigned i = 0; i < g.programCount; ++i)
        if (g.programs[i].program == program) return &g.programs[i];
    if (g.programCount >= kProgramCapacity) return nullptr;

    ProgramState& state = g.programs[g.programCount++];
    state.program = program;
    state.skinmesh = gl::GetUniformLocation(program, "skinmesh");
    state.areaFlags = gl::GetUniformLocation(program, "areaFlags");
    // This combination is specific to NWN's stock water path. Water gets its
    // own ripple branch and must not be treated as an ordinary puddle receiver.
    state.waterTime = gl::GetUniformLocation(program, "worldtimerTimeOfDay");
    state.waterWind = gl::GetUniformLocation(program, "areaGlobalWind");
    state.waterNoise = gl::GetUniformLocation(program, "texUnitNoise");
    state.rainAmount = gl::GetUniformLocation(program, "nwnWeatherRainAmount");
    state.snowAmount = gl::GetUniformLocation(program, "nwnWeatherSnowAmount");
    state.weatherEligible = gl::GetUniformLocation(program, "nwnWeatherEligible");
    state.weatherTime = gl::GetUniformLocation(program, "nwnWeatherTime");
    state.weatherFbColor = gl::GetUniformLocation(program, "nwnWeatherFbColor");
    state.weatherFbDepth = gl::GetUniformLocation(program, "nwnWeatherFbDepth");
    state.weatherReflectionReady =
        gl::GetUniformLocation(program, "nwnWeatherReflectionReady");
    state.weatherFbView = gl::GetUniformLocation(program, "nwnWeatherFbView");
    state.weatherFbProj = gl::GetUniformLocation(program, "nwnWeatherFbProj");
    state.weatherFbProjInv =
        gl::GetUniformLocation(program, "nwnWeatherFbProjInv");
    state.weatherNoiseTex = gl::GetUniformLocation(program, "nwnWeatherNoiseTex");
    state.weatherNoiseReady = gl::GetUniformLocation(program, "nwnWeatherNoiseReady");
    state.snowDeformationTex =
        gl::GetUniformLocation(program, "nwnWeatherSnowDeformationTex");
    state.snowDeformationTransform =
        gl::GetUniformLocation(program, "nwnWeatherSnowDeformationTransform");
    state.snowAreaLight = gl::GetUniformLocation(program, "nwnWeatherSnowAreaLight");
    state.snowLightFalloff =
        gl::GetUniformLocation(program, "nwnWeatherSnowLightFalloff");
    state.snowLightData = gl::GetUniformLocation(program, "nwnWeatherSnowLightData");
    state.snowLightCount = gl::GetUniformLocation(program, "nwnWeatherSnowLightCount");
    state.precipitationOcclusionTex =
        gl::GetUniformLocation(program, "nwnWeatherPrecipitationOcclusionTex");
    state.precipitationOcclusionVP =
        gl::GetUniformLocation(program, "nwnWeatherPrecipitationOcclusionVP");
    state.precipitationOcclusionReady =
        gl::GetUniformLocation(program, "nwnWeatherPrecipitationOcclusionReady");
    return &state;
}

static void probe_framebuffer_reflection_inputs() {
    if (g.reflectionProbeFrame == g.frameSerial || !gl::ActiveTexture ||
        !gl::GetIntegerv) return;
    g.reflectionProbeFrame = g.frameSerial;
    GLint oldActive = GL_TEXTURE0, noise = 0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    gl::ActiveTexture(GL_TEXTURE0 + kWaterNoiseUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &noise);
    gl::ActiveTexture((GLenum)oldActive);
    const bool ready = g.framebufferCameraValid &&
                       g.framebufferColorTexture != 0 &&
                       g.framebufferDepthTexture != 0;
    const bool noiseReady = noise != 0;
    static bool reported = false;
    static bool lastReady = false;
    static bool lastNoiseReady = false;
    if (!reported || ready != lastReady || noiseReady != lastNoiseReady) {
        reported = true;
        lastReady = ready;
        lastNoiseReady = noiseReady;
        fprintf(stderr,
                "[weather][linux] water inputs reflection=%s noise=%s "
                "color=%d depth=%d noise=%d\n",
                ready ? "ready" : "unavailable",
                noiseReady ? "ready" : "unavailable",
                (int)g.framebufferColorTexture,
                (int)g.framebufferDepthTexture, noise);
    }
    g.reflectionReady = ready;
    g.noiseReady = noiseReady;
}

static void clear_snow_tracks() {
    for (auto& actorTracks : g.snowTracks)
        for (SnowTrack& track : actorTracks) track = SnowTrack{};
    for (ActorSample& actor : g.actorSamples) actor = ActorSample{};
    for (SnowTrack& track : g.retiringSnowTracks) track = SnowTrack{};
    for (SnowTrack& track : g.retiringPlayerTracks) track = SnowTrack{};
    g.nextRetiringSnowTrack = 0;
    g.nextRetiringPlayerTrack = 0;
    g.snowDeformationReady = false;
    g.snowDeformationDirty = true;
    g.nextSnowDeformationUpdate = 0.0;
}

static void observe_area_flags(int flags) {
    const bool interior = (flags & 0x0001) != 0; // NWAREA_FLAG_INTERIOR
    const bool changed = !g.areaFlagsKnown || flags != g.areaFlags;
    g.areaFlagsKnown = true;
    g.areaFlags = flags;
    if (interior && (!g.areaInterior || g.rainAmount > 0.0f ||
                     g.snowAmount > 0.0f)) {
        fprintf(stderr,
                "[weather][linux] interior area flags=0x%x; clearing rain %.3f "
                "snow %.3f immediately\n",
                flags, g.rainAmount, g.snowAmount);
        g.rainAmount = 0.0f;
        g.snowAmount = 0.0f;
        clear_snow_tracks();
    }
    if (interior) {
        g.precipitationOcclusionValid = false;
        g.precipitationOcclusionDirty = true;
    }
    g.areaInterior = interior;
    // SetWeather can arrive before or after the first material upload during
    // an area load. Re-derive the targets whenever the authoritative area bit
    // changes so either ordering produces the same result.
    g.raining = !interior && g.weatherType == kWeatherRain &&
                g.weatherDensity > 0.001f;
    g.snowing = !interior && g.weatherType == kWeatherSnow &&
                g.weatherDensity > 0.001f;
    if (changed)
        fprintf(stderr, "[weather][linux] area flags=0x%x interior=%d\n",
                flags, interior ? 1 : 0);
}

static void retire_snow_track(const SnowTrack& source, double now,
                              bool playerPriority) {
    if (!(source.radius > 0.0f) || source.born <= 0.0) return;
    SnowTrack& retired = playerPriority
        ? g.retiringPlayerTracks[
            g.nextRetiringPlayerTrack % kRetiringPlayerTrackCapacity]
        : g.retiringSnowTracks[
            g.nextRetiringSnowTrack % kRetiringSnowTrackCapacity];
    retired = source;
    retired.retireStarted = now;
    if (playerPriority)
        g.nextRetiringPlayerTrack =
            (g.nextRetiringPlayerTrack + 1u) % kRetiringPlayerTrackCapacity;
    else
        g.nextRetiringSnowTrack =
            (g.nextRetiringSnowTrack + 1u) % kRetiringSnowTrackCapacity;
    g.snowDeformationDirty = true;
}

static void retire_actor_tracks(int actorIndex, double now) {
    if (actorIndex < 0 || actorIndex >= kTrackedActorCapacity) return;
    for (SnowTrack& track : g.snowTracks[actorIndex]) {
        retire_snow_track(track, now, actorIndex == 0);
        track = SnowTrack{};
    }
}

static void add_snow_track(int actorIndex, float startX, float startY,
                           float startZ, float endX, float endY, float endZ,
                           float radius, float depth, double now) {
    if (actorIndex < 0 || actorIndex >= kTrackedActorCapacity) return;
    ActorSample& actor = g.actorSamples[actorIndex];
    SnowTrack& track = g.snowTracks[actorIndex]
        [actor.nextTrack % kSnowTracksPerActor];
    retire_snow_track(track, now, actorIndex == 0);
    track.startX = startX;
    track.startY = startY;
    track.startZ = startZ;
    track.endX = endX;
    track.endY = endY;
    track.endZ = endZ;
    track.radius = radius;
    track.depth = depth;
    track.born = now;
    track.retireStarted = 0.0;
    actor.nextTrack = (actor.nextTrack + 1u) % kSnowTracksPerActor;
    g.snowDeformationDirty = true;
    ++g.snowTrackStamps;
}

static void observe_ground_contact(float x, float y, float z,
                                   int forcedSlot = -1) {
    if (g.snowAmount <= 0.015f || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(z)) return;
    ++g.actorPositions;
    const double now = now_seconds();
    int best = forcedSlot;
    float bestD2 = 2.25f;
    int oldest = 1;
    if (forcedSlot < 0) {
        for (int i = 1; i < kActorSampleCapacity; ++i) {
            ActorSample& actor = g.actorSamples[i];
            if (!actor.valid) { oldest = i; continue; }
            if (!g.actorSamples[oldest].valid ||
                actor.lastSeen < g.actorSamples[oldest].lastSeen) oldest = i;
            if (now - actor.lastSeen > 1.0) continue;
            const float dx = x - actor.x;
            const float dy = y - actor.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = i; }
        }
    }
    if (best < 0) {
        best = oldest;
        retire_actor_tracks(best, now);
        ActorSample& actor = g.actorSamples[best];
        actor = ActorSample{};
        actor.x = actor.stampX = x;
        actor.y = actor.stampY = y;
        actor.z = actor.stampZ = z;
        actor.lastSeen = now;
        actor.valid = true;
        return;
    }

    if (!g.actorSamples[best].valid) {
        ActorSample& actor = g.actorSamples[best];
        actor = ActorSample{};
        actor.x = actor.stampX = x;
        actor.y = actor.stampY = y;
        actor.z = actor.stampZ = z;
        actor.lastSeen = now;
        actor.valid = true;
        return;
    }

    ActorSample& actor = g.actorSamples[best];
    actor.x = x;
    actor.y = y;
    actor.z = z;
    actor.lastSeen = now;
    const float moveX = x - actor.stampX;
    const float moveY = y - actor.stampY;
    const float move = std::sqrt(moveX * moveX + moveY * moveY);
    if (move < 0.24f) return;
    // Do not draw a trail across teleports, area placement or a temporarily
    // mismatched contact cluster. Ordinary locomotion becomes a continuous
    // rounded segment rather than a row of circular projected stamps.
    if (move <= 1.50f)
        add_snow_track(best, actor.stampX, actor.stampY, actor.stampZ,
                       x, y, z, 0.16f, 0.92f, now);
    actor.stampX = x;
    actor.stampY = y;
    actor.stampZ = z;
}

// Bucket 2 submits a creature as several independently transformed meshes.
// Their origins include heads, hands, weapons and accessories, so stamping at
// draw time projects the whole body onto the floor. Collect the origins for a
// complete frame, cluster them at creature scale, and reduce each cluster to
// one locomotion sample derived only from its lowest transforms.
static void collect_contact_candidate(float x, float y, float z) {
    if (g.snowAmount <= 0.015f || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(z)) return;
    ++g.actorPositions;
    for (unsigned i = 0; i < g.contactCandidateCount; ++i) {
        const ContactCandidate& c = g.contactCandidates[i];
        const float dx = x - c.x;
        const float dy = y - c.y;
        const float dz = z - c.z;
        if (dx * dx + dy * dy + dz * dz < 0.0004f) return;
    }
    if (g.contactCandidateCount >= kContactCandidateCapacity) return;
    ContactCandidate& c = g.contactCandidates[g.contactCandidateCount++];
    c.x = x;
    c.y = y;
    c.z = z;
}

static void resolve_ground_contacts() {
    bool assigned[kContactCandidateCapacity] = {};
    ContactCandidate resolved[kContactCandidateCapacity] = {};
    unsigned resolvedCount = 0;
    for (unsigned seed = 0; seed < g.contactCandidateCount; ++seed) {
        if (assigned[seed]) continue;

        const ContactCandidate& origin = g.contactCandidates[seed];
        float minZ = origin.z;
        unsigned members[kContactCandidateCapacity] = {};
        unsigned memberCount = 0;
        // A generous creature-sized XY cluster keeps articulated nodes
        // together. Nearby creatures may share a sample for a frame, which is
        // preferable to inventing impacts from every body part.
        for (unsigned i = seed; i < g.contactCandidateCount; ++i) {
            if (assigned[i]) continue;
            const ContactCandidate& c = g.contactCandidates[i];
            const float dx = c.x - origin.x;
            const float dy = c.y - origin.y;
            if (dx * dx + dy * dy > 1.96f) continue;
            assigned[i] = true;
            members[memberCount++] = i;
            minZ = std::min(minZ, c.z);
        }

        float contactX = 0.0f;
        float contactY = 0.0f;
        unsigned contactCount = 0;
        for (unsigned m = 0; m < memberCount; ++m) {
            const ContactCandidate& c = g.contactCandidates[members[m]];
            if (c.z > minZ + 0.18f) continue;
            contactX += c.x;
            contactY += c.y;
            ++contactCount;
        }
        if (contactCount == 0) continue;
        if (resolvedCount < kContactCandidateCapacity) {
            ContactCandidate& contact = resolved[resolvedCount++];
            contact.x = contactX / (float)contactCount;
            contact.y = contactY / (float)contactCount;
            contact.z = minZ;
        }
    }

    if (resolvedCount == 0) return;
    const double now = now_seconds();
    int playerContact = -1;
    float bestScore = 1.0e20f;

    // Once acquired, preserve the controlled-character slot by continuity.
    // This is more stable than reselecting from screen position whenever an
    // NPC walks through the camera target.
    const ActorSample& player = g.actorSamples[0];
    if (player.valid && now - player.lastSeen <= 1.0) {
        for (unsigned i = 0; i < resolvedCount; ++i) {
            const float dx = resolved[i].x - player.x;
            const float dy = resolved[i].y - player.y;
            const float score = dx * dx + dy * dy;
            if (score < bestScore) { bestScore = score; playerContact = (int)i; }
        }
        if (bestScore > 4.0f) playerContact = -1;
    }

    // The renderer does not expose NWN object IDs here. For initial/reacquire,
    // select the feet nearest the camera's ground look target. NWN's orbit
    // camera aims at the controlled creature, making this a substantially
    // stronger discriminator than draw order or nearest-to-eye distance.
    if (playerContact < 0 && g_shadowFrameContext.valid) {
        const float* view = g_shadowFrameContext.view;
        const float eyeX = g_shadowFrameContext.viewInverse[12];
        const float eyeY = g_shadowFrameContext.viewInverse[13];
        const float eyeZ = g_shadowFrameContext.viewInverse[14];
        const float forwardX = -view[2];
        const float forwardY = -view[6];
        const float forwardZ = -view[10];
        bestScore = 1.0e20f;
        for (unsigned i = 0; i < resolvedCount; ++i) {
            float targetX = eyeX;
            float targetY = eyeY;
            if (std::fabs(forwardZ) > 0.05f) {
                const float along = (resolved[i].z - eyeZ) / forwardZ;
                targetX += forwardX * std::max(0.0f, along);
                targetY += forwardY * std::max(0.0f, along);
            }
            const float dx = resolved[i].x - targetX;
            const float dy = resolved[i].y - targetY;
            const float score = dx * dx + dy * dy;
            if (score < bestScore) { bestScore = score; playerContact = (int)i; }
        }
    }
    if (playerContact < 0) playerContact = 0;

    observe_ground_contact(resolved[playerContact].x,
                           resolved[playerContact].y,
                           resolved[playerContact].z, 0);
    for (unsigned i = 0; i < resolvedCount; ++i) {
        if ((int)i == playerContact) continue;
        observe_ground_contact(resolved[i].x, resolved[i].y, resolved[i].z);
    }
}

static bool ensure_snow_deformation_texture() {
    if (g.snowDeformationTexture) return true;
    if (!gl::GenTextures || !gl::BindTexture || !gl::TexImage2D ||
        !gl::TexParameteri || !gl::ActiveTexture || !gl::GetIntegerv)
        return false;
    GLint maxUnits = 0;
    gl::GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxUnits);
    if (maxUnits <= kSnowDeformationUnit) {
        fprintf(stderr,
                "[weather][linux] snow deformation disabled: texture unit %d "
                "requires at least %d units (driver reports %d)\n",
                kSnowDeformationUnit, kSnowDeformationUnit + 1, maxUnits);
        return false;
    }
    g.snowDeformationPixels.assign(
        kSnowDeformationSize * kSnowDeformationSize * 4, 0.0f);
    GLint oldActive = GL_TEXTURE0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    gl::ActiveTexture(GL_TEXTURE0 + kSnowDeformationUnit);
    gl::GenTextures(1, &g.snowDeformationTexture);
    gl::BindTexture(GL_TEXTURE_2D, g.snowDeformationTexture);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kSnowDeformationSize,
                   kSnowDeformationSize, 0, GL_RGBA, GL_FLOAT,
                   g.snowDeformationPixels.data());
    gl::ActiveTexture((GLenum)oldActive);
    fprintf(stderr,
            "[weather][linux] snow deformation texture ready %dx%d unit=%d\n",
            kSnowDeformationSize, kSnowDeformationSize, kSnowDeformationUnit);
    return g.snowDeformationTexture != 0;
}

static bool ensure_snow_light_data_texture() {
    if (g.snowLightDataTexture) return true;
    if (!gl::GenTextures || !gl::BindTexture || !gl::TexImage2D ||
        !gl::TexParameteri || !gl::ActiveTexture || !gl::GetIntegerv)
        return false;
    GLint maxUnits = 0;
    gl::GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxUnits);
    if (maxUnits <= kSnowLightDataUnit) {
        fprintf(stderr,
                "[weather][linux] full snow lighting disabled: texture unit %d "
                "requires at least %d units (driver reports %d)\n",
                kSnowLightDataUnit, kSnowLightDataUnit + 1, maxUnits);
        return false;
    }
    g.snowLightDataPixels.assign(kSnowLightCapacity * 2 * 4, 0.0f);
    GLint oldActive = GL_TEXTURE0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    gl::ActiveTexture(GL_TEXTURE0 + kSnowLightDataUnit);
    gl::GenTextures(1, &g.snowLightDataTexture);
    gl::BindTexture(GL_TEXTURE_2D, g.snowLightDataTexture);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kSnowLightCapacity * 2, 1,
                   0, GL_RGBA, GL_FLOAT, g.snowLightDataPixels.data());
    gl::ActiveTexture((GLenum)oldActive);
    fprintf(stderr,
            "[weather][linux] snow light-data texture ready for %d lights on unit=%d\n",
            kSnowLightCapacity, kSnowLightDataUnit);
    return g.snowLightDataTexture != 0;
}

static void update_snow_light_data_texture() {
    if (g.snowLightDataFrame == g.frameSerial) return;
    g.snowLightDataFrame = g.frameSerial;
    g.snowLightDataCount = 0;
    if (!ensure_snow_light_data_texture() || !gl::TexSubImage2D) return;

    std::fill(g.snowLightDataPixels.begin(), g.snowLightDataPixels.end(), 0.0f);
    const unsigned count = std::min<unsigned>(g_lampLightCount,
                                               kSnowLightCapacity);
    for (unsigned i = 0; i < count; ++i) {
        const LampEntry& lamp = g_lampLights[i];
        float* pos = &g.snowLightDataPixels[(i * 2) * 4];
        float* color = &g.snowLightDataPixels[(i * 2 + 1) * 4];
        pos[0] = lamp.pos[0]; pos[1] = lamp.pos[1]; pos[2] = lamp.pos[2];
        pos[3] = lamp.radius * lamp.radius;
        color[0] = lamp.color[0]; color[1] = lamp.color[1];
        color[2] = lamp.color[2]; color[3] = 1.0f;
    }
    GLint oldActive = GL_TEXTURE0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    gl::ActiveTexture(GL_TEXTURE0 + kSnowLightDataUnit);
    gl::BindTexture(GL_TEXTURE_2D, g.snowLightDataTexture);
    gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSnowLightCapacity * 2, 1,
                      GL_RGBA, GL_FLOAT, g.snowLightDataPixels.data());
    gl::ActiveTexture((GLenum)oldActive);
    g.snowLightDataCount = (int)count;
}

static bool ensure_precipitation_occlusion_target() {
    const int wantedSize = g_staticWorldSize;
    const float wantedExtent = g_staticWorldExtent;
    if (g.precipitationOcclusionFbo &&
        g.precipitationOcclusionSize == wantedSize) {
        if (g.precipitationOcclusionExtent != wantedExtent) {
            g.precipitationOcclusionExtent = wantedExtent;
            g.precipitationOcclusionValid = false;
            g.precipitationOcclusionDirty = true;
        }
        return g.precipitationOcclusionUsable;
    }
    if (!gl::GenTextures || !gl::BindTexture || !gl::TexImage2D ||
        !gl::TexParameteri || !gl::GenFramebuffers || !gl::BindFramebuffer ||
        !gl::FramebufferTexture2D || !gl::CheckFramebufferStatus ||
        !gl::ActiveTexture || !gl::GetIntegerv)
        return false;
    GLint maxUnits = 0;
    gl::GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxUnits);
    if (maxUnits <= kPrecipitationOcclusionUnit) {
        fprintf(stderr,
                "[weather][linux] precipitation occlusion disabled: texture unit %d "
                "requires at least %d units (driver reports %d)\n",
                kPrecipitationOcclusionUnit,
                kPrecipitationOcclusionUnit + 1, maxUnits);
        return false;
    }

    if (g.precipitationOcclusionFbo && gl::DeleteFramebuffers)
        gl::DeleteFramebuffers(1, &g.precipitationOcclusionFbo);
    if (g.precipitationOcclusionTexture && gl::DeleteTextures)
        gl::DeleteTextures(1, &g.precipitationOcclusionTexture);
    g.precipitationOcclusionFbo = 0;
    g.precipitationOcclusionTexture = 0;
    g.precipitationOcclusionUsable = false;
    g.precipitationOcclusionValid = false;

    GLint oldActive = GL_TEXTURE0, oldTexture = 0, oldFbo = 0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    gl::ActiveTexture(GL_TEXTURE0 + kPrecipitationOcclusionUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    while (gl::GetError() != GL_NO_ERROR) {}
    gl::GenTextures(1, &g.precipitationOcclusionTexture);
    gl::BindTexture(GL_TEXTURE_2D, g.precipitationOcclusionTexture);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                   wantedSize, wantedSize,
                   0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                      GL_COMPARE_REF_TO_TEXTURE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
    const GLenum textureError = gl::GetError();

    gl::GenFramebuffers(1, &g.precipitationOcclusionFbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, g.precipitationOcclusionFbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             GL_TEXTURE_2D,
                             g.precipitationOcclusionTexture, 0);
    gl::DrawBuffer(GL_NONE);
    gl::ReadBuffer(GL_NONE);
    const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
    g.precipitationOcclusionUsable =
        status == GL_FRAMEBUFFER_COMPLETE &&
        textureError == GL_NO_ERROR && gl::GetError() == GL_NO_ERROR;
    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)oldTexture);
    gl::ActiveTexture((GLenum)oldActive);
    g.precipitationOcclusionSize = wantedSize;
    g.precipitationOcclusionExtent = wantedExtent;
    g.precipitationOcclusionDirty = true;
    fprintf(stderr,
            "[weather][linux] precipitation occlusion target %dx%d extent=%.0f "
            "status=%s usable=%s\n",
            wantedSize, wantedSize, wantedExtent, fbo_status_str(status),
            g.precipitationOcclusionUsable ? "yes" : "no");
    return g.precipitationOcclusionUsable;
}

static bool precipitation_occlusion_needs_refresh() {
    if (!g.precipitationOcclusionValid || g.precipitationOcclusionDirty)
        return true;
    // SetWeather's CNWCArea pointer is the stable area identity.  Do not key
    // this cache on g_areaToggleObject: the shadowing hooks temporarily expose
    // other CAurObjects while a carried torch is being selected.  Treating
    // those transient pointers as area changes made an 8K top-down capture run
    // repeatedly while holding a torch, visibly disturbing the light passes.
    const void* area = g.weatherArea;
    if (g.precipitationOcclusionScene != g_shadowFrameContext.scene ||
        g.precipitationOcclusionArea != area)
        return true;
    const float dx = g_shadowFrameContext.eye.x -
                     g.precipitationOcclusionCenter[0];
    const float dy = g_shadowFrameContext.eye.y -
                     g.precipitationOcclusionCenter[1];
    const float limit = g.precipitationOcclusionExtent * 0.60f;
    return dx * dx + dy * dy > limit * limit;
}

// Called next to NWN's normal bucket render. The first stale frame requests
// the complete static BSP; the following frame captures it once from directly
// overhead. Buckets 0 and 1 accumulate into the same target: opaque static
// geometry plus static alpha/card meshes, while creatures remain excluded.
static void capture_precipitation_occlusion_bucket(void* scene, int bucket) {
    if ((bucket != 0 && bucket != 1) || scene != g_areaScene || g.areaInterior ||
        !g_shadowFrameContext.valid)
        return;
    const bool continuingCapture =
        bucket == 1 &&
        g.precipitationOcclusionRefreshSerial == g_shadowFrameContext.serial &&
        !g.precipitationOcclusionValid;
    if (bucket == 0 && !precipitation_occlusion_needs_refresh()) return;
    if (bucket == 1 && !continuingCapture) return;
    if (!ensure_precipitation_occlusion_target()) return;
    if (!g_fullBspEnabled || !g_casterFullBspNativeSubmit) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr,
                    "[weather][linux] precipitation occlusion unavailable: "
                    "complete static BSP submission is disabled\n");
        }
        return;
    }
    if (!g_fullBspSubmittedFrame) {
        g_fullBspWanted = true;
        return;
    }
    if (g.precipitationOcclusionRefreshSerial == g_shadowFrameContext.serial &&
        !continuingCapture)
        return;

    float* view = mtx_entry(MTX_VIEW);
    float* projection = mtx_entry(MTX_PROJ);
    if (!view || !projection) return;

    const float extent = g.precipitationOcclusionExtent;
    const int size = g.precipitationOcclusionSize;
    const float texel = (2.0f * extent) / (float)size;
    if (bucket == 0) {
        g.precipitationOcclusionCenter[0] =
            std::round(g_shadowFrameContext.eye.x / texel) * texel;
        g.precipitationOcclusionCenter[1] =
            std::round(g_shadowFrameContext.eye.y / texel) * texel;
        g.precipitationOcclusionCenter[2] = g_shadowFrameContext.eye.z;
    }
    const float direction[3] = {0.0f, 0.0f, -1.0f};
    Vec3f topEye{g.precipitationOcclusionCenter[0],
                 g.precipitationOcclusionCenter[1],
                 g.precipitationOcclusionCenter[2] + 512.0f, 1.0f};
    float topView[16], topProjection[16];
    build_lookat(topEye, direction, topView);
    build_ortho(extent, 1.0f, 1024.0f, topProjection);
    mat4_mul(topProjection, topView, g.precipitationOcclusionVP);

    float savedView[16], savedProjection[16];
    std::memcpy(savedView, view, sizeof(savedView));
    std::memcpy(savedProjection, projection, sizeof(savedProjection));
    const int oldMode = eng::CurMtxIdx ? *eng::CurMtxIdx : 0;
    std::memcpy(view, topView, sizeof(topView));
    std::memcpy(projection, topProjection, sizeof(topProjection));
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
    if (eng::FlagDirty) eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_PROJ);
    if (eng::FlagDirty) eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);

    SavedState saved;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &saved.fbo);
    gl::GetIntegerv(GL_VIEWPORT, saved.vp);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &saved.depthMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, saved.colorMask);
    saved.scissor = gl::IsEnabled(GL_SCISSOR_TEST);
    saved.depthTest = gl::IsEnabled(GL_DEPTH_TEST);
    if (gl::PolygonOffset && gl::GetFloatv) {
        saved.polyOffset = gl::IsEnabled(GL_POLYGON_OFFSET_FILL);
        gl::GetFloatv(GL_POLYGON_OFFSET_FACTOR, &saved.polyFactor);
        gl::GetFloatv(GL_POLYGON_OFFSET_UNITS, &saved.polyUnits);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, g.precipitationOcclusionFbo);
    gl::Viewport(0, 0, size, size);
    if (saved.scissor) gl::Disable(GL_SCISSOR_TEST);
    if (!saved.depthMask) gl::DepthMask(GL_TRUE);
    if (!saved.depthTest) gl::Enable(GL_DEPTH_TEST);
    gl::ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    // Bucket 0 begins the refresh; bucket 1 keeps its depth and adds static
    // alpha/card geometry. The stock fragment shader remains active during
    // replay, so alpha-discard holes stay holes instead of solid quads.
    if (bucket == 0) {
        gl::ClearDepth(1.0);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
    }

    const bool oldReplay = g_cascadeReplayActive;
    const char* oldFaultLabel = g_faultLabel;
    g_cascadeReplayActive = true;
    g_faultLabel = "WEATHER OCCLUSION";
    const bool rendered = guarded_render_bucket(scene, bucket);
    g_faultLabel = oldFaultLabel;
    g_cascadeReplayActive = oldReplay;

    restore_state(saved);
    std::memcpy(view, savedView, sizeof(savedView));
    std::memcpy(projection, savedProjection, sizeof(savedProjection));
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
    if (eng::FlagDirty) eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_PROJ);
    if (eng::FlagDirty) eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);

    if (!rendered) return;
    if (bucket == 0) {
        // Do not expose the half-built map. Bucket 1 follows later in this same
        // scene frame and completes the static blocker set.
        g.precipitationOcclusionScene = scene;
        g.precipitationOcclusionArea = g.weatherArea;
        g.precipitationOcclusionRefreshSerial = g_shadowFrameContext.serial;
        g.precipitationOcclusionValid = false;
        g.precipitationOcclusionDirty = true;
        return;
    }
    g.precipitationOcclusionScene = scene;
    g.precipitationOcclusionArea = g.weatherArea;
    g.precipitationOcclusionRefreshSerial = g_shadowFrameContext.serial;
    g.precipitationOcclusionValid = true;
    g.precipitationOcclusionDirty = false;
    g_fullBspWanted = false;
    fprintf(stderr,
            "[weather][linux] precipitation occlusion captured from static "
            "buckets 0+1 at centre=(%.1f,%.1f) area=%p\n",
            g.precipitationOcclusionCenter[0],
            g.precipitationOcclusionCenter[1],
            g.precipitationOcclusionArea);
}

static float smoothstep_cpu(float edge0, float edge1, float x) {
    const float t = std::max(0.0f, std::min(1.0f,
                              (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

static void update_snow_deformation_texture(double now) {
    if (now < g.nextSnowDeformationUpdate && !g.snowDeformationDirty) return;
    g.nextSnowDeformationUpdate = now + 0.10;
    g.snowDeformationDirty = false;
    if (!ensure_snow_deformation_texture() || !gl::TexSubImage2D) return;

    float minX = 1.0e20f, minY = 1.0e20f;
    float maxX = -1.0e20f, maxY = -1.0e20f;
    bool any = false;
    auto track_strength = [now](const SnowTrack& track) {
        if (!(track.radius > 0.0f) || track.born <= 0.0) return 0.0f;
        const float age = (float)std::max(0.0, now - track.born);
        float strength = std::max(0.0f, 1.0f - age / 55.0f);
        if (track.retireStarted > 0.0) {
            const float retiringAge =
                (float)std::max(0.0, now - track.retireStarted);
            strength *= std::max(0.0f, 1.0f - retiringAge / 2.0f);
        }
        return strength;
    };
    auto include_bounds = [&](const SnowTrack& track) {
        if (track_strength(track) <= 0.001f) return;
        minX = std::min(minX, std::min(track.startX, track.endX) - track.radius);
        minY = std::min(minY, std::min(track.startY, track.endY) - track.radius);
        maxX = std::max(maxX, std::max(track.startX, track.endX) + track.radius);
        maxY = std::max(maxY, std::max(track.startY, track.endY) + track.radius);
        any = true;
    };
    for (const auto& actorTracks : g.snowTracks) {
        for (const SnowTrack& track : actorTracks) include_bounds(track);
    }
    for (const SnowTrack& track : g.retiringSnowTracks) include_bounds(track);
    for (const SnowTrack& track : g.retiringPlayerTracks) include_bounds(track);
    if (!any || g.snowAmount <= 0.001f) {
        g.snowDeformationReady = false;
        return;
    }

    const float centerX = (minX + maxX) * 0.5f;
    const float centerY = (minY + maxY) * 0.5f;
    const float span = std::max(16.0f, std::max(maxX - minX, maxY - minY) + 1.0f);
    g.snowDeformationOriginX = centerX - span * 0.5f;
    g.snowDeformationOriginY = centerY - span * 0.5f;
    g.snowDeformationInvSpan = 1.0f / span;
    std::fill(g.snowDeformationPixels.begin(),
              g.snowDeformationPixels.end(), 0.0f);

    const float pixelsPerWorld = (float)kSnowDeformationSize / span;
    auto raster_track = [&](const SnowTrack& track) {
            const float strength = track_strength(track);
            if (strength <= 0.001f) return;
            const float dx = track.endX - track.startX;
            const float dy = track.endY - track.startY;
            const float len2 = std::max(0.0001f, dx * dx + dy * dy);
            const int px0 = std::max(0, (int)std::floor(
                (std::min(track.startX, track.endX) - track.radius -
                 g.snowDeformationOriginX) * pixelsPerWorld));
            const int py0 = std::max(0, (int)std::floor(
                (std::min(track.startY, track.endY) - track.radius -
                 g.snowDeformationOriginY) * pixelsPerWorld));
            const int px1 = std::min(kSnowDeformationSize - 1, (int)std::ceil(
                (std::max(track.startX, track.endX) + track.radius -
                 g.snowDeformationOriginX) * pixelsPerWorld));
            const int py1 = std::min(kSnowDeformationSize - 1, (int)std::ceil(
                (std::max(track.startY, track.endY) + track.radius -
                 g.snowDeformationOriginY) * pixelsPerWorld));
            for (int py = py0; py <= py1; ++py) {
                const float wy = g.snowDeformationOriginY +
                    ((float)py + 0.5f) / pixelsPerWorld;
                for (int px = px0; px <= px1; ++px) {
                    const float wx = g.snowDeformationOriginX +
                        ((float)px + 0.5f) / pixelsPerWorld;
                    const float along = std::max(0.0f, std::min(1.0f,
                        ((wx - track.startX) * dx +
                         (wy - track.startY) * dy) / len2));
                    const float sx = track.startX + dx * along;
                    const float sy = track.startY + dy * along;
                    const float sz = track.startZ +
                                     (track.endZ - track.startZ) * along;
                    const float nx = wx - sx, ny = wy - sy;
                    const float d = std::sqrt(nx * nx + ny * ny) / track.radius;
                    const float bowl = 1.0f - smoothstep_cpu(0.12f, 1.0f, d);
                    const float value = std::min(
                        1.0f, bowl * track.depth * strength);
                    float* pixel = &g.snowDeformationPixels[
                        (py * kSnowDeformationSize + px) * 4];
                    if (value > pixel[0]) {
                        pixel[0] = value;
                        pixel[1] = sz;
                    }
                }
            }
    };
    for (const auto& actorTracks : g.snowTracks) {
        for (const SnowTrack& track : actorTracks) raster_track(track);
    }
    for (const SnowTrack& track : g.retiringSnowTracks) raster_track(track);
    for (const SnowTrack& track : g.retiringPlayerTracks) raster_track(track);

    GLint oldActive = GL_TEXTURE0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    gl::ActiveTexture(GL_TEXTURE0 + kSnowDeformationUnit);
    gl::BindTexture(GL_TEXTURE_2D, g.snowDeformationTexture);
    gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSnowDeformationSize,
                      kSnowDeformationSize, GL_RGBA, GL_FLOAT,
                      g.snowDeformationPixels.data());
    gl::ActiveTexture((GLenum)oldActive);
    g.snowDeformationReady = true;
}

static void push_draw_state(ProgramState* state, float receiverClass) {
    if (!state || state->weatherEligible < 0 || !gl::Uniform1f) return;
    // The current engine program is already bound at the draw funnel. Always
    // upload eligibility, including zero, because NWN reuses a material
    // program across static meshes, creatures, water and auxiliary passes.
    // 0 = excluded, 1 = opaque static, 2 = static alpha/card. The shader uses
    // class 2 for wet noise but explicitly forbids standing puddles there.
    gl::Uniform1f(state->weatherEligible, receiverClass);
    if (state->rainAmount >= 0)
        gl::Uniform1f(state->rainAmount, g.rainAmount);
    if (state->snowAmount >= 0)
        gl::Uniform1f(state->snowAmount, g.snowAmount);
    if (state->weatherTime >= 0) {
        const float t = (float)std::fmod(now_seconds(), 4096.0);
        gl::Uniform1f(state->weatherTime, t);
    }
    if (receiverClass > 0.5f) probe_framebuffer_reflection_inputs();
    if (state->weatherReflectionReady >= 0)
        gl::Uniform1f(state->weatherReflectionReady,
                      receiverClass > 0.5f && g.reflectionReady &&
                      g.framebufferCameraValid &&
                      g.framebufferCameraScene == g_areaScene ? 1.0f : 0.0f);
    if (receiverClass > 0.5f && g.framebufferCameraValid &&
        g.framebufferCameraScene == g_areaScene && gl::UniformMatrix4fv) {
        if (state->weatherFbView >= 0)
            gl::UniformMatrix4fv(state->weatherFbView, 1, GL_FALSE,
                                 g.framebufferView);
        if (state->weatherFbProj >= 0)
            gl::UniformMatrix4fv(state->weatherFbProj, 1, GL_FALSE,
                                 g.framebufferProjection);
        if (state->weatherFbProjInv >= 0)
            gl::UniformMatrix4fv(state->weatherFbProjInv, 1, GL_FALSE,
                                 g.framebufferProjectionInverse);
    }
    if (state->weatherNoiseReady >= 0)
        gl::Uniform1f(state->weatherNoiseReady,
                      receiverClass > 0.5f && g.noiseReady ? 1.0f : 0.0f);
    if (gl::Uniform1i) {
        if (state->weatherFbColor >= 0)
            gl::Uniform1i(state->weatherFbColor, kWeatherCaptureColorUnit);
        if (state->weatherFbDepth >= 0)
            gl::Uniform1i(state->weatherFbDepth, kWeatherCaptureDepthUnit);
        if (state->weatherNoiseTex >= 0)
            gl::Uniform1i(state->weatherNoiseTex, kWaterNoiseUnit);
        if (state->snowDeformationTex >= 0)
            gl::Uniform1i(state->snowDeformationTex, kSnowDeformationUnit);
        if (state->snowLightData >= 0)
            gl::Uniform1i(state->snowLightData, kSnowLightDataUnit);
        if (state->precipitationOcclusionTex >= 0)
            gl::Uniform1i(state->precipitationOcclusionTex,
                          kPrecipitationOcclusionUnit);
    }
    if (state->snowDeformationTransform >= 0 && gl::Uniform4f) {
        gl::Uniform4f(state->snowDeformationTransform,
                      g.snowDeformationOriginX, g.snowDeformationOriginY,
                      g.snowDeformationInvSpan,
                      receiverClass > 0.5f && g.snowDeformationReady ? 1.0f : 0.0f);
    }
    if (state->precipitationOcclusionReady >= 0)
        gl::Uniform1f(state->precipitationOcclusionReady,
                      receiverClass > 0.5f &&
                      g.precipitationOcclusionValid ? 1.0f : 0.0f);
    if (state->precipitationOcclusionVP >= 0 && gl::UniformMatrix4fv)
        gl::UniformMatrix4fv(state->precipitationOcclusionVP, 1, GL_FALSE,
                             g.precipitationOcclusionVP);
    if (gl::Uniform4f && receiverClass > 0.5f) {
        // Snow is a new neutral material. Light it from NWN's decoded area and
        // point-light data, never from FragmentColor: that value contains the
        // covered tile albedo and made lamp pools reveal cobblestone detail.
        float ar = 0.34f, ag = 0.36f, ab = 0.42f;
        if (eng::SunAmbient) {
            ar = eng::SunAmbient[0]; ag = eng::SunAmbient[1]; ab = eng::SunAmbient[2];
        }
        if (eng::SunDiffuse) {
            ar += eng::SunDiffuse[0] * 0.45f;
            ag += eng::SunDiffuse[1] * 0.45f;
            ab += eng::SunDiffuse[2] * 0.45f;
        }
        if (state->snowAreaLight >= 0)
            gl::Uniform4f(state->snowAreaLight, ar, ag, ab, 1.0f);
        if (state->snowLightFalloff >= 0 && gl::Uniform2f)
            gl::Uniform2f(state->snowLightFalloff,
                          g_lightMaxIntensityInv, g_lightFalloffFactor);

        update_snow_light_data_texture();
        if (state->snowLightCount >= 0 && gl::Uniform1i)
            gl::Uniform1i(state->snowLightCount, g.snowLightDataCount);
    }
    if (receiverClass > 0.5f && g.snowDeformationTexture &&
        gl::ActiveTexture && gl::BindTexture && gl::GetIntegerv) {
        GLint oldActive = GL_TEXTURE0;
        gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
        gl::ActiveTexture(GL_TEXTURE0 + kSnowDeformationUnit);
        gl::BindTexture(GL_TEXTURE_2D, g.snowDeformationTexture);
        gl::ActiveTexture((GLenum)oldActive);
    }
    if (receiverClass > 0.5f && g.framebufferColorTexture &&
        g.framebufferDepthTexture && gl::ActiveTexture && gl::BindTexture &&
        gl::GetIntegerv) {
        GLint oldActive = GL_TEXTURE0;
        gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
        gl::ActiveTexture(GL_TEXTURE0 + kWeatherCaptureColorUnit);
        gl::BindTexture(GL_TEXTURE_2D, g.framebufferColorTexture);
        gl::ActiveTexture(GL_TEXTURE0 + kWeatherCaptureDepthUnit);
        gl::BindTexture(GL_TEXTURE_2D, g.framebufferDepthTexture);
        static bool logged = false;
        if (!logged && g.rainAmount > 0.01f) {
            GLint colorBound = 0, depthBound = 0;
            gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &depthBound);
            gl::ActiveTexture(GL_TEXTURE0 + kWeatherCaptureColorUnit);
            gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &colorBound);
            fprintf(stderr,
                    "[weather][linux] puddle optics upload frame=%u capture=%u "
                    "age=%u readyLoc=%d viewLoc=%d projLoc=%d invLoc=%d "
                    "color=%d depth=%d\n",
                    g.frameSerial, g.framebufferCameraFrame,
                    g.frameSerial - g.framebufferCameraFrame,
                    state->weatherReflectionReady, state->weatherFbView,
                    state->weatherFbProj, state->weatherFbProjInv,
                    colorBound, depthBound);
            logged = true;
        }
        gl::ActiveTexture((GLenum)oldActive);
    }
    if (receiverClass > 0.5f && g.snowLightDataTexture &&
        gl::ActiveTexture && gl::BindTexture && gl::GetIntegerv) {
        GLint oldActive = GL_TEXTURE0;
        gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
        gl::ActiveTexture(GL_TEXTURE0 + kSnowLightDataUnit);
        gl::BindTexture(GL_TEXTURE_2D, g.snowLightDataTexture);
        gl::ActiveTexture((GLenum)oldActive);
    }
    if (receiverClass > 0.5f && g.precipitationOcclusionTexture &&
        gl::ActiveTexture && gl::BindTexture && gl::GetIntegerv) {
        GLint oldActive = GL_TEXTURE0;
        gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
        gl::ActiveTexture(GL_TEXTURE0 + kPrecipitationOcclusionUnit);
        gl::BindTexture(GL_TEXTURE_2D, g.precipitationOcclusionTexture);
        gl::ActiveTexture((GLenum)oldActive);
    }
}

static void observe_engine_weather(void* area, int weather, float densityPercent) {
    if (!area || weather < kWeatherClear || weather > kWeatherSnow ||
        !std::isfinite(densityPercent)) return;

    // Wetness belongs to a CNWCArea instance. An area transition or save load
    // must not inherit the previous instance's drying tail; only a weather
    // change within the same area is allowed to fade naturally.
    const bool areaChanged = g.weatherArea && g.weatherArea != area;
    if (areaChanged) {
        fprintf(stderr,
                "[weather][linux] area changed %p -> %p; resetting rain %.3f snow %.3f\n",
                g.weatherArea, area, g.rainAmount, g.snowAmount);
        g.weatherObserved = false;
        g.weatherType = kWeatherClear;
        g.weatherDensity = 0.0f;
        g.raining = false;
        g.snowing = false;
        g.areaFlagsKnown = false;
        g.areaInterior = false;
        g.areaFlags = 0;
        g.precipitationOcclusionValid = false;
        g.precipitationOcclusionDirty = true;
        g.precipitationOcclusionScene = nullptr;
        g.precipitationOcclusionArea = nullptr;
        g.rainAmount = 0.0f;
        g.snowAmount = 0.0f;
        clear_snow_tracks();
        g.framebufferCameraValid = false;
        g.framebufferCameraScene = nullptr;
        g.lastTick = now_seconds();
        g.nextStatus = 0.0;
    }
    g.weatherArea = area;

    const float density = std::max(0.0f, std::min(1.0f, densityPercent * 0.01f));
    const bool first = !g.weatherObserved;
    const bool changed = first || weather != g.weatherType ||
                         std::fabs(density - g.weatherDensity) > 0.01f;
    g.weatherObserved = true;
    g.weatherType = weather;
    g.weatherDensity = density;
    g.raining = !g.areaInterior && weather == kWeatherRain &&
                g.weatherDensity > 0.001f;
    g.snowing = !g.areaInterior && weather == kWeatherSnow &&
                g.weatherDensity > 0.001f;
    // Precipitation is mutually exclusive, but accumulated surface state is
    // not instantaneous. The inactive reservoir decays while the active one
    // grows, giving rain<->snow the same natural transition as clear weather.
    if (changed) {
        const char* name = weather == kWeatherRain ? "rain" :
                           weather == kWeatherSnow ? "snow" : "clear";
        fprintf(stderr,
                "[weather][linux] engine weather=%s type=%d density=%.3f area=%p\n",
                name, weather, g.weatherDensity, area);
    }
}

static void note_program(GLuint program) {
    if (!area_draw_active()) return;
    (void)program_state(program);
}

static void observe_uniform1i(GLint location, GLint value) {
    if (!area_draw_active() || !g_curProgram) return;
    ProgramState* state = program_state(g_curProgram);
    if (!state) return;
    if (state->areaFlags >= 0 && location == state->areaFlags)
        observe_area_flags(value);
    if (state->skinmesh >= 0 && location == state->skinmesh) {
        state->skinmeshValue = value;
        state->skinmeshKnown = true;
    }
}

static void observe_draw(GLuint program, GLsizei count) {
    if (!area_draw_active() || !program || count <= 0 || g_inOurPass ||
        g_cascadeReplayActive || g_localLightPassActive ||
        g_overlayPassActive || nwn_core::g_ownedPass)
        return;

    ProgramState* state = program_state(program);
    if (!state) return;
    ++g.sceneDraws;

    if (state->skinmesh >= 0 && !state->skinmeshKnown) {
        push_draw_state(state, false);
        ++g.unclassifiedDraws;
        return;
    }
    if (state->skinmesh >= 0 && state->skinmeshValue != 0) {
        push_draw_state(state, false);
        ++g.skinnedDraws;
        return;
    }

    const bool water = state->waterTime >= 0 && state->waterWind >= 0 &&
                       state->waterNoise >= 0;
    if (water) {
        push_draw_state(state, false);
        ++g.waterDraws;
        return;
    }

    // Bucket 0 is validated static opaque geometry. Bucket 1 is validated
    // static alpha/card geometry and receives wet noise without puddles.
    // Buckets 2/3 are dynamic body/hair geometry and remain excluded, keeping
    // players out independently of a particular model or material variant.
    const int bucket = nwn_core::g_currentBucket;
    if (bucket != 0 && bucket != 1) {
        push_draw_state(state, false);
        if (bucket >= 0 && bucket < kBucketCount) ++g.nonSceneDraws;
        else                                     ++g.unclassifiedDraws;
        return;
    }
    if (state->weatherEligible < 0) {
        ++g.unclassifiedDraws;
        return;
    }
    push_draw_state(state, bucket == 1 ? 2.0f : 1.0f);
    ++g.eligibleDraws;
}

static void scene_begin(void* scene) {
    if (!scene || scene != g_areaScene) return;
    if (!g.areaInterior) ensure_precipitation_occlusion_target();
    g.frameSerial = g_shadowFrameContext.serial;
    g.sceneDraws = g.eligibleDraws = g.skinnedDraws = 0;
    g.waterDraws = g.nonSceneDraws = g.unclassifiedDraws = 0;
    g.actorPositions = 0;
    g.contactCandidateCount = 0;
}

static void scene_end(void* scene) {
    if (!scene || scene != g_areaScene) return;
    resolve_ground_contacts();
    const double now = now_seconds();
    if (g.lastTick <= 0.0) g.lastTick = now;
    const float dt = (float)std::max(0.0, std::min(0.25, now - g.lastTick));
    g.lastTick = now;

    // CNWCArea::SetWeather publishes persistent area state, not a frame pulse.
    // Keep it until the engine sends the next clear/rain/snow decision.
    const float rainTarget = g.raining ? g.weatherDensity : 0.0f;
    const float rainRate = rainTarget > g.rainAmount ? 0.55f : 0.12f;
    const float rainStep = rainRate * dt;
    if (g.rainAmount < rainTarget)
        g.rainAmount = std::min(rainTarget, g.rainAmount + rainStep);
    else
        g.rainAmount = std::max(rainTarget, g.rainAmount - rainStep);

    const float snowTarget = g.snowing ? g.weatherDensity : 0.0f;
    // Match rain's clear/switch-away fade: both full-strength surface layers
    // now disappear in roughly 8.3 seconds. Snow accumulation remains slower.
    const float snowRate = snowTarget > g.snowAmount ? 0.20f : 0.12f;
    const float snowStep = snowRate * dt;
    if (g.snowAmount < snowTarget)
        g.snowAmount = std::min(snowTarget, g.snowAmount + snowStep);
    else
        g.snowAmount = std::max(snowTarget, g.snowAmount - snowStep);

    update_snow_deformation_texture(now);

    if (g.weatherObserved && now >= g.nextStatus) {
        g.nextStatus = now + 30.0;
        fprintf(stderr,
                "[weather][linux] frame=%u mode=%d rain=%.3f snow=%.3f draws=%u "
                "eligible=%u skinned-excluded=%u non-scene=%u water=%u "
                "unclassified=%u actor-pos=%u track-stamps=%u\n",
                g.frameSerial, g.weatherType, g.rainAmount, g.snowAmount,
                g.sceneDraws,
                g.eligibleDraws, g.skinnedDraws, g.nonSceneDraws, g.waterDraws,
                g.unclassifiedDraws, g.actorPositions, g.snowTrackStamps);
    }
}

static void publish_framebuffer_capture(void* scene, GLuint colorTexture,
                                        GLuint depthTexture) {
    const ShadowFrameContext& ctx = g_shadowFrameContext;
    if (!scene || scene != g_areaScene || !ctx.valid || ctx.scene != scene ||
        !colorTexture || !depthTexture) return;
    float projectionInverse[16];
    if (!mat4_inverse(ctx.projection, projectionInverse)) return;
    std::memcpy(g.framebufferView, ctx.view, sizeof(ctx.view));
    std::memcpy(g.framebufferProjection, ctx.projection,
                sizeof(ctx.projection));
    std::memcpy(g.framebufferProjectionInverse, projectionInverse,
                sizeof(projectionInverse));
    g.framebufferColorTexture = colorTexture;
    g.framebufferDepthTexture = depthTexture;
    g.framebufferCameraFrame = ctx.serial;
    g.framebufferCameraScene = ctx.scene;
    g.framebufferCameraValid = true;
    static bool logged = false;
    if (!logged) {
        logged = true;
        fprintf(stderr,
                "[weather][linux] framebuffer capture published frame=%u "
                "color=%u depth=%u\n",
                ctx.serial, colorTexture, depthTexture);
    }
}

static bool patch_fragment_shader(std::string& source) {
    // Stock world materials share this final output shape. Requiring all
    // inputs makes post-processing, UI, particles and unusual custom shaders
    // fail closed instead of receiving a guessed transform.
    if (source.find("#define SHADER_TYPE 2") == std::string::npos ||
        source.find("vec3 vFragmentNormal;") == std::string::npos ||
        source.find("varying highp vec3 vPosView;") == std::string::npos ||
        source.find("uniform highp mat4 m_view_inv;") == std::string::npos ||
        source.find("uniform int areaFlags;") == std::string::npos)
        return false;

    const size_t mainPos = source.rfind("void main");
    if (mainPos == std::string::npos) return false;
    const size_t outputPos = source.find("gl_FragColor = FragmentColor;", mainPos);
    if (outputPos == std::string::npos) return false;

    static const char* kDeclarations =
        "uniform float nwnWeatherRainAmount;\n"
        "uniform float nwnWeatherSnowAmount;\n"
        "uniform float nwnWeatherEligible;\n"
        "uniform float nwnWeatherTime;\n"
        "uniform sampler2D nwnWeatherFbColor;\n"
        "uniform sampler2D nwnWeatherFbDepth;\n"
        "uniform float nwnWeatherReflectionReady;\n"
        "uniform mat4 nwnWeatherFbView;\n"
        "uniform mat4 nwnWeatherFbProj;\n"
        "uniform mat4 nwnWeatherFbProjInv;\n"
        "uniform sampler2D nwnWeatherNoiseTex;\n"
        "uniform float nwnWeatherNoiseReady;\n"
        "uniform sampler2D nwnWeatherSnowDeformationTex;\n"
        "uniform vec4 nwnWeatherSnowDeformationTransform;\n"
        "uniform vec4 nwnWeatherSnowAreaLight;\n"
        "uniform vec2 nwnWeatherSnowLightFalloff;\n"
        "uniform sampler2D nwnWeatherSnowLightData;\n"
        "uniform int nwnWeatherSnowLightCount;\n"
        "uniform sampler2DShadow nwnWeatherPrecipitationOcclusionTex;\n"
        "uniform mat4 nwnWeatherPrecipitationOcclusionVP;\n"
        "uniform float nwnWeatherPrecipitationOcclusionReady;\n"
        "float nwnRainHash(vec2 p) {\n"
        "    p = fract(p * vec2(123.34, 345.45));\n"
        "    p += dot(p, p + 34.345);\n"
        "    return fract(p.x * p.y);\n"
        "}\n"
        "float nwnRainNoise(vec2 p) {\n"
        "    vec2 i = floor(p);\n"
        "    vec2 f = fract(p);\n"
        "    f = f * f * (3.0 - 2.0 * f);\n"
        "    return mix(mix(nwnRainHash(i), nwnRainHash(i + vec2(1.0, 0.0)), f.x),\n"
        "               mix(nwnRainHash(i + vec2(0.0, 1.0)),\n"
        "                   nwnRainHash(i + vec2(1.0, 1.0)), f.x), f.y);\n"
        "}\n"
        "float nwnWeatherPrecipitationExposure(vec3 worldPos) {\n"
        "    if (nwnWeatherPrecipitationOcclusionReady < 0.5) return 1.0;\n"
        "    vec4 projected = nwnWeatherPrecipitationOcclusionVP\n"
        "                   * vec4(worldPos, 1.0);\n"
        "    if (projected.w <= 0.0) return 1.0;\n"
        "    vec3 samplePos = projected.xyz / projected.w * 0.5 + 0.5;\n"
        "    if (samplePos.x <= 0.0 || samplePos.x >= 1.0 ||\n"
        "        samplePos.y <= 0.0 || samplePos.y >= 1.0 ||\n"
        "        samplePos.z <= 0.0 || samplePos.z >= 1.0) return 1.0;\n"
        // A rotated, world-anchored disk avoids the discrete coverage stripes
        // produced by a fixed 3x3 kernel. Rotation changes only the sampling
        // pattern, never the captured roof silhouette or comparison position.
        "    vec2 spreadX = (nwnWeatherPrecipitationOcclusionVP\n"
        "                    * vec4(0.65, 0.0, 0.0, 0.0)).xy\n"
        "                   * (0.5 / projected.w);\n"
        "    vec2 spreadY = (nwnWeatherPrecipitationOcclusionVP\n"
        "                    * vec4(0.0, 0.65, 0.0, 0.0)).xy\n"
        "                   * (0.5 / projected.w);\n"
        // The bias is about 0.7 world units over the 1024-unit vertical range:
        // enough to keep the captured top surface exposed without allowing a
        // character-height roof separation to leak through.
        "    float compareZ = samplePos.z - 0.0007;\n"
        "    float angle = nwnRainHash(floor(worldPos.xy * 32.0)) * 6.2831853;\n"
        "    float ca = cos(angle), sa = sin(angle);\n"
        "    mat2 diskRotation = mat2(ca, sa, -sa, ca);\n"
        "    vec2 disk;\n"
        "    float exposed = 0.0;\n"
        "    disk = diskRotation * vec2( 0.1768,  0.0000);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2(-0.2258,  0.2068);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2( 0.0346, -0.3943);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2( 0.2846,  0.3712);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2(-0.5223, -0.0924);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2( 0.4945, -0.3146);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2(-0.1658,  0.6166);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2(-0.3150, -0.6068);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2( 0.6849,  0.2501);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2(-0.7121,  0.2940);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2( 0.3430, -0.7280);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2( 0.2530,  0.7980);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2(-0.7640, -0.4430);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2( 0.8970, -0.1970);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2(-0.5420,  0.8180);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    disk = diskRotation * vec2(-0.1110, -0.9780);\n"
        "    exposed += texture(nwnWeatherPrecipitationOcclusionTex, vec3(samplePos.xy + spreadX*disk.x + spreadY*disk.y, compareZ));\n"
        "    exposed *= 0.0625;\n"
        // Fine world-space dither hides the remaining 1/16 quantisation. It is
        // anchored to geometry, so camera motion cannot make it crawl.
        "    exposed += (nwnRainHash(floor(worldPos.xy * 48.0) + 73.1) - 0.5)\n"
        "             * 0.055;\n"
        "    return smoothstep(0.02, 0.98, exposed);\n"
        "}\n"
        "float nwnRainPuddle(vec2 worldXY, float wetness) {\n"
        // Several compact octaves avoid the original tile-sized connected
        // regions. Low values represent local basins; the threshold grows
        // slowly with wetness so islands expand without joining half the area.
        "    float broad = nwnRainNoise(worldXY * 0.62);\n"
        "    broad = 0.58 * broad + 0.42 * nwnRainNoise(worldXY * 1.31 + 19.7);\n"
        "    float edge = nwnRainNoise(worldXY * 3.45 - 7.4);\n"
        "    float field = broad + (edge - 0.5) * 0.13;\n"
        "    float threshold = mix(0.16, 0.33, wetness);\n"
        "    return 1.0 - smoothstep(threshold - 0.04, threshold + 0.04, field);\n"
        "}\n"
        "vec3 nwnRainViewPosition(vec2 uv) {\n"
        "    float d = texture2D(nwnWeatherFbDepth, uv).x;\n"
        "    vec4 p = nwnWeatherFbProjInv\n"
        "        * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
        "    return p.xyz / max(0.00001, p.w);\n"
        "}\n"
        "float nwnRainDepth(vec2 uv) {\n"
        "    return -nwnRainViewPosition(uv).z;\n"
        "}\n"
        "vec3 nwnRainDepthNormal(vec2 uv) {\n"
        "    vec3 p = nwnRainViewPosition(uv);\n"
        "    vec3 px = nwnRainViewPosition(uv + vec2(pixelParams.x, 0.0));\n"
        "    vec3 py = nwnRainViewPosition(uv + vec2(0.0, pixelParams.y));\n"
        "    return normalize(cross(px - p, py - p));\n"
        "}\n"
        // This is the stock water shader's perspective-correct depth walk,
        // namespaced for the injector. Its relative depth error is returned in
        // w and is used exactly as a confidence term instead of accepting a
        // stretched nearest sample.
        "vec4 nwnRainTraceReflection(vec3 origin, vec3 ray) {\n"
        "    const int iterations = 10;\n"
        "    float maxDistance = clamp(abs(farClip + origin.z), 1.0, 30.0);\n"
        "    float angleFactor = mix(1.0, abs(ray.z), 0.9);\n"
        "    maxDistance *= angleFactor;\n"
        "    ray /= max(0.001, angleFactor);\n"
        "    float stepMax = maxDistance / float(iterations);\n"
        "    float minRelativeError = 4096.0;\n"
        "    float hitDepth = maxDistance + origin.z;\n"
        "    vec2 hitUv = vec2(-1.0);\n"
        "    vec3 rayEndProj = mat3(nwnWeatherFbProj)\n"
        "                    * (origin + maxDistance * ray);\n"
        "    vec2 rayEndUv = 0.5 * (rayEndProj.xy / rayEndProj.z) + 0.5;\n"
        "    vec3 originProj = mat3(nwnWeatherFbProj) * origin;\n"
        "    vec2 originUv = 0.5 * (originProj.xy / originProj.z) + 0.5;\n"
        "    float denomInv = 1.0 / (1.0 / (origin.z + ray.z * maxDistance)\n"
        "                              - 1.0 / origin.z);\n"
        "    float interpOffset = (1.0 / origin.z) * denomInv;\n"
        "    float depthDelta = 0.035;\n"
        "    float travel = 0.0;\n"
        "    bool crossed = false;\n"
        "    for (int i = 0; i < iterations; ++i) {\n"
        "        travel += clamp(depthDelta, -stepMax, stepMax);\n"
        "        travel = max(0.01, travel);\n"
        "        vec3 offset = travel * ray;\n"
        "        vec2 uv = mix(originUv, rayEndUv,\n"
        "            (1.0 / (origin.z + offset.z)) * denomInv - interpOffset);\n"
        "        if (uv.x <= 0.001 || uv.x >= 0.999 ||\n"
        "            uv.y <= 0.001 || uv.y >= 0.999) break;\n"
        "        float sampleDepth = nwnRainDepth(uv);\n"
        "        depthDelta = sampleDepth + origin.z + offset.z;\n"
        "        float relativeError = depthDelta / (travel + 1.0);\n"
        "        if (abs(relativeError) <= abs(minRelativeError)) {\n"
        "            hitUv = uv; minRelativeError = relativeError;\n"
        "            hitDepth = sampleDepth;\n"
        "        }\n"
        "        if (depthDelta < 0.0) crossed = true;\n"
        "        if (crossed) stepMax *= 0.5;\n"
        "    }\n"
        "    float error = clamp(abs(minRelativeError) / 0.20, 0.0, 1.0);\n"
        "    return vec4(hitUv, -hitDepth, error);\n"
        "}\n"
        // Return scalar ripple height plus its analytic world-space gradient.
        // Reflection/refraction consume the gradient as a normal; no ring is
        // painted directly into the material colour.
        "vec3 nwnRainRippleField(vec2 worldXY, float time, float rippleScale) {\n"
        "    vec2 grid = worldXY * 1.45;\n"
        "    vec2 baseCell = floor(grid);\n"
        "    float height = 0.0;\n"
        "    vec2 gradient = vec2(0.0);\n"
        // Include neighboring cells so a ring remains continuous when its
        // radius crosses a procedural-cell boundary.
        "    for (int y = -1; y <= 1; ++y) {\n"
        "        for (int x = -1; x <= 1; ++x) {\n"
        "            vec2 cell = baseCell + vec2(float(x), float(y));\n"
        "            float seed = nwnRainHash(cell);\n"
        "            vec2 center = cell + vec2(nwnRainHash(cell + 13.1),\n"
        "                                      nwnRainHash(cell + 37.7));\n"
        "            vec2 delta = grid - center;\n"
        "            float safeScale = max(rippleScale, 0.01);\n"
        "            float maxRadius = 0.62 * safeScale;\n"
        "            float radius = fract(time * 0.72 + seed * 5.37) * maxRadius;\n"
        "            float dist = length(delta);\n"
        "            float q = (dist - radius) / safeScale;\n"
        "            float fade = 1.0 - radius / maxRadius;\n"
        "            float ringHeight = exp(-58.0 * q * q) * fade;\n"
        "            height += ringHeight;\n"
        "            if (dist > 0.0001) {\n"
        "                float dHeight = -116.0 * q * ringHeight / safeScale;\n"
        "                gradient += normalize(delta) * dHeight * 1.45;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    gradient /= max(1.0, length(gradient) * 0.22);\n"
        "    return vec3(clamp(height, 0.0, 1.0), gradient);\n"
        "}\n"
        "float nwnRainGroundNoiseHeight(vec2 p, float time) {\n"
        "    if (nwnWeatherNoiseReady > 0.5) {\n"
        "        float a = texture2D(nwnWeatherNoiseTex,\n"
        "                            p * 0.58 + vec2(time * 0.021, -time * 0.047)).r;\n"
        "        float b = texture2D(nwnWeatherNoiseTex,\n"
        "                            p * 1.73 + vec2(-time * 0.063, time * 0.029)).r;\n"
        "        return mix(a, b, 0.38);\n"
        "    }\n"
        "    return 0.62 * nwnRainNoise(p * 0.58 + time * 0.021)\n"
        "         + 0.38 * nwnRainNoise(p * 1.73 - time * 0.037);\n"
        "}\n"
        "vec2 nwnRainGroundNoiseSlope(vec2 p, float time) {\n"
        "    const float e = 0.035;\n"
        "    float h = nwnRainGroundNoiseHeight(p, time);\n"
        "    return vec2(nwnRainGroundNoiseHeight(p + vec2(e, 0.0), time) - h,\n"
        "                nwnRainGroundNoiseHeight(p + vec2(0.0, e), time) - h) / e;\n"
        "}\n"
        "float nwnSnowHeight(vec2 p, float amount) {\n"
        "    float broad = 0.56 * nwnRainNoise(p * 0.92 + 41.7)\n"
        "                + 0.44 * nwnRainNoise(p * 2.15 - 18.2);\n"
        "    float grain = nwnRainNoise(p * 8.40 + 7.1);\n"
        "    float field = broad + (grain - 0.5) * 0.20;\n"
        "    float threshold = mix(0.74, 0.25, amount);\n"
        "    float cover = smoothstep(threshold - 0.075, threshold + 0.065, field);\n"
        // Lower densities retain irregular accumulation islands. As snowfall
        // approaches full strength, introduce a continuous minimum blanket so
        // bright local lights cannot reveal bare albedo gaps and make the
        // layer appear to lift away.
        "    float islands = cover * mix(0.52, 1.0, broad);\n"
        "    float blanket = smoothstep(0.72, 1.0, amount) * 0.22;\n"
        "    return amount * max(islands, blanket);\n"
        "}\n"
        "float nwnSnowTrackDepth(vec2 p, float surfaceZ) {\n"
        "    if (nwnWeatherSnowDeformationTransform.w < 0.5) return 0.0;\n"
        "    vec2 uv = (p - nwnWeatherSnowDeformationTransform.xy)\n"
        "              * nwnWeatherSnowDeformationTransform.z;\n"
        "    if (uv.x <= 0.0 || uv.x >= 1.0 ||\n"
        "        uv.y <= 0.0 || uv.y >= 1.0) return 0.0;\n"
        "    vec2 track = texture2D(nwnWeatherSnowDeformationTex, uv).rg;\n"
        "    float sameLevel = 1.0 - smoothstep(0.38, 0.82,\n"
        "                                      abs(surfaceZ - track.y));\n"
        "    return track.x * sameLevel;\n"
        "}\n"
        "float nwnSnowSurfaceHeight(vec2 p, float amount, float surfaceZ) {\n"
        "    float base = nwnSnowHeight(p, amount);\n"
        "    float carved = nwnSnowTrackDepth(p, surfaceZ) * min(amount, 0.82);\n"
        "    return max(0.0, base - carved);\n"
        "}\n"
        "vec3 nwnSnowPointLight(int lightIndex, vec3 worldPos,\n"
        "                       vec3 worldNormal) {\n"
        "    float lightTexel = (float(lightIndex * 2) + 0.5) / 256.0;\n"
        "    vec4 posRadius = texture2D(nwnWeatherSnowLightData,\n"
        "                               vec2(lightTexel, 0.5));\n"
        "    vec4 color = texture2D(nwnWeatherSnowLightData,\n"
        "                           vec2(lightTexel + 1.0 / 256.0, 0.5));\n"
        "    if (posRadius.w <= 0.0) return vec3(0.0);\n"
        "    vec3 toLight = posRadius.xyz - worldPos;\n"
        "    float d2 = dot(toLight, toLight);\n"
        "    if (d2 >= posRadius.w) return vec3(0.0);\n"
        "    float f = d2 / posRadius.w;\n"
        "    float attenuation = (1.0 - f) / max(0.0001,\n"
        "        nwnWeatherSnowLightFalloff.x + nwnWeatherSnowLightFalloff.y * f);\n"
        "    float diffuse = 0.24 + 0.76 * max(dot(worldNormal,\n"
        "                              toLight * inversesqrt(max(d2, 0.0001))), 0.0);\n"
        "    return color.rgb * max(attenuation, 0.0) * diffuse;\n"
        "}\n";
    source.insert(mainPos, kDeclarations);

    const size_t shiftedOutput = outputPos + std::strlen(kDeclarations);
    static const char* kBody =
        "// injector-owned procedural rain puddles\n"
        "#if POSITION_VIEW == 1 && VERTEX_NORMAL == 1\n"
        "if (nwnWeatherEligible > 0.5 && nwnWeatherRainAmount > 0.001 &&\n"
        "    (areaFlags & 1) == 0) {\n"
        "    vec3 nwnRainWorld = (m_view_inv * vec4(vPosView, 1.0)).xyz;\n"
        "    float nwnRainExposure = nwnWeatherPrecipitationExposure(nwnRainWorld);\n"
        // Material normals include normal maps and smoothed cliff normals, so
        // they cannot decide whether standing water is physically plausible.
        // Position derivatives recover the actual triangle plane instead.
        "    vec3 nwnRainDx = dFdx(nwnRainWorld);\n"
        "    vec3 nwnRainDy = dFdy(nwnRainWorld);\n"
        "    vec3 nwnRainGeomNormal = normalize(cross(nwnRainDx, nwnRainDy));\n"
        "    float nwnRainGeomUp = abs(nwnRainGeomNormal.z);\n"
        "    float nwnRainStaticAlpha = step(1.5, nwnWeatherEligible);\n"
        // Geometry derivatives remain the hard cliff rejection. Interpolated
        // stock surface normals provide the smooth flatness transition across
        // neighboring terrain triangles, eliminating tile/triangle outlines.
        "    vec3 nwnRainSmoothWorldN = normalize(mat3(m_view_inv) * vSurfaceNormal);\n"
        "    float nwnRainCliffReject = smoothstep(0.68, 0.78, nwnRainGeomUp);\n"
        "    float nwnRainSmoothFlat = smoothstep(0.88, 0.98,\n"
        "                                          abs(nwnRainSmoothWorldN.z));\n"
        "    float nwnRainFlat = nwnRainCliffReject * nwnRainSmoothFlat;\n"
        "    float nwnRainFacing = mix(0.28, 1.0, smoothstep(0.08, 0.78, nwnRainGeomUp));\n"
        "    float nwnRainMask = nwnRainPuddle(nwnRainWorld.xy, nwnWeatherRainAmount)\n"
        "                      * nwnRainFlat * nwnWeatherRainAmount\n"
        "                      * (1.0 - nwnRainStaticAlpha);\n"
        // This is perceived shallow depth only. It drives absorption and
        // refraction strength but never writes gl_FragDepth or moves geometry.
        "    float nwnRainOpticalDepth = smoothstep(0.08, 0.92, nwnRainMask);\n"
        // Apply only the visibility left after NWN's native fog pass. The rain
        // branch remains at the safe final-colour location, so shadow/A2C
        // material logic is untouched, while distant wetness converges back to
        // the already-fogged base material exactly as shadow strength does.
        "    float nwnRainFogVisibility = 1.0;\n"
        "#if FOG == 1\n"
        "    nwnRainFogVisibility = 1.0 - GetFogAlpha(fFogFragCoord);\n"
        "#endif\n"
        "    nwnRainMask *= nwnRainFogVisibility;\n"
        "    nwnRainMask *= nwnRainExposure;\n"
        "    float nwnRainGrazing = pow(1.0 - abs(dot(normalize(-vPosView),\n"
        "                                      normalize(vFragmentNormal))), 4.0);\n"
        // Every exposed static surface may become mildly wet. Only the strict
        // geometric-flatness branch below can become standing water.
        "    float nwnRainFilm = nwnWeatherRainAmount * nwnRainFacing * 0.14\n"
        "                      * nwnRainFogVisibility * nwnRainExposure;\n"
        // Non-puddle wet ground gets an engine-noise micro-normal plus the
        // accepted scalar impact normals. Only a restrained normal-derived sky
        // specular term is added; there is no painted ring or depth write.
        "    float nwnRainGroundOnly = (1.0 - smoothstep(0.04, 0.32,\n"
        "                                      nwnRainOpticalDepth))\n"
        "                              * nwnRainSmoothFlat;\n"
        // Alpha/card surfaces may be vertical or sloped. Give them the same
        // film disturbance through a dominant-axis projection, but never the
        // flatness-driven standing-water branch used by opaque terrain.
        "    nwnRainGroundOnly = max(nwnRainGroundOnly,\n"
        "                             nwnRainStaticAlpha * nwnRainFacing);\n"
        "    vec3 nwnRainAbsGeomNormal = abs(nwnRainGeomNormal);\n"
        "    vec2 nwnRainNoiseCoord = nwnRainWorld.xy;\n"
        "    vec3 nwnRainNoiseBaseN = vec3(0.0, 0.0, 1.0);\n"
        "    vec3 nwnRainNoiseTangentU = vec3(1.0, 0.0, 0.0);\n"
        "    vec3 nwnRainNoiseTangentV = vec3(0.0, 1.0, 0.0);\n"
        "    if (nwnRainStaticAlpha > 0.5) {\n"
        "        nwnRainNoiseBaseN = nwnRainSmoothWorldN;\n"
        "        if (nwnRainAbsGeomNormal.x > nwnRainAbsGeomNormal.y &&\n"
        "            nwnRainAbsGeomNormal.x > nwnRainAbsGeomNormal.z) {\n"
        "            nwnRainNoiseCoord = nwnRainWorld.yz;\n"
        "            nwnRainNoiseTangentU = vec3(0.0, 1.0, 0.0);\n"
        "            nwnRainNoiseTangentV = vec3(0.0, 0.0, 1.0);\n"
        "        } else if (nwnRainAbsGeomNormal.y > nwnRainAbsGeomNormal.z) {\n"
        "            nwnRainNoiseCoord = nwnRainWorld.xz;\n"
        "            nwnRainNoiseTangentU = vec3(1.0, 0.0, 0.0);\n"
        "            nwnRainNoiseTangentV = vec3(0.0, 0.0, 1.0);\n"
        "        }\n"
        "    }\n"
        "    float nwnRainGroundNoise = 0.5;\n"
        "    float nwnRainGroundSpec = 0.0;\n"
        "    if (nwnRainGroundOnly > 0.001) {\n"
        "        nwnRainGroundNoise = nwnRainGroundNoiseHeight(\n"
        "                                      nwnRainNoiseCoord, nwnWeatherTime);\n"
        "        vec2 nwnRainGroundSlope = nwnRainGroundNoiseSlope(\n"
        "                                      nwnRainNoiseCoord, nwnWeatherTime);\n"
        // Ground drops temporarily erase the fine moving film beneath their
        // contact wave. The wave then replaces that detail in the normal only:
        // it breaks the noise optically without painting a ring into albedo.
        "        vec3 nwnRainGroundImpact = nwnRainRippleField(\n"
        "                                      nwnRainNoiseCoord * 0.95,\n"
        "                                      nwnWeatherTime * 1.32, 0.50);\n"
        "        float nwnRainGroundBreak = smoothstep(0.035, 0.30,\n"
        "                                      nwnRainGroundImpact.x);\n"
        "        nwnRainGroundNoise = mix(nwnRainGroundNoise, 0.5,\n"
        "                                  nwnRainGroundBreak * 0.82);\n"
        "        vec2 nwnRainGroundSurface = nwnRainGroundSlope * 0.050\n"
        "                                  * (1.0 - nwnRainGroundBreak * 0.72)\n"
        "                                  - nwnRainGroundImpact.yz * 0.032;\n"
        "        vec3 nwnRainGroundWorldN = normalize(nwnRainNoiseBaseN\n"
        "                                      - nwnRainGroundSurface.x\n"
        "                                        * nwnRainNoiseTangentU\n"
        "                                      - nwnRainGroundSurface.y\n"
        "                                        * nwnRainNoiseTangentV);\n"
        "        vec3 nwnRainGroundViewN = normalize(\n"
        "                                      mat3(m_view) * nwnRainGroundWorldN);\n"
        "        vec3 nwnRainGroundIncident = normalize(vPosView);\n"
        "        vec3 nwnRainSkyView = normalize(\n"
        "                                      mat3(m_view) * vec3(0.0, 0.0, 1.0));\n"
        "        vec3 nwnRainGroundHalf = normalize(\n"
        "                                      -nwnRainGroundIncident + nwnRainSkyView);\n"
        "        nwnRainGroundSpec = pow(max(dot(nwnRainGroundViewN,\n"
        "                                      nwnRainGroundHalf), 0.0), 20.0)\n"
        "                              * mix(0.45, 1.0, nwnRainGroundNoise)\n"
        "                              * nwnRainGroundOnly;\n"
        "    }\n"
        "    nwnRainFilm *= mix(1.0, mix(0.62, 1.42, nwnRainGroundNoise),\n"
        "                       nwnRainGroundOnly);\n"
        "    vec3 nwnRainFilmColor = FragmentColor.rgb * vec3(0.84, 0.87, 0.90);\n"
        "    nwnRainFilmColor += vec3(0.025, 0.035, 0.05) * nwnRainGrazing;\n"
        "    FragmentColor.rgb = mix(FragmentColor.rgb, nwnRainFilmColor, nwnRainFilm);\n"
        // Do not bury the micro-normal response inside the 14% film blend.
        // Apply a bounded normal highlight and low-contrast noise modulation
        // directly to visible, non-puddle wet ground.
        "    float nwnRainGroundVisibility = nwnRainGroundOnly * nwnWeatherRainAmount\n"
        "                                  * nwnRainFogVisibility * nwnRainExposure;\n"
        "    FragmentColor.rgb *= 1.0 + (nwnRainGroundNoise - 0.5) * 0.10\n"
        "                              * nwnRainGroundVisibility;\n"
        "    FragmentColor.rgb += vec3(0.11, 0.135, 0.17)\n"
        "                           * nwnRainGroundSpec * nwnWeatherRainAmount\n"
        "                           * nwnRainFogVisibility * nwnRainExposure;\n"
        "    vec3 nwnRainAbsorption = mix(vec3(0.74, 0.78, 0.83),\n"
        "                                 vec3(0.42, 0.49, 0.58),\n"
        "                                 nwnRainOpticalDepth);\n"
        "    vec3 nwnRainWetColor = FragmentColor.rgb * nwnRainAbsorption;\n"
        "    nwnRainWetColor += vec3(0.10, 0.13, 0.17) * nwnRainGrazing;\n"
        "    if (nwnWeatherReflectionReady > 0.5 && nwnRainMask > 0.002) {\n"
        "        vec3 nwnRainRipple = nwnRainRippleField(nwnRainWorld.xy,\n"
        "                                                nwnWeatherTime, 1.0);\n"
        "        vec2 nwnRainWave = vec2(\n"
        "            sin(nwnRainWorld.x * 5.1 + nwnRainWorld.y * 2.7 + nwnWeatherTime * 1.4),\n"
        "            cos(nwnRainWorld.y * 4.7 - nwnRainWorld.x * 2.2 + nwnWeatherTime * 1.1));\n"
        "        vec2 nwnRainSurface = -nwnRainRipple.yz * 0.020\n"
        "                            + nwnRainWave * 0.012;\n"
        "        vec3 nwnRainWaterWorldN = normalize(vec3(nwnRainSurface, 1.0));\n"
        "        vec3 nwnRainWaterViewN = normalize(mat3(m_view) * nwnRainWaterWorldN);\n"
        "        vec3 nwnRainIncident = normalize(vPosView);\n"
        // The sampled color/depth pair belongs to the camera active when NWN
        // copied it. Reproject the current world-space puddle and reflection
        // ray into that exact camera instead of mixing frame generations.
        "        vec3 nwnRainFbOrigin = (nwnWeatherFbView\n"
        "            * vec4(nwnRainWorld, 1.0)).xyz;\n"
        "        vec3 nwnRainRayWorld = mat3(m_view_inv)\n"
        "            * reflect(nwnRainIncident, nwnRainWaterViewN);\n"
        "        vec3 nwnRainFbRay = normalize(mat3(nwnWeatherFbView)\n"
        "            * nwnRainRayWorld);\n"
        "        vec4 nwnRainFbClip = nwnWeatherFbProj\n"
        "            * vec4(nwnRainFbOrigin, 1.0);\n"
        "        vec2 nwnRainScreenUv = nwnRainFbClip.xy\n"
        "            / max(0.00001, nwnRainFbClip.w) * 0.5 + 0.5;\n"
        // Refraction dominates from above, as it does in fswater.shd. Reject
        // offsets that cross onto a different depth layer so a nearby creature
        // cannot smear into the puddle through the previous framebuffer.
        "        float nwnRainRefractionScale = mix(0.0035, 0.0115,\n"
        "                                               nwnRainOpticalDepth);\n"
        "        vec2 nwnRainRefractUv = clamp(nwnRainScreenUv +\n"
        "            nwnRainSurface * vec2(nwnRainRefractionScale,\n"
        "                nwnRainRefractionScale * pixelParams.x /\n"
        "                                      max(0.00001, pixelParams.y)),\n"
        "            vec2(0.002), vec2(0.998));\n"
        "        vec3 nwnRainRefractPos = nwnRainViewPosition(nwnRainRefractUv);\n"
        "        float nwnRainRefractGate = 1.0 - smoothstep(0.12, 1.25,\n"
        "            abs(nwnRainRefractPos.z - nwnRainFbOrigin.z));\n"
        "        vec3 nwnRainRefracted = texture2D(nwnWeatherFbColor,\n"
        "                                           nwnRainRefractUv).rgb;\n"
        "        float nwnRainRefractionMix = nwnRainRefractGate\n"
        "            * mix(0.20, 0.62, nwnRainOpticalDepth);\n"
        "        nwnRainWetColor = mix(nwnRainWetColor, nwnRainRefracted,\n"
        "                              nwnRainRefractionMix);\n"
        "        vec4 nwnRainHit = nwnRainTraceReflection(nwnRainFbOrigin +\n"
        "            normalize(mat3(nwnWeatherFbView) * nwnRainWaterWorldN)\n"
        "            * 0.035, nwnRainFbRay);\n"
        "        vec3 nwnRainReflected = texture2D(nwnWeatherFbColor,\n"
        "                                      clamp(nwnRainHit.xy, 0.002, 0.998)).rgb;\n"
        "        float nwnRainNdotV = clamp(dot(nwnRainWaterViewN,\n"
        "                                      -nwnRainIncident), 0.0, 1.0);\n"
        "        float nwnRainFresnel = 0.02 + 0.98 * pow(1.0 - nwnRainNdotV, 5.0);\n"
        "        float nwnRainHitValid = step(0.0, nwnRainHit.x) *\n"
        "            step(0.0, nwnRainHit.y) * step(nwnRainHit.x, 1.0) *\n"
        "            step(nwnRainHit.y, 1.0);\n"
        "        vec3 nwnRainHitNormal = nwnRainDepthNormal(\n"
        "                                      clamp(nwnRainHit.xy, 0.002, 0.998));\n"
        "        float nwnRainNormalGate = clamp(0.1 - 4.0 *\n"
        "                                      dot(nwnRainHitNormal, nwnRainFbRay),\n"
        "                                      0.0, 1.0);\n"
        "        float nwnRainEdge = min(min(nwnRainHit.x, nwnRainHit.y),\n"
        "            min(1.0 - nwnRainHit.x, 1.0 - nwnRainHit.y));\n"
        "        float nwnRainReflectMix = nwnRainHitValid * nwnRainNormalGate *\n"
        "            (1.0 - nwnRainHit.w) * smoothstep(0.0, 0.055, nwnRainEdge) *\n"
        "            nwnRainFresnel * 0.92;\n"
        "        nwnRainWetColor = mix(nwnRainWetColor, nwnRainReflected,\n"
        "                              nwnRainReflectMix);\n"
        "    }\n"
        "    FragmentColor.rgb = mix(FragmentColor.rgb, nwnRainWetColor,\n"
        "                            clamp(nwnRainMask * 0.86, 0.0, 0.86));\n"
        "}\n"
        "if (nwnWeatherEligible > 0.5 && nwnWeatherEligible < 1.5 &&\n"
        "    nwnWeatherSnowAmount > 0.001 && (areaFlags & 1) == 0) {\n"
        "    vec3 nwnSnowWorld = (m_view_inv * vec4(vPosView, 1.0)).xyz;\n"
        "    float nwnSnowExposure = nwnWeatherPrecipitationExposure(nwnSnowWorld);\n"
        // The occlusion fringe is an accumulation fade, not a displaced snow
        // bank. Suppress parallax and relief normals until the receiver is
        // almost fully exposed, then ease them in over the outer edge.
        "    float nwnSnowReliefExposure = smoothstep(0.80, 0.98,\n"
        "                                              nwnSnowExposure);\n"
        "    vec3 nwnSnowDx = dFdx(nwnSnowWorld);\n"
        "    vec3 nwnSnowDy = dFdy(nwnSnowWorld);\n"
        "    vec3 nwnSnowGeomN = normalize(cross(nwnSnowDx, nwnSnowDy));\n"
        "    vec3 nwnSnowSmoothN = normalize(mat3(m_view_inv) * vSurfaceNormal);\n"
        // Settlement is a geometry decision. Rough cobble/rock normal maps can
        // point their smooth material normal sideways even when the underlying
        // triangle is level; using that as a hard gate made snow disappear on
        // non-snow tilesets. Keep it only as a mild surface-shape modulation.
        "    float nwnSnowGeomSettle = smoothstep(0.38, 0.74,\n"
        "                                           abs(nwnSnowGeomN.z));\n"
        "    float nwnSnowNormalHint = smoothstep(0.18, 0.78,\n"
        "                                           abs(nwnSnowSmoothN.z));\n"
        "    float nwnSnowSettle = nwnSnowGeomSettle\n"
        "                        * mix(0.78, 1.0, nwnSnowNormalHint);\n"
        "    float nwnSnowFogVisibility = 1.0;\n"
        "#if FOG == 1\n"
        "    nwnSnowFogVisibility = 1.0 - GetFogAlpha(fFogFragCoord);\n"
        "#endif\n"
        // Parallax is a bounded world-XY height walk. It changes perceived
        // depth and coverage edges without modifying geometry or gl_FragDepth.
        "    vec3 nwnSnowViewWorld = normalize(mat3(m_view_inv)\n"
        "                                      * normalize(-vPosView));\n"
        "    vec2 nwnSnowRay = -nwnSnowViewWorld.xy\n"
        "        / max(0.30, abs(nwnSnowViewWorld.z))\n"
        "        * (0.220 * nwnWeatherSnowAmount * nwnSnowReliefExposure);\n"
        "    vec2 nwnSnowCoord = nwnSnowWorld.xy;\n"
        "    float nwnSnowLayer = 1.0;\n"
        "    const float nwnSnowLayerStep = 0.0833333;\n"
        "    for (int nwnSnowStep = 0; nwnSnowStep < 12; ++nwnSnowStep) {\n"
        "        float nwnSnowSample = nwnSnowSurfaceHeight(nwnSnowCoord,\n"
        "                           nwnWeatherSnowAmount, nwnSnowWorld.z);\n"
        "        if (nwnSnowLayer > nwnSnowSample)\n"
        "            nwnSnowCoord += nwnSnowRay * nwnSnowLayerStep;\n"
        "        nwnSnowLayer -= nwnSnowLayerStep;\n"
        "    }\n"
        "    float nwnSnowH = nwnSnowSurfaceHeight(nwnSnowCoord,\n"
        "                       nwnWeatherSnowAmount, nwnSnowWorld.z);\n"
        "    float nwnSnowCoverage = smoothstep(0.025, 0.20, nwnSnowH)\n"
        "                          * nwnSnowSettle * nwnSnowFogVisibility\n"
        "                          * nwnSnowExposure;\n"
        "    const float nwnSnowE = 0.045;\n"
        "    vec2 nwnSnowSlope = vec2(\n"
        "        nwnSnowSurfaceHeight(nwnSnowCoord + vec2(nwnSnowE, 0.0),\n"
        "                 nwnWeatherSnowAmount, nwnSnowWorld.z) - nwnSnowH,\n"
        "        nwnSnowSurfaceHeight(nwnSnowCoord + vec2(0.0, nwnSnowE),\n"
        "                 nwnWeatherSnowAmount, nwnSnowWorld.z) - nwnSnowH) / nwnSnowE;\n"
        "    vec3 nwnSnowWorldN = normalize(vec3(\n"
        "        -nwnSnowSlope * (0.22 * nwnSnowReliefExposure), 1.0));\n"
        "    vec3 nwnSnowViewN = normalize(mat3(m_view) * nwnSnowWorldN);\n"
        "    float nwnSnowFacing = clamp(dot(nwnSnowViewN,\n"
        "                                      normalize(-vPosView)), 0.0, 1.0);\n"
        // Re-light a neutral snow surface. FragmentColor is deliberately not
        // used here because it includes the covered material's albedo; copying
        // it made local-light pools reproduce cobblestone inside the snow.
        "    vec3 nwnSnowIllumination = max(nwnWeatherSnowAreaLight.rgb, vec3(0.10));\n"
        "    for (int nwnSnowLight = 0; nwnSnowLight < 128; ++nwnSnowLight) {\n"
        "        if (nwnSnowLight >= nwnWeatherSnowLightCount) break;\n"
        "        nwnSnowIllumination += nwnSnowPointLight(nwnSnowLight,\n"
        "                                  nwnSnowWorld, nwnSnowWorldN);\n"
        "    }\n"
        "    vec3 nwnSnowColor = nwnSnowIllumination\n"
        "                      / (vec3(1.0) + nwnSnowIllumination);\n"
        "    nwnSnowColor = clamp(nwnSnowColor * 1.42 + vec3(0.12),\n"
        "                         vec3(0.16), vec3(1.0));\n"
        "    float nwnSnowGrain = nwnRainNoise(nwnSnowCoord * 5.7 + 83.0);\n"
        "    nwnSnowColor *= mix(0.90, 1.08, nwnSnowGrain)\n"
        "                  * mix(0.82, 1.04, nwnSnowFacing);\n"
        "    FragmentColor.rgb = mix(FragmentColor.rgb, nwnSnowColor,\n"
        "                            clamp(nwnSnowCoverage * 0.97, 0.0, 0.97));\n"
        "}\n"
        "#endif\n"
        "    ";
    source.insert(shiftedOutput, kBody);
    return true;
}

} // namespace weather_runtime

extern "C" int weather_area_set_weather(void* area, uint8_t weather,
                                      float densityPercent) {
    int result = 0;
    if (weather_runtime::g_weatherTrampoline)
        result = weather_runtime::g_weatherTrampoline(area, weather, densityPercent);
    else if (weather_runtime::g_weatherHook && eng::AreaSetWeather) {
        // Same validated Linux fallback used by the renderer trace hooks for
        // prologues subhook cannot relocate: remove, call the real entry, then
        // reinstall. CNWCArea weather changes run on the single render thread
        // and occur only on area/weather transitions, so this is not a hot path.
        subhook_remove(weather_runtime::g_weatherHook);
        result = eng::AreaSetWeather(area, weather, densityPercent);
        subhook_install(weather_runtime::g_weatherHook);
    }
    if (result)
        weather_runtime::observe_engine_weather(area, (int)weather, densityPercent);
    return result;
}

static void install_weather_engine_hooks() {
    if (!weather_runtime::g_weatherHook && eng::AreaSetWeather)
        weather_runtime::g_weatherHook = subhook_new(
            (void*)eng::AreaSetWeather, (void*)weather_area_set_weather,
            SUBHOOK_64BIT_OFFSET);
    if (weather_runtime::g_weatherHook &&
        subhook_install(weather_runtime::g_weatherHook) != 0) {
        fprintf(stderr,
                "[weather][linux] warning: CNWCArea::SetWeather hook unavailable; "
                "weather effects will fail closed\n");
        subhook_free(weather_runtime::g_weatherHook);
        weather_runtime::g_weatherHook = nullptr;
    }
    if (weather_runtime::g_weatherHook) {
        weather_runtime::g_weatherTrampoline = (eng::AreaSetWeather_t)
            subhook_get_trampoline(weather_runtime::g_weatherHook);
        fprintf(stderr, "[weather][linux] engine weather observer active%s\n",
                weather_runtime::g_weatherTrampoline ? "" :
                " (remove/call/reinstall fallback)");
    }

    fprintf(stderr,
            "[weather][linux] injector-owned framebuffer synchronization active\n");
}

static void uninstall_weather_engine_hooks() {
    if (weather_runtime::g.snowDeformationTexture && gl::DeleteTextures) {
        gl::DeleteTextures(1, &weather_runtime::g.snowDeformationTexture);
        weather_runtime::g.snowDeformationTexture = 0;
    }
    if (weather_runtime::g.snowLightDataTexture && gl::DeleteTextures) {
        gl::DeleteTextures(1, &weather_runtime::g.snowLightDataTexture);
        weather_runtime::g.snowLightDataTexture = 0;
    }
    if (weather_runtime::g.precipitationOcclusionFbo && gl::DeleteFramebuffers) {
        gl::DeleteFramebuffers(1,
            &weather_runtime::g.precipitationOcclusionFbo);
        weather_runtime::g.precipitationOcclusionFbo = 0;
    }
    if (weather_runtime::g.precipitationOcclusionTexture && gl::DeleteTextures) {
        gl::DeleteTextures(1,
            &weather_runtime::g.precipitationOcclusionTexture);
        weather_runtime::g.precipitationOcclusionTexture = 0;
    }
    if (weather_runtime::g_weatherHook) {
        subhook_remove(weather_runtime::g_weatherHook);
        subhook_free(weather_runtime::g_weatherHook);
        weather_runtime::g_weatherHook = nullptr;
        weather_runtime::g_weatherTrampoline = nullptr;
    }
}

static bool weather_needs_shader_sources() { return true; }
static bool weather_needs_draw_observer() { return true; }
static bool weather_patch_shader_source(std::string& source) {
    return weather_runtime::patch_fragment_shader(source);
}
static void weather_observe_draw(GLuint program, GLsizei count) {
    weather_runtime::observe_draw(program, count);
}
static void weather_note_program(GLuint program) {
    weather_runtime::note_program(program);
}
static bool weather_tracks_dynamic_creatures() {
    return weather_runtime::g.snowAmount > 0.015f;
}
static void weather_observe_dynamic_position(const float position[3]) {
    if (position)
        weather_runtime::collect_contact_candidate(position[0], position[1],
                                                   position[2]);
}
extern "C" void weather_uniform1i(GLint location, GLint value) {
    weather_runtime::observe_uniform1i(location, value);
    if (weather_runtime::g_realUniform1i)
        weather_runtime::g_realUniform1i(location, value);
}
static void install_weather_uniform_patch() {
    static bool tried = false;
    if (tried || !eng::GlewUniform1i) return;
    tried = true;
    void* current = *eng::GlewUniform1i;
    if (!current) { tried = false; return; }
    weather_runtime::g_realUniform1i = (weather_runtime::Uniform1i_t)current;
    *eng::GlewUniform1i = (void*)weather_uniform1i;
    fprintf(stderr, "[weather][linux] hooked glUniform1i for skinmesh classification\n");
}
static void weather_scene_begin(void* scene) { weather_runtime::scene_begin(scene); }
static void weather_scene_end(void* scene) { weather_runtime::scene_end(scene); }

#else

// Deliberate Windows hold: the accepted Linux module is ported only after a
// separate request, with platform-specific changes unless a shared bug is
// confirmed first.
static bool weather_needs_shader_sources() { return false; }
static bool weather_needs_draw_observer() { return false; }
static bool weather_patch_shader_source(std::string&) { return false; }
static void weather_observe_draw(GLuint, GLsizei) {}
static void weather_note_program(GLuint) {}
static bool weather_tracks_dynamic_creatures() { return false; }
static void weather_observe_dynamic_position(const float[3]) {}
static void install_weather_uniform_patch() {}
static void install_weather_engine_hooks() {}
static void uninstall_weather_engine_hooks() {}
static void weather_scene_begin(void*) {}
static void weather_scene_end(void*) {}

#endif
