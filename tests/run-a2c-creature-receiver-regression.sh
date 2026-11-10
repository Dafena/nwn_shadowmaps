#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
make -s
TEST_DIR="$(mktemp -d /tmp/nwn-a2c-receiver.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-g++}" -O1 -std=c++17 -fPIC -I. -Isubhook \
    tests/a2c_creature_receiver_regression.cpp \
    shadow_config.o shadow_math.o nwn_oit.o nwn_overlay_imgui.o drawing_texture_probe.o subhook.o \
    imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_opengl3.o \
    -ldl -o "$TEST_DIR/a2c-receiver"
NWN_SHADOWMAP_OFF=1 "$TEST_DIR/a2c-receiver"
