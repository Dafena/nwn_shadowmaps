#!/usr/bin/env bash
# Fog composition, authored-distance, depth reconstruction and light transitions.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
export PYTHONDONTWRITEBYTECODE=1
python3 tests/fog_energy_regression.py
python3 tests/fog_depth_reconstruction_regression.py
python3 tests/fog_opaque_distance_regression.py
TEST_DIR="$(mktemp -d /tmp/nwn-fog-regression.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -I. tests/fog_lamp_fade_regression.cpp -o "$TEST_DIR/fade"
"$TEST_DIR/fade"
