#!/usr/bin/env bash
# Phase 1 only: record camera/scene/light ordering in a loaded area.  This
# intentionally disables every legacy target/replay/shader/receiver experiment,
# so the game should look exactly like normal NWN while the trace is captured.

set -o pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Locate the real linux-x86 game directory.  The refactored tree is nested as
# linux-x86/csm_claude/refactored, while older standalone copies lived only one
# level below linux-x86.  Prefer an explicit override, then support both layouts.
if [[ -n "${NWN_SHADOWMAP_GAME_DIR:-}" ]]; then
    GAME_DIR="$(cd "$NWN_SHADOWMAP_GAME_DIR" && pwd)"
elif [[ -x "$HERE/../../nwmain-linux" ]]; then
    GAME_DIR="$(cd "$HERE/../.." && pwd)"
elif [[ -x "$HERE/../nwmain-linux" ]]; then
    GAME_DIR="$(cd "$HERE/.." && pwd)"
else
    echo "Could not locate nwmain-linux above $HERE" >&2
    echo "Set NWN_SHADOWMAP_GAME_DIR to the linux-x86 game directory." >&2
    exit 1
fi
# nwmain-linux partly infers its own "base data directory" from the process's
# CURRENT WORKING DIRECTORY at launch, not just argv[0]'s path -- launching
# from this fork's own directory (one level deeper than the validated
# linux-x86/ baseline) made it miscompute that base dir by one level and
# refuse to start ("Could not divine nwn base data directory"). cd into the
# real game directory before exec so CWD matches the baseline exactly,
# regardless of where the caller's shell happened to be.
cd "$GAME_DIR"
# ...but keep diagnostic output in THIS fork. The cd above changes the process
# CWD, which is where the injector's PGM dumps would otherwise land -- i.e. in
# the parent baseline directory, silently overwriting its artifacts.
export NWN_SHADOWMAP_OUT_DIR="${NWN_SHADOWMAP_OUT_DIR:-$HERE}"
LOG="${NWN_SHADOWMAP_LOG:-$HERE/shadowmap-phase1.log}"
# Keep this non-rendering add-on explicit when the dedicated cascade launcher
# delegates here.  `env` receives only explicitly supplied assignments below,
# so an inherited value cannot be lost among the many legacy flag removals.
CASCADE_MATH_ARGS=()
if [[ "${NWN_SHADOWMAP_CASCADE_MATH:-}" == "1" ]]; then
    CASCADE_MATH_ARGS+=(NWN_SHADOWMAP_CASCADE_MATH=1)
fi
LIGHT_VECTOR_ARGS=()
if [[ "${NWN_SHADOWMAP_LIGHT_VECTOR_TRACE:-}" == "1" ]]; then
    LIGHT_VECTOR_ARGS+=(NWN_SHADOWMAP_LIGHT_VECTOR_TRACE=1)
fi
LOCAL_LIGHT_TRACE_ARGS=()
if [[ "${NWN_SHADOWMAP_LOCAL_LIGHT_TRACE:-}" == "1" ]]; then
    LOCAL_LIGHT_TRACE_ARGS+=(NWN_SHADOWMAP_LOCAL_LIGHT_TRACE=1)
fi
CASCADE_TARGET_VALIDATE_ARGS=()
if [[ "${NWN_SHADOWMAP_CASCADE_TARGET_VALIDATE:-}" == "1" ]]; then
    CASCADE_TARGET_VALIDATE_ARGS+=(NWN_SHADOWMAP_CASCADE_TARGET_VALIDATE=1)
fi
CASCADE_GEOMETRY_TRACE_ARGS=()
if [[ "${NWN_SHADOWMAP_CASCADE_GEOMETRY_TRACE:-}" == "1" ]]; then
    CASCADE_GEOMETRY_TRACE_ARGS+=(NWN_SHADOWMAP_CASCADE_GEOMETRY_TRACE=1)
fi
CASTER_CULL_TRACE_ARGS=()
if [[ "${NWN_SHADOWMAP_CASTER_CULL_TRACE:-}" == "1" ]]; then
    CASTER_CULL_TRACE_ARGS+=(NWN_SHADOWMAP_CASTER_CULL_TRACE=1)
    if [[ -n "${NWN_SHADOWMAP_CASTER_CULL_TRACE_MAX:-}" ]]; then
        CASTER_CULL_TRACE_ARGS+=(NWN_SHADOWMAP_CASTER_CULL_TRACE_MAX="$NWN_SHADOWMAP_CASTER_CULL_TRACE_MAX")
    fi
fi
RELAX_AREA_VIEWPORT_ARGS=()
if [[ "${NWN_SHADOWMAP_TRACE_RELAX_AREA_VIEWPORT:-}" == "1" ]]; then
    RELAX_AREA_VIEWPORT_ARGS+=(NWN_SHADOWMAP_TRACE_RELAX_AREA_VIEWPORT=1)
fi
FULL_BSP_TRACE_ARGS=()
if [[ "${NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE:-}" == "1" ]]; then
    FULL_BSP_TRACE_ARGS+=(NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE=1)
    if [[ -n "${NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE_MAX:-}" ]]; then
        FULL_BSP_TRACE_ARGS+=(NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE_MAX="$NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE_MAX")
    fi
fi
FULL_BSP_NATIVE_SUBMIT_ARGS=()
if [[ "${NWN_SHADOWMAP_CASTER_FULL_BSP_NATIVE_SUBMIT:-}" == "1" ]]; then
    FULL_BSP_NATIVE_SUBMIT_ARGS+=(NWN_SHADOWMAP_CASTER_FULL_BSP_NATIVE_SUBMIT=1)
fi
CASCADE_MATRIX_TRACE_ARGS=()
if [[ "${NWN_SHADOWMAP_CASCADE_MATRIX_TRACE:-}" == "1" ]]; then
    CASCADE_MATRIX_TRACE_ARGS+=(NWN_SHADOWMAP_CASCADE_MATRIX_TRACE=1)
    if [[ -n "${NWN_SHADOWMAP_CASCADE_MATRIX_TRACE_MAX:-}" ]]; then
        CASCADE_MATRIX_TRACE_ARGS+=(NWN_SHADOWMAP_CASCADE_MATRIX_TRACE_MAX="$NWN_SHADOWMAP_CASCADE_MATRIX_TRACE_MAX")
    fi
fi
CASCADE_CAMERA_CAPTURE_ARGS=()
if [[ "${NWN_SHADOWMAP_CASCADE_CAMERA_CAPTURE:-}" == "1" ]]; then
    CASCADE_CAMERA_CAPTURE_ARGS+=(NWN_SHADOWMAP_CASCADE_CAMERA_CAPTURE=1)
    if [[ -n "${NWN_SHADOWMAP_CASCADE_CAMERA_BUCKET:-}" ]]; then
        CASCADE_CAMERA_CAPTURE_ARGS+=(NWN_SHADOWMAP_CASCADE_CAMERA_BUCKET="$NWN_SHADOWMAP_CASCADE_CAMERA_BUCKET")
    fi
fi
CASCADE_LIGHT_CAPTURE_ARGS=()
if [[ "${NWN_SHADOWMAP_CASCADE_LIGHT_CAPTURE:-}" == "1" ]]; then
    CASCADE_LIGHT_CAPTURE_ARGS+=(NWN_SHADOWMAP_CASCADE_LIGHT_CAPTURE=1)
    if [[ -n "${NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET:-}" ]]; then
        CASCADE_LIGHT_CAPTURE_ARGS+=(NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET="$NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET")
    fi
    if [[ -n "${NWN_SHADOWMAP_CASCADE_LIGHT_BUCKETS:-}" ]]; then
        CASCADE_LIGHT_CAPTURE_ARGS+=(NWN_SHADOWMAP_CASCADE_LIGHT_BUCKETS="$NWN_SHADOWMAP_CASCADE_LIGHT_BUCKETS")
    fi
    if [[ -n "${NWN_SHADOWMAP_CASCADE_LIGHT_LAYER:-}" ]]; then
        CASCADE_LIGHT_CAPTURE_ARGS+=(NWN_SHADOWMAP_CASCADE_LIGHT_LAYER="$NWN_SHADOWMAP_CASCADE_LIGHT_LAYER")
    fi
    if [[ "${NWN_SHADOWMAP_CASCADE_LIGHT_ALL_BUCKETS:-}" == "1" ]]; then
        CASCADE_LIGHT_CAPTURE_ARGS+=(NWN_SHADOWMAP_CASCADE_LIGHT_ALL_BUCKETS=1)
    fi
fi
STATIC_RECEIVER_ARGS=()
if [[ "${NWN_SHADOWMAP_STATIC_RECEIVER:-}" == "1" ]]; then
    STATIC_RECEIVER_ARGS+=(NWN_SHADOWMAP_STATIC_RECEIVER=1)
fi
CSM_STATIC_RECEIVER_ARGS=()
if [[ "${NWN_SHADOWMAP_CSM_STATIC_RECEIVER:-}" == "1" ]]; then
    CSM_STATIC_RECEIVER_ARGS+=(NWN_SHADOWMAP_CSM_STATIC_RECEIVER=1)
fi
CSM_DYNAMIC_RECEIVER_ARGS=()
if [[ "${NWN_SHADOWMAP_CSM_DYNAMIC_RECEIVER:-}" == "1" ]]; then
    CSM_DYNAMIC_RECEIVER_ARGS+=(NWN_SHADOWMAP_CSM_DYNAMIC_RECEIVER=1)
fi
CSM_ALPHA_RECEIVER_ARGS=()
if [[ "${NWN_SHADOWMAP_CSM_ALPHA_RECEIVER:-}" == "1" ]]; then
    CSM_ALPHA_RECEIVER_ARGS+=(NWN_SHADOWMAP_CSM_ALPHA_RECEIVER=1)
fi
CSM_COMPOSITE_ARGS=()
if [[ "${NWN_SHADOWMAP_CSM_COMPOSITE:-}" == "1" ]]; then
    CSM_COMPOSITE_ARGS+=(NWN_SHADOWMAP_CSM_COMPOSITE=1)
    if [[ -n "${NWN_SHADOWMAP_CSM_STRENGTH:-}" ]]; then
        CSM_COMPOSITE_ARGS+=(NWN_SHADOWMAP_CSM_STRENGTH="$NWN_SHADOWMAP_CSM_STRENGTH")
    fi
fi
CSM_BIAS_ARGS=()
if [[ -n "${NWN_SHADOWMAP_CSM_BIAS:-}" ]]; then
    CSM_BIAS_ARGS+=(NWN_SHADOWMAP_CSM_BIAS="$NWN_SHADOWMAP_CSM_BIAS")
fi
CSM_BLEND_ARGS=()
if [[ -n "${NWN_SHADOWMAP_CSM_BLEND:-}" ]]; then
    CSM_BLEND_ARGS+=(NWN_SHADOWMAP_CSM_BLEND="$NWN_SHADOWMAP_CSM_BLEND")
fi
CSM_PCF_ARGS=()
if [[ -n "${NWN_SHADOWMAP_CSM_PCF_RADIUS:-}" ]]; then
    CSM_PCF_ARGS+=(NWN_SHADOWMAP_CSM_PCF_RADIUS="$NWN_SHADOWMAP_CSM_PCF_RADIUS")
fi
DYNAMIC_RECEIVER_ARGS=()
if [[ "${NWN_SHADOWMAP_DYNAMIC_RECEIVER:-}" == "1" ]]; then
    DYNAMIC_RECEIVER_ARGS+=(NWN_SHADOWMAP_DYNAMIC_RECEIVER=1)
    if [[ -n "${NWN_SHADOWMAP_DYNAMIC_BUCKET:-}" ]]; then
        DYNAMIC_RECEIVER_ARGS+=(NWN_SHADOWMAP_DYNAMIC_BUCKET="$NWN_SHADOWMAP_DYNAMIC_BUCKET")
    fi
fi
ALPHA_RECEIVER_ARGS=()
if [[ "${NWN_SHADOWMAP_ALPHA_RECEIVER:-}" == "1" ]]; then
    ALPHA_RECEIVER_ARGS+=(NWN_SHADOWMAP_ALPHA_RECEIVER=1)
    if [[ -n "${NWN_SHADOWMAP_ALPHA_BUCKET:-}" ]]; then
        ALPHA_RECEIVER_ARGS+=(NWN_SHADOWMAP_ALPHA_BUCKET="$NWN_SHADOWMAP_ALPHA_BUCKET")
    fi
fi
STATIC_ALPHA_CAPTURE_ARGS=()
if [[ "${NWN_SHADOWMAP_STATIC_ALPHA_CAPTURE:-}" == "1" ]]; then
    STATIC_ALPHA_CAPTURE_ARGS+=(NWN_SHADOWMAP_STATIC_ALPHA_CAPTURE=1)
    if [[ -n "${NWN_SHADOWMAP_STATIC_ALPHA_BUCKET:-}" ]]; then
        STATIC_ALPHA_CAPTURE_ARGS+=(NWN_SHADOWMAP_STATIC_ALPHA_BUCKET="$NWN_SHADOWMAP_STATIC_ALPHA_BUCKET")
    fi
fi
STATIC_ALPHA_RECEIVER_ARGS=()
if [[ "${NWN_SHADOWMAP_STATIC_ALPHA_RECEIVER:-}" == "1" ]]; then
    STATIC_ALPHA_RECEIVER_ARGS+=(NWN_SHADOWMAP_STATIC_ALPHA_RECEIVER=1)
    if [[ -n "${NWN_SHADOWMAP_STATIC_ALPHA_BUCKET:-}" ]]; then
        STATIC_ALPHA_RECEIVER_ARGS+=(NWN_SHADOWMAP_STATIC_ALPHA_BUCKET="$NWN_SHADOWMAP_STATIC_ALPHA_BUCKET")
    fi
fi
MULTI_LAYER_CAPTURE_ARGS=()
if [[ "${NWN_SHADOWMAP_CASCADE_MULTI_CAPTURE:-}" == "1" ]]; then
    MULTI_LAYER_CAPTURE_ARGS+=(NWN_SHADOWMAP_CASCADE_MULTI_CAPTURE=1)
fi
DYNAMIC_CHARACTER_CAPTURE_ARGS=()
if [[ "${NWN_SHADOWMAP_DYNAMIC_CHARACTER_CAPTURE:-}" == "1" ]]; then
    DYNAMIC_CHARACTER_CAPTURE_ARGS+=(NWN_SHADOWMAP_DYNAMIC_CHARACTER_CAPTURE=1)
fi
DYNAMIC_BUCKET_CAPTURE_ARGS=()
if [[ "${NWN_SHADOWMAP_DYNAMIC_BUCKET_CAPTURE:-}" == "1" ]]; then
    DYNAMIC_BUCKET_CAPTURE_ARGS+=(NWN_SHADOWMAP_DYNAMIC_BUCKET_CAPTURE=1)
    if [[ -n "${NWN_SHADOWMAP_DYNAMIC_BUCKET:-}" ]]; then
        DYNAMIC_BUCKET_CAPTURE_ARGS+=(NWN_SHADOWMAP_DYNAMIC_BUCKET="$NWN_SHADOWMAP_DYNAMIC_BUCKET")
    fi
fi
ALPHA_CARD_CAPTURE_ARGS=()
if [[ "${NWN_SHADOWMAP_ALPHA_CARD_CAPTURE:-}" == "1" ]]; then
    ALPHA_CARD_CAPTURE_ARGS+=(NWN_SHADOWMAP_ALPHA_CARD_CAPTURE=1)
fi

if [[ ! -x "$HERE/run-nwn.sh" ]]; then
    echo "Missing launcher: $HERE/run-nwn.sh" >&2
    exit 1
fi
if [[ ! -f "$HERE/libnwn_shadowmap.so" ]]; then
    echo "Build the injector first: cd \"$HERE\" && make" >&2
    exit 1
fi

: > "$LOG"
echo "[shadowmap] Phase 1 logging to $LOG" | tee -a "$LOG"
if [[ ${#CASCADE_MATH_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3a cascade-math logging enabled (no rendering changes)" | tee -a "$LOG"
fi
if [[ ${#LIGHT_VECTOR_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3b light-vector logging enabled (no rendering changes)" | tee -a "$LOG"
fi
if [[ ${#LOCAL_LIGHT_TRACE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] local-light candidate census enabled (read-only; no rendering changes)" | tee -a "$LOG"
fi
if [[ ${#CASCADE_TARGET_VALIDATE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3c cascade-target validation enabled (allocation/completeness only)" | tee -a "$LOG"
fi
if [[ ${#CASCADE_GEOMETRY_TRACE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3e in-sequence normal-geometry census enabled (no draw changes)" | tee -a "$LOG"
fi
if [[ ${#CASTER_CULL_TRACE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 4b post-BSP caster culling census enabled (read-only)" | tee -a "$LOG"
fi
if [[ ${#FULL_BSP_TRACE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 4c full-BSP caster candidate census enabled (read-only)" | tee -a "$LOG"
fi
if [[ ${#FULL_BSP_NATIVE_SUBMIT_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 4d native full-BSP static submit enabled (private red diagnostic)" | tee -a "$LOG"
fi
if [[ ${#RELAX_AREA_VIEWPORT_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] relaxed world-viewport selection enabled (diagnostic launcher only)" | tee -a "$LOG"
fi
if [[ ${#CASCADE_MATRIX_TRACE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3g normal matrix-uniform trace enabled (no draw changes)" | tee -a "$LOG"
fi
if [[ ${#CASCADE_CAMERA_CAPTURE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3f native camera-depth capture enabled (one private PGM; no visible change)" | tee -a "$LOG"
fi
if [[ ${#CASCADE_LIGHT_CAPTURE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3h native light-space depth capture enabled (one private PGM; no visible change)" | tee -a "$LOG"
fi
if [[ ${#STATIC_RECEIVER_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3i static-only fullscreen receiver enabled (visible red diagnostic)" | tee -a "$LOG"
fi
if [[ ${#DYNAMIC_RECEIVER_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3l static+dynamic fullscreen receiver enabled (visible red diagnostic)" | tee -a "$LOG"
fi
if [[ ${#ALPHA_RECEIVER_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3p static+dynamic+validated-alpha receiver enabled (visible red diagnostic)" | tee -a "$LOG"
fi
if [[ ${#STATIC_ALPHA_CAPTURE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3q source-classified static alpha capture enabled (private PGM only)" | tee -a "$LOG"
fi
if [[ ${#STATIC_ALPHA_RECEIVER_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3r source-classified static alpha receiver enabled (visible red diagnostic)" | tee -a "$LOG"
fi
if [[ ${#MULTI_LAYER_CAPTURE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 4a four-layer depth capture enabled (no visible receiver)" | tee -a "$LOG"
fi
if [[ ${#DYNAMIC_CHARACTER_CAPTURE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3j dynamic-character capture enabled (private PGM; no visible change)" | tee -a "$LOG"
fi
if [[ ${#DYNAMIC_BUCKET_CAPTURE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3k post-dynamic bucket capture enabled (private PGM; no visible change)" | tee -a "$LOG"
fi
if [[ ${#ALPHA_CARD_CAPTURE_ARGS[@]} -ne 0 ]]; then
    echo "[shadowmap] Phase 3o enlarged alpha-card depth crop enabled (private PGM only)" | tee -a "$LOG"
fi

env -u NWN_SHADOWMAP_LIGHT \
    -u NWN_SHADOWMAP_INJECT_TEST \
    -u NWN_SHADOWMAP_COLORCAST_TEST \
    -u NWN_SHADOWMAP_FULLSCREEN_RECEIVER \
    -u NWN_SHADOWMAP_CASCADE_TARGETS \
    -u NWN_SHADOWMAP_CASCADE_TARGET_VALIDATE \
    -u NWN_SHADOWMAP_CASCADE_GEOMETRY_TRACE \
    -u NWN_SHADOWMAP_CASTER_CULL_TRACE \
    -u NWN_SHADOWMAP_CASTER_CULL_TRACE_MAX \
    -u NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE \
    -u NWN_SHADOWMAP_CASTER_FULL_BSP_TRACE_MAX \
    -u NWN_SHADOWMAP_CASTER_FULL_BSP_NATIVE_SUBMIT \
    -u NWN_SHADOWMAP_TRACE_RELAX_AREA_VIEWPORT \
    -u NWN_SHADOWMAP_CASCADE_MATRIX_TRACE \
    -u NWN_SHADOWMAP_CASCADE_MATRIX_TRACE_MAX \
    -u NWN_SHADOWMAP_CASCADE_CAMERA_CAPTURE \
    -u NWN_SHADOWMAP_CASCADE_CAMERA_BUCKET \
    -u NWN_SHADOWMAP_CASCADE_LIGHT_CAPTURE \
    -u NWN_SHADOWMAP_CASCADE_LIGHT_BUCKET \
    -u NWN_SHADOWMAP_CASCADE_LIGHT_BUCKETS \
    -u NWN_SHADOWMAP_CASCADE_LIGHT_LAYER \
    -u NWN_SHADOWMAP_CASCADE_LIGHT_ALL_BUCKETS \
    -u NWN_SHADOWMAP_STATIC_RECEIVER \
    -u NWN_SHADOWMAP_CSM_STATIC_RECEIVER \
    -u NWN_SHADOWMAP_CSM_DYNAMIC_RECEIVER \
    -u NWN_SHADOWMAP_CSM_ALPHA_RECEIVER \
    -u NWN_SHADOWMAP_CSM_COMPOSITE \
    -u NWN_SHADOWMAP_CSM_STRENGTH \
    -u NWN_SHADOWMAP_CSM_BIAS \
    -u NWN_SHADOWMAP_CSM_BLEND \
    -u NWN_SHADOWMAP_CSM_PCF_RADIUS \
    -u NWN_SHADOWMAP_DYNAMIC_RECEIVER \
    -u NWN_SHADOWMAP_ALPHA_RECEIVER \
    -u NWN_SHADOWMAP_ALPHA_BUCKET \
    -u NWN_SHADOWMAP_STATIC_ALPHA_CAPTURE \
    -u NWN_SHADOWMAP_STATIC_ALPHA_RECEIVER \
    -u NWN_SHADOWMAP_CASCADE_MULTI_CAPTURE \
    -u NWN_SHADOWMAP_STATIC_ALPHA_BUCKET \
    -u NWN_SHADOWMAP_DYNAMIC_CHARACTER_CAPTURE \
    -u NWN_SHADOWMAP_DYNAMIC_BUCKET_CAPTURE \
    -u NWN_SHADOWMAP_DYNAMIC_BUCKET \
    -u NWN_SHADOWMAP_ALPHA_CARD_CAPTURE \
    -u NWN_SHADOWMAP_CASCADE_COPY \
    -u NWN_SHADOWMAP_CASCADE_VERIFY \
    -u NWN_SHADOWMAP_GPU_TEST \
    -u NWN_SHADOWMAP_CPU_BRIDGE \
    -u NWN_SHADOWMAP_GPU_STEP_BRIDGE \
    -u NWN_SHADOWMAP_VERTEX_REPROJECT \
    -u NWN_SHADOWMAP_LIGHT_CASTER_MATRICES \
    -u NWN_SHADOWMAP_LOCAL_LIGHT_TRACE \
    -u NWN_SHADOWMAP_UNIFORM_TRACE \
    -u NWN_SHADOWMAP_RECEIVER_TRACE \
    -u NWN_SHADOWMAP_STENCIL_TRACE \
    -u NWN_SHADOWMAP_PROBE \
    -u NWN_SHADOWMAP_DUMP \
    "${CASCADE_MATH_ARGS[@]}" \
    "${LIGHT_VECTOR_ARGS[@]}" \
    "${LOCAL_LIGHT_TRACE_ARGS[@]}" \
    "${CASCADE_TARGET_VALIDATE_ARGS[@]}" \
    "${CASCADE_GEOMETRY_TRACE_ARGS[@]}" \
    "${CASTER_CULL_TRACE_ARGS[@]}" \
    "${FULL_BSP_TRACE_ARGS[@]}" \
    "${FULL_BSP_NATIVE_SUBMIT_ARGS[@]}" \
    "${RELAX_AREA_VIEWPORT_ARGS[@]}" \
    "${CASCADE_MATRIX_TRACE_ARGS[@]}" \
    "${CASCADE_CAMERA_CAPTURE_ARGS[@]}" \
    "${CASCADE_LIGHT_CAPTURE_ARGS[@]}" \
    "${STATIC_RECEIVER_ARGS[@]}" \
    "${CSM_STATIC_RECEIVER_ARGS[@]}" \
    "${CSM_DYNAMIC_RECEIVER_ARGS[@]}" \
    "${CSM_ALPHA_RECEIVER_ARGS[@]}" \
    "${CSM_COMPOSITE_ARGS[@]}" \
    "${CSM_BIAS_ARGS[@]}" \
    "${CSM_BLEND_ARGS[@]}" \
    "${CSM_PCF_ARGS[@]}" \
    "${DYNAMIC_RECEIVER_ARGS[@]}" \
    "${ALPHA_RECEIVER_ARGS[@]}" \
    "${STATIC_ALPHA_CAPTURE_ARGS[@]}" \
    "${STATIC_ALPHA_RECEIVER_ARGS[@]}" \
    "${MULTI_LAYER_CAPTURE_ARGS[@]}" \
    "${DYNAMIC_CHARACTER_CAPTURE_ARGS[@]}" \
    "${DYNAMIC_BUCKET_CAPTURE_ARGS[@]}" \
    "${ALPHA_CARD_CAPTURE_ARGS[@]}" \
    ${NWN_SHADOWMAP_DUMP_PGM:+NWN_SHADOWMAP_DUMP_PGM="$NWN_SHADOWMAP_DUMP_PGM"} \
    NWN_SHADOWMAP_TRACE=1 \
    NWN_SHADOWMAP_TRACE_FRAMES="${NWN_SHADOWMAP_TRACE_FRAMES:-90}" \
    NWN_SHADOWMAP_TRACE_EVENTS="${NWN_SHADOWMAP_TRACE_EVENTS:-4096}" \
    "$HERE/run-nwn.sh" "$GAME_DIR/nwmain-linux" "$@" 2>&1 | tee -a "$LOG"

exit "${PIPESTATUS[0]}"
