#!/usr/bin/env bash
# Render the real wet-surface branch in GL; no game or installed resource changes.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
TEST_DIR="$(mktemp -d /tmp/nwn-weather-surface.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT
python3 tests/extract_weather_surface.py weather_runtime.inc "$TEST_DIR/surface.frag"
"${CXX:-g++}" -O2 -std=c++17 tests/weather_surface_gpu.cpp -lEGL -lGL -o "$TEST_DIR/gpu"
for shader in surface degenerate gradient gradient-degenerate trace10 trace5 trace3 trace-default surface-puddles-off optics-on optics-puddles-off surface-film-off-puddles-off surface-film-off film-fields-puddles-off film-fields-film-off-puddles-off; do
    LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless "$TEST_DIR/gpu" "$TEST_DIR/$shader.frag"
done

for scale in 0.5 1.0; do
    LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless "$TEST_DIR/gpu" \
        "$TEST_DIR/ripple-reference-$scale.frag" "$TEST_DIR/ripple-bounded-$scale.frag"
done

for phase in 0.0 1.3 11.7 4095.0; do
    LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless "$TEST_DIR/gpu" \
        "$TEST_DIR/ripple-film-reference-$phase.frag" "$TEST_DIR/ripple-film-compact-$phase.frag"
done

for mode in 0 1 2 3 4 5; do
    suffix=""
    if [[ "$mode" == 5 ]]; then suffix="-film-off"; fi
    LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless "$TEST_DIR/gpu" "$TEST_DIR/profile-$mode$suffix-puddles-off.frag"
done
