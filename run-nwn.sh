#!/usr/bin/env bash
# Launch NWN:EE with the shadow-map hook injected.
#
#   ./run-nwn.sh /path/to/nwmain-linux [game args...]
#
# The hook prints "[shadowmap] active." to stderr when it installs. Run from a
# terminal the first time so you can see that line and the target status.
#
# Knobs (see nwn_shadowmap.cpp):
#   NWN_SHADOWMAP_OFF=1        load but install nothing (A/B without rebuilding)
#   NWN_SHADOWMAP_SIZE=2048    depth target resolution
#   NWN_SHADOWMAP_VERBOSE=1    log every frame
#   NWN_SHADOWMAP_DUMP=N       after N frames write shadowmap_dump.pgm

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="libnwn_shadowmap.so"
HOOK="$HERE/$LIB"

if [[ ! -f "$HOOK" ]]; then
  echo "Build it first:  make" >&2
  exit 1
fi

# ld.so splits LD_PRELOAD on whitespace, so a hook path containing spaces
# (e.g. HERE living under a dir like "a directory with a space in its name") breaks preloading. Work
# around it by preloading a symlink from a space-free location instead.
if [[ "$HOOK" == *' '* ]]; then
  SAFE_DIR="${XDG_RUNTIME_DIR:-/tmp}"
  ln -sf "$HOOK" "$SAFE_DIR/$LIB"
  HOOK="$SAFE_DIR/$LIB"
fi

GAME="${1:?usage: run-nwn.sh /path/to/nwmain-linux [args...]}"
shift || true

# Prepend so we win symbol/hook order; keep any existing LD_PRELOAD.
export LD_PRELOAD="$HOOK${LD_PRELOAD:+:$LD_PRELOAD}"

exec "$GAME" "$@"
