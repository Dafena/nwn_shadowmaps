#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
make -s
TEST_DIR="$(mktemp -d /tmp/nwn-puddle-trace.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-g++}" -O1 -std=c++17 -fPIC -I. -Isrc -Isubhook tests/puddle_trace_quality_regression.cpp \
 shadow_config.o shadow_math.o nwn_oit.o nwn_overlay_imgui.o drawing_texture_probe.o subhook.o \
 imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_opengl3.o \
 -ldl -o "$TEST_DIR/trace"
NWN_SHADOWMAP_OFF=1 "$TEST_DIR/trace"

NWN_SHADOWMAP_OFF=1 "$TEST_DIR/trace" --settings-disabled

# Compile the production injector's equivalent shelter candidate, not a retyped shader.
NWN_SHADOWMAP_OFF=1 "$TEST_DIR/trace" --dump-weather "$TEST_DIR/candidate-base-puddles-off.frag"
cp "$TEST_DIR/candidate-base-puddles-off.frag" "$TEST_DIR/candidate-fast-puddles-off.frag"
"${CXX:-g++}" -O2 -std=c++17 tests/weather_surface_gpu.cpp -lEGL -lGL -o "$TEST_DIR/gpu"
LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless "$TEST_DIR/gpu" \
 "$TEST_DIR/candidate-base-puddles-off.frag" "$TEST_DIR/candidate-fast-puddles-off.frag"
