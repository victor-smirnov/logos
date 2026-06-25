#!/usr/bin/env bash
# build.sh <file.logos> — compile a Logos source with -g and link an executable
# (same recipe as tests/logos/run_test.sh), for VSCode debugging.
set -euo pipefail

SRC="$(realpath "${1:?usage: build.sh <file.logos>}")"   # abs path → DWARF carries it
OUT="${SRC%.logos}"

# Locate logosc: $LOGOSC, then PATH, then the dev build tree.
LOGOSC="${LOGOSC:-logosc}"
command -v "$LOGOSC" >/dev/null 2>&1 || LOGOSC="$HOME/devel/logos/build/bin/logosc"

"$LOGOSC" "$SRC" -g -o "$OUT.o"

# stdlib archives to link against (the dir logosc itself resolves).
LIBDIR="$("$LOGOSC" --print-lib-dir 2>/dev/null || true)"
[ -d "$LIBDIR" ] || LIBDIR="$HOME/devel/logos/build/lib/logos"

ARCHIVES=()
for a in "$LIBDIR"/liblstdlib*.a "$LIBDIR"/liblogos-*.a "$LIBDIR"/*.a; do
    case "$(basename "$a")" in
        liblstdlib*|liblogos-*) ;;                 # already taken in priority order
        *) [ -f "$a" ] && ARCHIVES+=("$a") ;;
    esac
done
# prepend the priority archives
PRI=()
for a in "$LIBDIR"/liblstdlib*.a "$LIBDIR"/liblogos-*.a; do [ -f "$a" ] && PRI+=("$a"); done
ARCHIVES=("${PRI[@]}" "${ARCHIVES[@]}")

cc "$OUT.o" -g -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group \
   -lpthread -lm -lstdc++ -Wl,--allow-multiple-definition -o "$OUT"
echo "built $OUT"
