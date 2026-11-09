#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
make -s
TEST_DIR="$(mktemp -d /tmp/nwn-local-depth.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-g++}" -O1 -std=c++17 -fPIC -I. -Isubhook \
    tests/local_contact_depth_regression.cpp \
    shadow_config.o shadow_math.o nwn_oit.o nwn_overlay_imgui.o subhook.o \
    imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_opengl3.o \
    -ldl -o "$TEST_DIR/local-depth"
NWN_SHADOWMAP_OFF=1 NWN_SHADOWMAP_DEPTH_BLIT=1 "$TEST_DIR/local-depth"
NWN_SHADOWMAP_OFF=1 NWN_SHADOWMAP_DEPTH_BLIT=0 "$TEST_DIR/local-depth"
