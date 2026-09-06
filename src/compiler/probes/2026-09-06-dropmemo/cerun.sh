#!/usr/bin/env bash
# Compile + link + run one .logos exactly the way tests/logos/run_test.sh does,
# and print `cc=<rc> rc=<rc> out=[...]`. Used to give the counter-examples a
# verdict on TWO binaries in one loop.
set -u
LOGOSC="$1"; SRC="$2"; D="$3"; shift 3
export LOGOS_LIB_DIR="${LOGOS_LIB_DIR:-$PWD/build/lib/logos}"
n=$(basename "$SRC" .logos)
mkdir -p "$D"
timeout 600 "$LOGOSC" "$SRC" -o "$D/$n.o" > "$D/$n.cc" 2>&1; cc=$?
rc=NA; out=""
if [ $cc -eq 0 ]; then
    A=()
    for a in "$LOGOS_LIB_DIR"/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done
    for a in "$LOGOS_LIB_DIR"/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
    for a in "$LOGOS_LIB_DIR"/*.a; do
        case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac
    done
    if cc "$D/$n.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
          -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$D/$n.exe" 2>"$D/$n.link"; then
        out=$(timeout 120 "$D/$n.exe" 2>/dev/null); rc=$?
    else
        rc=LINKFAIL
    fi
fi
printf '%s cc=%s rc=%s out=[%s]\n' "$n" "$cc" "$rc" "$out"
