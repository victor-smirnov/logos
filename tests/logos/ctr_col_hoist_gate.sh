#!/usr/bin/env bash
# ctr_col_hoist_gate.sh LOGOSC FIXTURE LIB_DIR EXTRACTOR
#
# ADR 0025 §2 (#341) — THE EMITTED SCAN RESOLVES EACH COLUMN ONCE PER BATCH.
#
# A `deem` over a container family reads a columnar leaf batch through the
# family's typed views (`col_<c>()`), which the emitter binds ABOVE the row
# loop. A view is built by `pdt_col`, whose `ColsBatch::resolve_fse` is the one
# `(allocator, slot)` resolve — so its call count is the number of column resolves (counted at the CALLEE: the edge into `pdt_col` itself is not in the profile). The
# answer does not depend on it (the per-cell form `key_at(j)` returns the same
# rows), which is why this is a callgrind COUNT and not a pass fixture: a
# regression to the per-cell form is invisible to every result check.
#
# The expectation is read from the fixture's constants, not invented here:
#   2 queries × 2 columns × LEAVES   (ordered arm: `key`, `val`)
# + 1 query   × 1 column  × VLEAVES  (vector arm: `val`; `pos` has no cell)
# The per-cell form costs 2·2·N + VN instead.
#
# CONTROL (measured when the gate landed): emitting the row as `bb.<c>_at(j)`
# again reds this gate with the per-cell count.
set -euo pipefail

LOGOSC="${1:?logosc path}"
FIXTURE="${2:?fixture .logos}"
LIB_DIR="${3:?stdlib archive dir}"
EXTRACTOR="${4:?callgrind_calls.py path}"

for f in "$LOGOSC" "$FIXTURE" "$EXTRACTOR"; do
    if [ ! -e "$f" ]; then echo "FAIL(2): missing input: $f"; exit 2; fi
done
if [ ! -d "$LIB_DIR" ]; then echo "FAIL(2): no archive dir: $LIB_DIR"; exit 2; fi
if ! command -v valgrind > /dev/null 2>&1; then
    echo "FAIL(2): valgrind is not installed — this gate MEASURES call counts and"
    echo "         has no verdict without it."
    exit 2
fi

cst() { sed -n "s/^const $1: u64 = \([0-9]*\)u64;.*/\1/p" "$FIXTURE" | head -1; }
N=$(cst N); LEAVES=$(cst LEAVES); VN=$(cst VN); VLEAVES=$(cst VLEAVES)
if [ -z "$N" ] || [ -z "$LEAVES" ] || [ -z "$VN" ] || [ -z "$VLEAVES" ]; then
    echo "FAIL(2): could not read const N / LEAVES / VN / VLEAVES from $FIXTURE."
    exit 2
fi

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if ! "$LOGOSC" "$FIXTURE" -o "$TMPD/t.o" > "$TMPD/cc.log" 2>&1; then
    echo "FAIL(2): logosc failed on $FIXTURE:"; cat "$TMPD/cc.log"; exit 2
fi
shopt -s nullglob
ARCHIVES=("$LIB_DIR"/liblstdlib*.a "$LIB_DIR"/liblogos-*.a)
for a in "$LIB_DIR"/*.a; do
    case "$(basename "$a")" in
        liblstdlib*|liblogos-*) ;;
        *) ARCHIVES+=("$a") ;;
    esac
done
if ! cc "$TMPD/t.o" -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group \
        -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
        -o "$TMPD/t" > "$TMPD/link.log" 2>&1; then
    echo "FAIL(2): link failed:"; cat "$TMPD/link.log"; exit 2
fi

set +e
"$TMPD/t" > "$TMPD/prog.out" 2>&1
PROG_RC=$?
set -e
if [ "$PROG_RC" != 0 ]; then
    echo "FAIL(2): $FIXTURE exited $PROG_RC — its own assertions failed, so a"
    echo "         call count would describe a different execution."
    cat "$TMPD/prog.out"; exit 2
fi

if ! valgrind --tool=callgrind --callgrind-out-file="$TMPD/cg.out" \
        "$TMPD/t" > "$TMPD/cg.stdout" 2> "$TMPD/cg.err"; then
    echo "FAIL(2): callgrind run failed:"; tail -20 "$TMPD/cg.err"; exit 2
fi
if ! python3 "$EXTRACTOR" "$TMPD/cg.out" > "$TMPD/edges" 2> "$TMPD/edges.err"; then
    echo "FAIL(2): the call-edge extractor could not read the callgrind file:"
    cat "$TMPD/edges.err"; exit 2
fi

RESOLVES=$(awk -F'\t' '$3 ~ /ColsBatch([$]M[0-9a-f]+)?__resolve_fse__f__/ { n += $1 } END { if (n > 0) print n }' "$TMPD/edges")
if [ -z "$RESOLVES" ]; then
    echo "FAIL(2): no call edge into ColsBatch::resolve_fse in the profile — inlined away or"
    echo "         renamed, so this gate cannot decide. Edges seen (top 20):"
    head -20 "$TMPD/edges"; exit 2
fi

WANT=$((4 * LEAVES + VLEAVES))
PERCELL=$((4 * N + VN))
echo "measured: resolves=$RESOLVES  (per batch: $WANT, per cell: $PERCELL)"
if [ "$RESOLVES" != "$WANT" ]; then
    echo "FAIL(1): the emitted scans resolved a column $RESOLVES times; ADR 0025 §2"
    echo "         says once per column per batch = $WANT. $PERCELL would be the"
    echo "         per-cell form (\`bb.<c>_at(j)\` in the row loop)."
    exit 1
fi
echo "PASS"
