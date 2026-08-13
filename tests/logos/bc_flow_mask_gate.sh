#!/usr/bin/env bash
# bc_flow_mask_gate.sh LOGOSC FIXTURE_DIR
#
# A BORROW-FLOW MASK IS AN ANSWER NO VERDICT ASKS FOR.
#
# The per-function borrow-flow summary (`to_result` / `to_outparam[j]`) is the
# borrow checker's INTERNAL answer about a callee. A wrong bit in it moves a
# verdict only when some caller happens to spell the shape that reads that bit;
# until then the corpus is green and the summary is fiction. D1 round 11 / X3 is
# exactly that case: name-keyed charging manufactured PARAM-TO-PARAM edges on
# DISJOINT field stores (`dst.x = a; dst.y = z` ⇒ `out1<-0x4 out2<-0x2` beside
# the true `out0<-0x6`), an over-approximation with 0 verdict moves in the whole
# stdlib — so an rc-shaped fixture could not pin it, in EITHER direction: it
# could not red before the fix and cannot red after a regression.
#
# This gate reads the masks themselves (LOGOS_DUMP_FLOWS) and asserts them,
# positively AND negatively. The negative half is the load-bearing one — the
# defect is a bit that should NOT be there.
#
# ⚠ THE FLOOR. A gate whose input list can silently empty is the defect it
# checks for: if `logosc` stops printing summaries (env renamed, dump gated
# differently, fixture failing to compile), every "no forbidden bit" assertion
# passes vacuously. So each fixture must produce its NAMED line first, and the
# run must produce at least MIN_FLOWS lines overall.
set -euo pipefail

LOGOSC="${1:?logosc}"
DIR="${2:?fixture dir}"
MIN_FLOWS=2

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
fail=0
seen_flows=0

note() { echo "FAIL: $*"; fail=1; }

# run FIXTURE FILTER -> $TMPD/flows.txt (the [flow] lines for that filter),
# with the compiler's exit status in $RC.
#
# ⚠ NOT `rc=$(run …)`. A command substitution runs in a SUBSHELL, so the
# `seen_flows` floor this gate depends on would be incremented in a process that
# exits immediately — the counter would read 0 forever and the floor would fire
# on a perfectly good run (measured: it did, first try).
RC=0
run() {
    local f="$1" filt="$2"
    # rc comes from the compiler, not from a pipeline (see run_test.sh's note).
    set +e
    LOGOS_DUMP_FLOWS="$filt" "$LOGOSC" "$DIR/$f" -o /dev/null \
        > "$TMPD/out.txt" 2> "$TMPD/err.txt"
    RC=$?
    set -e
    grep -F '[flow]' "$TMPD/err.txt" > "$TMPD/flows.txt" || true
    seen_flows=$(( seen_flows + $(wc -l < "$TMPD/flows.txt") ))
}

# ── x3_wire3: two disjoint &mut params into two disjoint fields of a third ──
# TRUE:  result<-0 out0<-0x6      FICTION: any out1/out2 bit at all.
run x3_wire3.logos wire3; rc=$RC
[ "$rc" = 0 ] || note "x3_wire3 must COMPILE (it is a mask fixture, not a
      refusal fixture); logosc exited $rc:
$(sed -n '1,20p' "$TMPD/err.txt")"
line=$(grep -F '$wire3__f__' "$TMPD/flows.txt" || true)
if [ -z "$line" ]; then
    note "no summary line for wire3 — the dump printed nothing to assert
      against, so every mask assertion below would pass vacuously."
else
    grep -qF 'result<-0 out0<-0x6' <<<"$line" ||
        note "wire3's mask is not the true one (want 'result<-0 out0<-0x6'):
      $line"
    if grep -qE ' out1<-| out2<-' <<<"$line"; then
        note "wire3 carries a PARAM-TO-PARAM bit it cannot have: the body
      stores \`a\` into dst.x and \`z\` into dst.y and writes through neither,
      so out1 and out2 must not appear at all. This is X3's over-refusal
      returning — charging keyed by a place's ROOT instead of the place.
      $line"
    fi
fi

# ── x3_sp0_reauth: the corpus-shaped twin. out1<-0x1 is true (c's loan reaches
#    v through h.i.r); the 0x4 bit (w reaches v) is the fiction.
run x3_sp0_reauth.logos stash2; rc=$RC
[ "$rc" = 1 ] || note "x3_sp0_reauth must be REFUSED (it is fail/bc_d1r10_sp0_
      aggregate_composed_in's twin); logosc exited $rc"
line=$(grep -F '$stash2__f__' "$TMPD/flows.txt" || true)
if [ -z "$line" ]; then
    note "no summary line for stash2 — nothing to assert against."
else
    grep -qE 'out1<-0x1( |$)' <<<"$line" ||
        note "stash2's out1 mask is not 0x1. 0x5 is the pre-X3 answer, whose
      0x4 bit claims the OTHER \`&mut\` param reaches \`v\`:
      $line"
fi

if [ "$seen_flows" -lt "$MIN_FLOWS" ]; then
    note "the whole run produced $seen_flows [flow] lines (floor $MIN_FLOWS).
      LOGOS_DUMP_FLOWS printed nothing, so this gate asserted nothing."
fi

if [ "$fail" != 0 ]; then exit 1; fi
echo "bc_flow_mask_gate: 2 fixtures, masks pinned positively and negatively"
