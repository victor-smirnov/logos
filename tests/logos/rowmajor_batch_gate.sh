#!/usr/bin/env bash
# rowmajor_batch_gate.sh LOGOSC PASS_DIR
#
# THE BATCH LAYOUT, PINNED IN BOTH DIRECTIONS — ADR 0025 §2 / S6.
#
# ── WHY A PAIR AND NOT AN ASSERTION ─────────────────────────────────────────
#
# §2 says the pull shape is ONE and the LAYOUT is selected by the source's
# declaration. Two layouts exist:
#
#   ROW-MAJOR   `RowsBatch<R>` = `&[R]` — the row is `bb[j]`, the counter is
#               cast (`bb.len()` is `i64` on a slice).
#               Selected by `impl RowMajor for <stream>` ⇒ natspec letter `m`.
#   COLUMNAR    `ColsBatch` over a Memoria leaf's PdtBuf slots — the row is
#               built cell by cell, `bb.<col>_at(j)`, and `len()` is `u64`.
#               The ABSENCE of `m`; every producer older than S6 is one.
#
# A gate that checked only the row-major arm is green on an emitter that emits
# `bb[j]` for EVERYTHING — which does not compile against a family walk, but
# "does not compile" is only a fact about a tree that still has such a fixture.
# A gate that checked only the columnar arm is the tree S6 started from, where
# the row-major half of §2 was prose and every row-major source died in codegen.
# So both layouts are read here, off two fixtures that share the emitter and
# nothing else, and each layout's spelling is asserted ABSENT from the other:
# the selector is what is under test, not the two spellings.
#
#   ROW-MAJOR SUBJECT  `pass/deem_rowmajor_batch_source::froms_run` — a
#                      hand-written four-packet stream (one packet empty).
#   COLUMNAR SUBJECT   `pass/deem_ctr_family_streams` — the generated container
#                      family's leaf walk, the layout the emitter spelled first.
#
# ── WHAT THE BEHAVIOURAL FIXTURE CANNOT SAY ─────────────────────────────────
#
# `deem_rowmajor_batch_source` asserts answers and the SOURCE's own call log
# (`PULLS == 5`, `ROWS == 5`), which is a strong oracle for "the batches were
# pulled and the empty one was a tick". It is blind to HOW the row was read: an
# emitter that copied each batch into a `Vec` first, or that indexed with a
# second counter of its own, gives the same five pulls and the same three
# answers. The row read is an artifact fact and is read off the artifact.
#
# ── PROBE PAIR, MEASURED (fixture-only perturbations, no stdlib rebuild) ────
#
#   P1  `impl RowMajor for EdgeStream` DELETED from the row-major fixture:
#       logosc rc 1 before any dump is complete — `slice has no method 'a_at'`,
#       `'b_at'`, and `let '__bn0': type mismatch — expected u64, got i64`.
#       The gate reds on the compile clause, which is the honest failure: the
#       selector is not a formatting choice, it is what makes the file build.
#   P2  the ABSENCE clauses are NON-VACUOUS BY CONSTRUCTION, which is better
#       evidence than a perturbation: every `deny` pattern here is a `want`
#       pattern of the OTHER subject on the SAME tree in the SAME run. A
#       spelling that no longer exists anywhere would take its `want` down with
#       it, so a silently-dead `deny` cannot hide — the failure mode where an
#       absence check passes because the string stopped being emittable at all
#       is unrepresentable in this gate.
#
# ⚠ RE-AIMS WITH ITS SUBJECT, NEVER WEAKENS: the `bb[j as i64]` and `<col>_at(j)`
# spellings are pins over `params::batch_row_text`. If a later slice changes how
# a row is spelled off a batch, this reds — the instruction is to re-aim it WITH
# that change and record it, not to drop a clause.
set -uo pipefail

LOGOSC="$1"
PASS_DIR="$2"
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
FAILED=0

note() { echo "FAIL: $*"; FAILED=1; }

compile() {   # compile NAME -> $TMPD/<name>/ dumps, rc in $TMPD/<name>.rc
    local n=$1
    "$LOGOSC" "$PASS_DIR/$n.logos" -o "$TMPD/$n.o" --gen-dir "$TMPD/$n" \
        > "$TMPD/$n.out" 2> "$TMPD/$n.err"
    echo $? > "$TMPD/$n.rc"
}

want() {      # want NAME PATTERN DESC  — pattern must appear in the dumps
    local n=$1 p=$2 d=$3 c
    c=$(cat "$TMPD/$n"/*.gen.logos 2>/dev/null | grep -cF -- "$p")
    [ "$c" -ge 1 ] || note "$n: $d — no dump line carries '$p'"
}

deny() {      # deny NAME PATTERN DESC  — pattern must be absent from the dumps
    local n=$1 p=$2 d=$3 c
    c=$(cat "$TMPD/$n"/*.gen.logos 2>/dev/null | grep -cF -- "$p")
    [ "$c" -eq 0 ] || note "$n: $d — $c dump line(s) carry '$p'"
}

RM=deem_rowmajor_batch_source
CB=deem_ctr_family_streams

compile "$RM"
compile "$CB"
[ "$(cat "$TMPD/$RM.rc")" = 0 ] || {
    note "$RM did not compile (rc $(cat "$TMPD/$RM.rc")) — the row-major arm is
      not merely unasserted, it is unreachable. First errors:"
    sed -n '1,5p' "$TMPD/$RM.err"
}
[ "$(cat "$TMPD/$CB.rc")" = 0 ] || {
    note "$CB did not compile (rc $(cat "$TMPD/$CB.rc")) — the columnar half of
      the pair has no subject. First errors:"
    sed -n '1,5p' "$TMPD/$CB.err"
}

# ── the ONE pull shape, both layouts ────────────────────────────────────────
want "$RM" ".next_batch();"        "the outer pull is the inherent batch door"
want "$CB" ".next_batch();"        "the outer pull is the inherent batch door"

# ── ROW-MAJOR: the row IS the index; no cell accessor anywhere ──────────────
want "$RM" "[(__bj0 as i64)]"      "the row is read by index off the slice batch"
want "$RM" "(__bb0.len() as u64)"  "the slice counter is cast to the pull counter's type"
deny "$RM" "_at(__bj0)"            "a row-major batch has no per-column accessor"

# ── COLUMNAR: the row is built cell by cell; no slice index ─────────────────
want "$CB" "_at(__bj0)"            "the columnar row is built from per-column accessors"
deny "$CB" "[(__bj0 as i64)]"      "a columnar batch is not indexed as a slice"
deny "$CB" ".len() as u64)"        "a ColsBatch length is already u64 — no cast"

# `FAILED` is set to the literal 1 by `note()` and to nothing else — a
# two-valued flag, never a count and never a captured `$?`, so the 8-bit
# ceiling this rule guards is unreachable here.
exit $FAILED  # lint:exit-ok — FAILED is a 0/1 literal flag, never a count
