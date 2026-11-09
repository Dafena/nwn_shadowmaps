#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
test_binary=$(mktemp /tmp/nwn-layer-stack-test.XXXXXX)
trap 'rm -f "$test_binary"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -I"$repo" "$repo/tests/layer_stack_regression.cpp" -o "$test_binary"
"$test_binary"
