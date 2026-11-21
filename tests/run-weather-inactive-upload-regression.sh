#!/usr/bin/env bash
# CPU-only inactive weather upload model: no EGL/GL context and no hardware GPU workload.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
make -s tag-bench
TEST_DIR="$(mktemp -d /tmp/nwn-weather-inactive.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-g++}" -O1 -std=c++17 -fPIC -DNWN_TAG_WORK_BENCH_DIAGNOSTIC -I. -Isubhook \
 tests/weather_inactive_upload_regression.cpp shadow_config.o shadow_math.o build-tag-bench/nwn_oit.o \
 nwn_overlay_imgui.o drawing_texture_probe.o subhook.o imgui.o imgui_draw.o imgui_tables.o \
 imgui_widgets.o imgui_impl_opengl3.o -ldl -o "$TEST_DIR/lease"
NWN_SHADOWMAP_OFF=1 "$TEST_DIR/lease"
