#!/bin/bash
# handrun.sh <logosc> <outfile> — compile+link+RUN every hand program, one line each.
R=/home/logos/devel/logos
LOGOSC="$1"; OUT="$2"
export LOGOS_LIB_DIR=$R/build/lib/logos
A=()
for a in "$LOGOS_LIB_DIR"/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LOGOS_LIB_DIR"/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LOGOS_LIB_DIR"/*.a; do case "$(basename $a)" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done
: > "$OUT"
D=$(mktemp -d)
for f in "$R"/src/compiler/probes/2026-09-10c-vecpkg/hand/*.logos; do
    b=$(basename "$f" .logos)
    err=$("$LOGOSC" "$f" -o "$D/$b.o" 2>&1); cc=$?
    if [ $cc -ne 0 ]; then
        line=$(printf '%s\n' "$err" | grep -m1 'error' | sed 's/^.*error [^:]*: //' | cut -c1-110)
        printf '%s\tCOMPILE=%d\t-\t%s\n' "$b" "$cc" "$line" >> "$OUT"; continue
    fi
    if ! cc "$D/$b.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
         -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$D/$b.bin" 2>/dev/null; then
        printf '%s\tCOMPILE=0\tLINKFAIL\t-\n' "$b" >> "$OUT"; continue
    fi
    so=$("$D/$b.bin" 2>/dev/null); rc=$?
    printf '%s\tCOMPILE=0\trc=%d\tstdout=[%s]\n' "$b" "$rc" "$so" >> "$OUT"
done
rm -rf "$D"
