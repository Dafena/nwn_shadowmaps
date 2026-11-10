#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
make -s
TEST_DIR="$(mktemp -d /tmp/nwn-sun-filter.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-g++}" -O1 -std=c++17 -fPIC -I. -Isubhook \
    tests/sun_creature_filter_regression.cpp \
    shadow_config.o shadow_math.o nwn_oit.o nwn_overlay_imgui.o drawing_texture_probe.o subhook.o \
    imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_opengl3.o \
    -ldl -o "$TEST_DIR/sun-filter"
NWN_SHADOWMAP_OFF=1 "$TEST_DIR/sun-filter" "$TEST_DIR/tag.frag"
"${CXX:-g++}" -O1 -std=c++17 -fPIC -I. -Isubhook \
    tests/sun_receiver_tag_state_regression.cpp \
    shadow_config.o shadow_math.o nwn_oit.o nwn_overlay_imgui.o drawing_texture_probe.o subhook.o \
    imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_opengl3.o \
    -ldl -o "$TEST_DIR/tag-state"
NWN_SHADOWMAP_OFF=1 "$TEST_DIR/tag-state"
glslangValidator -S frag "$TEST_DIR/tag.frag"

PYTHONDONTWRITEBYTECODE=1 python3 - "$TEST_DIR/receiver.frag" <<'PYSHADER'
import sys
import check_shaders
path='shadow_fullscreen_receiver.inc'
lines=open(path).read().split('\n')
shaders=[check_shaders.grab(lines,idx) for name,idx in check_shaders.find_shaders(path) if name=='fs']
open(sys.argv[1],'w').write(max(shaders,key=len))
PYSHADER
"${CXX:-g++}" -O1 -std=c++17 tests/sun_filter_gpu.cpp -lEGL -lGL -o "$TEST_DIR/gpu"
LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless "$TEST_DIR/gpu" "$TEST_DIR/receiver.frag" "$TEST_DIR/tag.frag"
