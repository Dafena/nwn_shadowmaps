#!/usr/bin/env bash
# Verify the Windows proxy artifact and, optionally, its symbol map against the
# exact nwmain.exe that will load it.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DLL="${1:-$HERE/version.dll}"
EXE="${2:-}"
OBJDUMP="${OBJDUMP:-x86_64-w64-mingw32-objdump}"

if [[ ! -f "$DLL" ]]; then
    echo "missing DLL: $DLL" >&2
    exit 1
fi
if ! file "$DLL" | grep -q 'PE32+.*x86-64'; then
    echo "not an x86-64 PE DLL: $DLL" >&2
    exit 1
fi

DLL_DUMP="$(mktemp)"
EXE_DUMP=""
cleanup() {
    rm -f "$DLL_DUMP"
    [[ -z "$EXE_DUMP" ]] || rm -f "$EXE_DUMP"
}
trap cleanup EXIT
"$OBJDUMP" -p "$DLL" > "$DLL_DUMP"

EXPECTED_EXPORTS=(
    GetFileVersionInfoA GetFileVersionInfoByHandle GetFileVersionInfoExA
    GetFileVersionInfoExW GetFileVersionInfoSizeA GetFileVersionInfoSizeExA
    GetFileVersionInfoSizeExW GetFileVersionInfoSizeW GetFileVersionInfoW
    VerFindFileA VerFindFileW VerInstallFileA VerInstallFileW
    VerLanguageNameA VerLanguageNameW VerQueryValueA VerQueryValueW
)

for name in "${EXPECTED_EXPORTS[@]}"; do
    if ! grep -Eq "[[:space:]]${name}$" "$DLL_DUMP"; then
        echo "missing proxy export: $name" >&2
        exit 1
    fi
done

echo "proxy exports: ${#EXPECTED_EXPORTS[@]}/${#EXPECTED_EXPORTS[@]}"
sha256sum "$DLL"

if [[ -z "$EXE" ]]; then
    echo "nwmain symbol map: skipped (pass nwmain.exe as argument 2)"
    exit 0
fi
if [[ ! -f "$EXE" ]]; then
    echo "missing executable: $EXE" >&2
    exit 1
fi
if ! file "$EXE" | grep -q 'PE32+.*x86-64'; then
    echo "not an x86-64 PE executable: $EXE" >&2
    exit 1
fi

EXE_DUMP="$(mktemp)"
"$OBJDUMP" -p "$EXE" > "$EXE_DUMP"
mapped=0
while IFS= read -r symbol; do
    [[ -n "$symbol" ]] || continue
    mapped=$((mapped + 1))
    if ! grep -Fq "$symbol" "$EXE_DUMP"; then
        echo "missing mapped NWN export: $symbol" >&2
        exit 1
    fi
done < <(sed -n \
    -e 's/^[[:space:]]*{"[^"]*",[[:space:]]*"\([^"]*\)".*/\1/p' \
    -e 's/^[[:space:]]*"\([?][^"]*\)",[[:space:]]*false}.*/\1/p' \
    "$HERE/nwn_win_symbols.h")

echo "mapped NWN exports: $mapped/$mapped"
strings -el "$EXE" | grep -A1 -m1 '^FileVersion$' || true
sha256sum "$EXE"
