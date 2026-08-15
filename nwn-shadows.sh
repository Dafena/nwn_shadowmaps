#!/usr/bin/env bash
# Launch NWN:EE with the cascaded shadow injector.
#
# Put this next to nwmain-linux, together with libnwn_shadowmap_deploy.so, and
# run it instead of the game. Or use it from Steam -- see the bottom of this file.
#
# WHY THIS SCRIPT EXISTS AT ALL, rather than just telling you to set LD_PRELOAD:
# ld.so splits LD_PRELOAD on SPACES as well as colons, and the default install
# path contains one --
#
#     .../steamapps/common/Neverwinter Nights/bin/linux-x86/
#                          ^^^^^^^^^^^^^^^^^^
#
# so a plain `LD_PRELOAD=/that/path/lib.so` silently loads nothing and you get
# no shadows and no error. This script preloads through a symlink in a
# space-free directory instead, which is the only reliable fix.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="libnwn_shadowmap_deploy.so"
HOOK="$HERE/$LIB"

if [[ ! -f "$HOOK" ]]; then
    echo "error: $LIB is not next to this script." >&2
    echo "       Both files belong in the folder that holds nwmain-linux," >&2
    echo "       normally <NWN install>/bin/linux-x86/" >&2
    exit 1
fi

# See the header. $XDG_RUNTIME_DIR is per-user and never has spaces; /tmp is the
# fallback. -f so a stale link from an older build is replaced.
if [[ "$HOOK" == *' '* ]]; then
    SAFE_DIR="${XDG_RUNTIME_DIR:-/tmp}"
    ln -sf "$HOOK" "$SAFE_DIR/$LIB"
    HOOK="$SAFE_DIR/$LIB"
fi

# Prepend, keeping anything the user already had.
export LD_PRELOAD="$HOOK${LD_PRELOAD:+:$LD_PRELOAD}"

# nwmain-linux works out its own data directory partly from the process's
# working directory, and refuses to start ("Could not divine nwn base data
# directory") if that is wrong. cd first so it matches a normal launch exactly.
cd "$HERE"

if [[ $# -gt 0 ]]; then
    # Steam passes the real command through %command%; run that.
    exec "$@"
else
    if [[ ! -x ./nwmain-linux ]]; then
        echo "error: nwmain-linux is not in $HERE" >&2
        echo "       This script belongs in <NWN install>/bin/linux-x86/" >&2
        exit 1
    fi
    exec ./nwmain-linux
fi

# ---------------------------------------------------------------------------
# Steam:  Properties -> Launch Options
#
#     "/full/path/to/nwn-shadows.sh" %command%
#
# Keep the quotes -- the path very likely contains a space, for the same reason
# this script exists.
# ---------------------------------------------------------------------------
