#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
make -s subhook.o
case_dir=$(mktemp -d /tmp/nwn-native-gateway.XXXXXX)
trap 'rm -rf "$case_dir"' EXIT
"${CXX:-g++}" -O2 -std=c++17 -I. -Isubhook tests/native_gateway_regression.cpp tests/native_gateway_fixture.S subhook.o -o "$case_dir/check"
"$case_dir/check"
