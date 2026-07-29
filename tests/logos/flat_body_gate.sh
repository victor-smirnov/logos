#!/usr/bin/env bash
# flat_body_gate.sh LOGOSC TEST_LOGOS
#
# An EMITTED fn's body must be FLAT — the quote's `#(body)` occupies the fn's
# own body slot (`fn_body <- HASH LPAREN expr RPAREN`, ADR 0024 S5), not a
# statement position inside it.
#
# ⚠ WHY THIS NEEDS A GATE OF ITS OWN. The extra scope reads as cosmetic and is
# not: reintroducing it (`-> #(rt) { #(body) }` in `emit_fn_quote`) fails the
# stdlib build on `canon_split_fast` with "use of moved variable '__out'" —
# wrapping a body in a block changes what move analysis sees. That failure is
# loud but it is also ARBITRARY: it depends on one emitted fn in one stdlib
# query happening to move a local. Nothing systematic stands behind it, so the
# shape can regress anywhere the accident does not repeat, and there the dump
# still reparses, DWARF still points at it, and the query still returns the
# right rows. Before this gate the only thing holding the property was an author
# reading dumps by hand, once, per conversion.
#
# ⚠ AND IT CANNOT BE FOLDED INTO run_gendir_test.sh, whose corpus is mostly
# HAND-WRITTEN quotes — `quote_item_exprblob_cursor` splices a bare `{ … }`
# scope ON PURPOSE. A bare block is only wrong as the FIRST thing in an emitted
# fn's body; deeper down it is ordinary (a join chain's hash-build phase opens
# one). So the assertion is narrow by construction: fn head, then the very next
# line.
set -euo pipefail

LOGOSC="$1"
TEST_LOGOS="$2"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if ! "$LOGOSC" -g "$TEST_LOGOS" --gen-dir "$TMPD/gen" -o "$TMPD/test.o" \
        2>"$TMPD/err"; then
    echo "FAIL: logosc failed:"; cat "$TMPD/err"; exit 1
fi

# The user module's own emitted dumps — `test.<stem>.N.gen.logos`. Canon's
# factory modules (`logos.gen.*`) are a different producer and are not in scope.
shopt -s nullglob
DUMPS=("$TMPD"/gen/test.*.gen.logos)
if [ "${#DUMPS[@]}" -lt 1 ]; then
    echo "FAIL: no test.*.gen.logos dump produced — nothing was asserted"
    ls -la "$TMPD/gen" || true
    exit 1
fi

FNS=0
BAD=0
for f in "${DUMPS[@]}"; do
    # Count emitted fn heads, and flag any whose next line is a bare `{`.
    n=$(awk '/^(pub )?fn /{c++} END{print c+0}' "$f")
    FNS=$((FNS + n))
    if out=$(awk '/^(pub )?fn /{head=$0; hl=NR; if ((getline nxt) > 0 &&
                     nxt ~ /^[[:space:]]*\{[[:space:]]*$/)
                     print FILENAME":"hl": "head" >>> "nxt}' "$f") && [ -n "$out" ]; then
        echo "FAIL: emitted fn body opens with a nested scope:"
        echo "$out"
        BAD=1
    fi
done

if [ "$BAD" -ne 0 ]; then exit 1; fi
# A gate that asserted nothing is a gate that lies: the dumps must actually
# contain emitted fns.
if [ "$FNS" -lt 1 ]; then
    echo "FAIL: dumps contain no emitted fn — the assertion was vacuous"
    echo "  dumps: ${DUMPS[*]}"
    exit 1
fi
exit 0
