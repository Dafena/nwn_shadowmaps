#!/usr/bin/env bash
# Run the real cascade replay against a recording GL backend, without NWN.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
make -s
TEST_DIR="$(mktemp -d /tmp/nwn-caster-toggle.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-g++}" -O1 -std=c++17 -fPIC -I. -Isubhook \
    tests/caster_toggle_regression.cpp \
    shadow_config.o shadow_math.o nwn_oit.o nwn_overlay_imgui.o subhook.o \
    imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_opengl3.o \
    -ldl -o "$TEST_DIR/caster-toggle"
NWN_SHADOWMAP_OFF=1 "$TEST_DIR/caster-toggle"
