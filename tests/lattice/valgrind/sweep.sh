#!/usr/bin/env bash
# sweep.sh [OUTDIR] — the whole pass corpus, compiled + linked natively and RUN
# under valgrind. Re-baselines in one command.
#
#   bash tests/lattice/valgrind/sweep.sh /tmp/vg-sweep
#
# Env: LOGOSC (default build/bin/logosc), LOGOS_LIB_DIR (default build/lib/logos),
#      JOBS (default nproc), RUN_TIMEOUT (default 60s per instrumented run).
#
# ⚠ A CLEAN VALGRIND ON A PROGRAM THAT NEVER CALLED malloc IS COVERAGE, NOT
# EVIDENCE. Most fixtures in this corpus allocate NOTHING — `hello.logos` and
# `iter_terminals` report `0 allocs, 0 frees`. So every row carries its ALLOCS
# count and `summary.txt` reports SWEPT and ALLOCATED as separate numbers. A
# sweep that reports "6525 clean" while 5300 never reached the allocator has
# measured nothing.
#
# ⚠ `still reachable` IS NOT A LEAK and is recorded, never rowed.
#
# Per fixture one line in results.tsv:
#   <path>\t<status>\t<allocs>\t<frees>\t<definite>\t<indirect>\t<reachable_blocks>\t<invalid>\t<rc>
# status: OK | LEAK | CORRUPT | LEAK+CORRUPT | CFAIL | LINKFAIL | RUNFAIL | TIMEOUT | NOVG
# Full valgrind stderr for every non-OK fixture is kept under $OUTDIR/vg/ so a
# queue row can cite it.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUTDIR="${1:-$ROOT/build/vg-sweep}"
LOGOSC="${LOGOSC:-$ROOT/build/bin/logosc}"
LIB_DIR="${LOGOS_LIB_DIR:-$ROOT/build/lib/logos}"
JOBS="${JOBS:-$(nproc)}"
RUN_TIMEOUT="${RUN_TIMEOUT:-60}"

command -v valgrind >/dev/null 2>&1 || { echo "sweep: valgrind not installed — no verdict"; exit 2; }
[ -x "$LOGOSC" ] || { echo "sweep: no compiler at $LOGOSC"; exit 2; }

rm -rf "$OUTDIR"; mkdir -p "$OUTDIR/vg"
: > "$OUTDIR/results.tsv"

# The archive list is computed ONCE — it is identical for every fixture and
# globbing it 6500 times is the whole difference between minutes and an hour.
ARCHIVES=()
for a in "$LIB_DIR"/liblstdlib*.a; do [ -f "$a" ] && ARCHIVES+=("$a"); done
for a in "$LIB_DIR"/liblogos-*.a;  do [ -f "$a" ] && ARCHIVES+=("$a"); done
for a in "$LIB_DIR"/*.a; do
    case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && ARCHIVES+=("$a") ;; esac
done
printf '%s\n' "${ARCHIVES[@]}" > "$OUTDIR/archives.txt"

find "$ROOT/tests/logos/pass" "$ROOT/tests/imported/pass" "$ROOT/tests/spec/pass" \
     -name '*.logos' 2>/dev/null | sort > "$OUTDIR/fixtures.txt"
echo "sweep: $(wc -l < "$OUTDIR/fixtures.txt") fixtures, $JOBS jobs, compiler $LOGOSC"

export ROOT OUTDIR LOGOSC LIB_DIR RUN_TIMEOUT
cat > "$OUTDIR/one.sh" <<'ONEEOF'
#!/usr/bin/env bash
set -uo pipefail
SRC="$1"
REL="${SRC#$ROOT/}"
KEY="$(echo "$REL" | tr '/' '_')"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
emit() { printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$REL" "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8"; }
if ! LOGOS_LIB_DIR="$LIB_DIR" timeout 120 "$LOGOSC" "$SRC" -o "$T/f.o" >"$T/cc.log" 2>&1; then
    emit CFAIL - - - - - - -; exit 0
fi
mapfile -t A < "$OUTDIR/archives.txt"
if ! cc "$T/f.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
        -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$T/f.bin" 2>"$T/link.log"; then
    emit LINKFAIL - - - - - - -; exit 0
fi
timeout "$RUN_TIMEOUT" valgrind --leak-check=full --show-leak-kinds=definite,indirect \
        --errors-for-leak-kinds=definite,indirect --error-exitcode=97 \
        "$T/f.bin" >"$T/out" 2>"$T/vg"
RC=$?
if [ "$RC" = 124 ]; then emit TIMEOUT - - - - - - 124; exit 0; fi
ALLOCS=$(sed -n 's/.*total heap usage: *\([0-9,]*\) allocs, *\([0-9,]*\) frees.*/\1/p' "$T/vg" | tr -d ',')
FREES=$( sed -n 's/.*total heap usage: *\([0-9,]*\) allocs, *\([0-9,]*\) frees.*/\2/p' "$T/vg" | tr -d ',')
DEF=$(sed -n 's/.*definitely lost: *\([0-9,]*\) bytes.*/\1/p' "$T/vg" | tr -d ',')
IND=$(sed -n 's/.*indirectly lost: *\([0-9,]*\) bytes.*/\1/p' "$T/vg" | tr -d ',')
RCH=$(sed -n 's/.*still reachable: *[0-9,]* bytes in *\([0-9,]*\) blocks.*/\1/p' "$T/vg" | tr -d ',')
INV=$(grep -cE 'Invalid free|Invalid read|Invalid write|Mismatched free' "$T/vg")
if [ -z "$ALLOCS" ]; then emit NOVG - - - - - - "$RC"; cp "$T/vg" "$OUTDIR/vg/$KEY.vg"; exit 0; fi
: "${DEF:=0}"; : "${IND:=0}"; : "${RCH:=0}"
ST=OK
[ "$DEF" -ne 0 ] || [ "$IND" -ne 0 ] && ST=LEAK
if [ "$INV" -ne 0 ]; then [ "$ST" = LEAK ] && ST="LEAK+CORRUPT" || ST=CORRUPT; fi
# allocs - frees must equal the deliberately-reachable blocks. A gap with a
# clean leak summary is what a destructor-count oracle sees and leak-check
# may not; it is recorded as its own status, never folded into OK.
if [ "$ST" = OK ] && [ "$((ALLOCS - FREES))" -ne "$RCH" ]; then ST=IMBALANCE; fi
[ "$ST" != OK ] && cp "$T/vg" "$OUTDIR/vg/$KEY.vg"
emit "$ST" "$ALLOCS" "$FREES" "$DEF" "$IND" "$RCH" "$INV" "$RC"
ONEEOF
chmod +x "$OUTDIR/one.sh"

xargs -a "$OUTDIR/fixtures.txt" -P "$JOBS" -n 1 "$OUTDIR/one.sh" >> "$OUTDIR/results.tsv"

# ── The grid. SWEPT and ALLOCATED are separate numbers on purpose. ──────────
awk -F'\t' '
  { n++; st[$2]++ }
  $3 != "-" && $3 != "" && $3+0 > 0 { alloc++; if ($2=="OK") aok++ }
  $3 == "0" { zero++ }
  END {
    printf "swept          %d\n", n
    printf "  allocated    %d   (reached malloc at least once)\n", alloc
    printf "  zero-alloc   %d   (COVERAGE, NOT EVIDENCE)\n", zero
    printf "  not run      %d\n", n - alloc - zero
    printf "clean (of allocating)  %d\n", aok
    printf "\nby status:\n"
    for (k in st) printf "  %-14s %d\n", k, st[k]
  }' "$OUTDIR/results.tsv" | tee "$OUTDIR/summary.txt"
echo
echo "rows: $OUTDIR/results.tsv   valgrind stderr for every non-OK: $OUTDIR/vg/"
