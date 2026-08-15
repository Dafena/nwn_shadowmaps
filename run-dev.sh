#!/usr/bin/env bash
# THE development launcher. Use this one.
#
# It replaces the pile that grew one script per phase:
#
#   run-shadowmap.sh                              sun only -- local lights were
#                                                 commented out on 2026-08-10 and
#                                                 never turned back on, which is
#                                                 the trap this script exists to
#                                                 remove
#   run-shadowmap-local-light-shadows.sh          identical PLUS the three
#                                                 LOCAL_LIGHT_* flags. That was
#                                                 the ONLY difference between them
#   run-shadowmap-full-bsp-csm-filtered-shadows*  older phase configs
#   run-shadowmap-local-light-probe.sh            one-off probe
#
# Everything here is overridable from the environment, so a one-off experiment
# needs no new script -- that is how the pile happened:
#
#   NWN_SHADOWMAP_CSM_DYNAMIC_CASCADES=1 ./run-dev.sh
#   NWN_SHADOWMAP_LOCAL_LIGHT_RECEIVER=0 ./run-dev.sh      sun only
#   NWN_DEV_NO_BUILD=1 ./run-dev.sh                        skip the rebuild
#
# EVERY diagnostic is ON here -- this is the development build and the whole
# point of it. The shipping builds are the ones that turn this off.
#
# The log lands in shadowmap-phase1.log (run-shadowmap-trace.sh owns that).
# Panel settings persist by default.  For a deliberately stateless A/B run,
# launch with NWN_SHADOWMAP_NO_SETTINGS=1 in the environment.
set -o pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Rebuild first. The single most common way to test the wrong thing is to launch
# a .so from an hour ago; `make` is a no-op when nothing changed.
if [[ "${NWN_DEV_NO_BUILD:-0}" != "1" ]]; then
    if ! make -C "$HERE" 2>&1 | grep -vE "^make(\[[0-9]+\])?: (Nothing|Entering|Leaving)"; then
        :   # grep found nothing to print, which is the quiet success case
    fi
    if [[ ! -f "$HERE/libnwn_shadowmap.so" ]]; then
        echo "build failed -- not launching" >&2
        exit 1
    fi
fi

# --- look ------------------------------------------------------------------
SHADOW_STRENGTH="${NWN_SHADOWMAP_CSM_STRENGTH:-0.42}"
CASCADE_BLEND="${NWN_SHADOWMAP_CSM_BLEND:-0.75}"
PCF_RADIUS="${NWN_SHADOWMAP_CSM_PCF_RADIUS:-0.75}"

# --- quality / cost --------------------------------------------------------
# 3 cascades + 3 dynamic measured better looking AND cheaper than 4+4. Extent
# 128 was the maintainer's pick: 32 was crisp but too short, 256 lost small
# objects' shadows. The panel overrides all of these live.
CASCADES="${NWN_SHADOWMAP_CSM_CASCADES:-3}"
DYN_CASCADES="${NWN_SHADOWMAP_CSM_DYNAMIC_CASCADES:-3}"
NEAR_CASCADES="${NWN_SHADOWMAP_STATIC_NEAR_CASCADES:-4}"
CASCADE_SIZE="${NWN_SHADOWMAP_SIZE:-2048}"
WORLD_SIZE="${NWN_SHADOWMAP_STATIC_WORLD_SIZE:-8192}"
WORLD_EXTENT="${NWN_SHADOWMAP_STATIC_WORLD_EXTENT:-128}"
# Per-draw static capture is the alpha-safe diagnostic path. The world map is
# a whole-bucket re-entry, so it must stay off while proving the fix.
STATIC_WORLD="${NWN_SHADOWMAP_STATIC_WORLD:-0}"
# Static AlphaDiscard casters (foliage, transparent tile overlays).  Keep this
# independently switchable: these are the materials that flicker under a
# moving scripted sun, while opaque/static and character paths stay active.
# Defaults restore the accepted complete caster set. Override either only for
# a focused A/B, never as a workaround that silently drops foliage shadows.
STATIC_ALPHA_RECEIVER="${NWN_SHADOWMAP_STATIC_ALPHA_RECEIVER:-1}"
STATIC_CASTERS="${NWN_SHADOWMAP_STATIC_CASTERS:-1}"
DYNAMIC_CASTERS="${NWN_SHADOWMAP_DYNAMIC_CASTERS:-0}"
# Presentation-only A/B: 0 keeps every capture/replay path live while skipping
# the fullscreen shadow composite itself.
RECEIVER_DRAW="${NWN_SHADOWMAP_RECEIVER_DRAW:-1}"

# --- local lights ----------------------------------------------------------
# ON here, matching what both shipping builds default to. They were off in
# run-shadowmap.sh from when the receiver drew nothing; it works now.
# TRACE and CAPTURE are read ONCE at startup and cannot be switched on from the
# panel -- the panel's "Local light" checkbox only drives the RECEIVER, so
# without these two it has no depth map to sample and ticking it does nothing.
LOCAL_TRACE="${NWN_SHADOWMAP_LOCAL_LIGHT_TRACE:-1}"
LOCAL_CAPTURE="${NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE:-1}"
LOCAL_RECEIVER="${NWN_SHADOWMAP_LOCAL_LIGHT_RECEIVER:-1}"
LOCAL_SIZE="${NWN_SHADOWMAP_LOCAL_LIGHT_SIZE:-256}"

# --- diagnostics: ALL ON ---------------------------------------------------
# Development build. Everything observational is enabled; the log is
# shadowmap-phase1.log and the .pgm dumps land beside it.
DUMP_PGM="${NWN_SHADOWMAP_DUMP_PGM:-1}"          # cascade/local depth dumps
COST="${NWN_SHADOWMAP_COST:-1}"                  # [cost]/[buckets] frame timings
LAMP_CENSUS="${NWN_SHADOWMAP_LAMP_CENSUS:-1}"    # per-light census
STENCIL_TRACE="${NWN_SHADOWMAP_STENCIL_TRACE:-1}"   # the engine's own shadow pass
LOCAL_LIGHT_DUMP="${NWN_SHADOWMAP_LOCAL_LIGHT_DUMP:-1}"
TRACE_FRAMES="${NWN_SHADOWMAP_TRACE_FRAMES:-90}" # text trace stops after N area frames
TRACE_EVENTS="${NWN_SHADOWMAP_TRACE_EVENTS:-4096}"

# DELIBERATELY NOT SET, and none of these are oversights:
#
#   UNIFORM_TRACE        sits on the per-glUniformMatrix4fv path and does a
#                        glGetIntegerv per upload -- thousands a frame. This is
#                        the exact shape of the bug that made Windows unplayable
#                        for a week. Set it by hand for one run, never by default.
#   VERTEX_REPROJECT     known to produce sparse/incorrect maps (AGENTS.md)
#   LIGHT_CASTER_MATRICES  same
#   CASCADE_VERIFY       one-shot readback; "must never become per-frame"
#   CASCADE_COPY         superseded population bridge
#   COLORCAST_TEST / GPU_TEST / INJECT_TEST   isolated experiments, not diagnostics
#   DUMP_VERTEX / DUMP_FRAGMENT   dump every shader; enormous, use per-run
#   RECEIVER_DEBUG       the 1/2/3/4 colour ladder -- set it when the receiver
#                        draws nothing, not while looking at real shadows

echo "[run-dev] cascades=$CASCADES (+$DYN_CASCADES moving)  cascade=${CASCADE_SIZE}^2" \
     " world=$STATIC_WORLD:${WORLD_SIZE}^2/${WORLD_EXTENT}u  static=$STATIC_CASTERS alpha=$STATIC_ALPHA_RECEIVER dynamic=$DYNAMIC_CASTERS receiver=$RECEIVER_DRAW" \
     " local-lights=$LOCAL_RECEIVER@${LOCAL_SIZE}^2  diagnostics=ALL"

exec env -u NWN_SHADOWMAP_AREA_LIGHT_DIRECTION_APPLY \
    -u NWN_SHADOWMAP_AREA_LIGHT_DIRECTION_STEP \
    -u NWN_SHADOWMAP_AREA_LIGHT_DIRECTION_DELTA \
    -u NWN_SHADOWMAP_AREA_LIGHT_DIRECTION_PROBE \
    -u NWN_SHADOWMAP_RELEASE_TRANSITION_BUFFER \
    NWN_SHADOWMAP_CASCADE_MATH=1 \
    NWN_SHADOWMAP_LIGHT_VECTOR_TRACE=1 \
    NWN_SHADOWMAP_CASCADE_TARGET_VALIDATE=1 \
    NWN_SHADOWMAP_CASCADE_GEOMETRY_TRACE=1 \
    NWN_SHADOWMAP_CASCADE_LIGHT_CAPTURE=1 \
    NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET=0 \
    NWN_SHADOWMAP_STATIC_RECEIVER=1 \
    NWN_SHADOWMAP_STATIC_CASTERS="$STATIC_CASTERS" \
    NWN_SHADOWMAP_DYNAMIC_CASTERS="$DYNAMIC_CASTERS" \
    NWN_SHADOWMAP_RECEIVER_DRAW="$RECEIVER_DRAW" \
    NWN_SHADOWMAP_STATIC_ALPHA_RECEIVER="$STATIC_ALPHA_RECEIVER" \
    NWN_SHADOWMAP_STATIC_ALPHA_BUCKET=1 \
    NWN_SHADOWMAP_CASTER_FULL_BSP_NATIVE_SUBMIT=1 \
    NWN_SHADOWMAP_CSM_STATIC_RECEIVER=1 \
    NWN_SHADOWMAP_CSM_DYNAMIC_RECEIVER=1 \
    NWN_SHADOWMAP_CSM_ALPHA_RECEIVER=1 \
    NWN_SHADOWMAP_CSM_COMPOSITE=1 \
    NWN_SHADOWMAP_CSM_BUCKET_REPLAY=1 \
    NWN_SHADOWMAP_CSM_STRENGTH="$SHADOW_STRENGTH" \
    NWN_SHADOWMAP_CSM_BLEND="$CASCADE_BLEND" \
    NWN_SHADOWMAP_CSM_PCF_RADIUS="$PCF_RADIUS" \
    NWN_SHADOWMAP_CSM_CASCADES="$CASCADES" \
    NWN_SHADOWMAP_CSM_DYNAMIC_CASCADES="$DYN_CASCADES" \
    NWN_SHADOWMAP_STATIC_NEAR_CASCADES="$NEAR_CASCADES" \
    NWN_SHADOWMAP_SIZE="$CASCADE_SIZE" \
    NWN_SHADOWMAP_STATIC_WORLD="$STATIC_WORLD" \
    NWN_SHADOWMAP_STATIC_WORLD_SIZE="$WORLD_SIZE" \
    NWN_SHADOWMAP_STATIC_WORLD_EXTENT="$WORLD_EXTENT" \
    NWN_SHADOWMAP_LOCAL_LIGHT_TRACE="$LOCAL_TRACE" \
    NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE="$LOCAL_CAPTURE" \
    NWN_SHADOWMAP_LOCAL_LIGHT_RECEIVER="$LOCAL_RECEIVER" \
    NWN_SHADOWMAP_LOCAL_LIGHT_SIZE="$LOCAL_SIZE" \
    NWN_SHADOWMAP_DUMP_PGM="$DUMP_PGM" \
    NWN_SHADOWMAP_COST="$COST" \
    NWN_SHADOWMAP_LAMP_CENSUS="$LAMP_CENSUS" \
    NWN_SHADOWMAP_STENCIL_TRACE="$STENCIL_TRACE" \
    NWN_SHADOWMAP_LOCAL_LIGHT_DUMP="$LOCAL_LIGHT_DUMP" \
    NWN_SHADOWMAP_AREA_SHADOW_PROBE=1 \
    NWN_SHADOWMAP_TRACE_FRAMES="$TRACE_FRAMES" \
    NWN_SHADOWMAP_TRACE_EVENTS="$TRACE_EVENTS" \
    "$HERE/run-shadowmap-trace.sh" "$@"
