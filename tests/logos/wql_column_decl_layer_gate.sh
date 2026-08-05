#!/usr/bin/env bash
# wql_column_decl_layer_gate.sh LOGOSC FAIL_DIR
#
# A REFUSAL THAT MOVES DOWN A LAYER IS A REGRESSION EVEN THOUGH BOTH ARE RED,
# AND `run_test.sh` CANNOT SEE IT.
#
# THE DEFECT CLASS. The fail-test harness asserts CONTAINMENT: `.expected` is a
# substring its stderr must have. It has no way to assert that a string is
# ABSENT. Every one of the four column-declaration fixtures used to fail — with
# a diagnostic from a layer that names nothing the author wrote:
#
#   `select`/`order by`/`group by` over a tuple column
#       <metaprog-blob-subst>:NNNN: error [fn q_run]: method 'Vec__push' arg 1:
#       expected i64, got (i64, &[u8])          ← a GENERATED BLOB's line number
#   `where x < (…)` over the same column
#       'arith.cmpi' op operand #0 must be signless-integer-like, but got
#       '!llvm.ptr'                             ← the MLIR VERIFIER, plus a
#                                                 ~21 000-line dump beside it
#
# so a regression that put either message back would leave all four `.expected`
# assertions PASSING as long as the front-end sentence still appeared somewhere
# in the same stderr — and would leave them passing outright if the front-end
# check were removed and only the deeper message remained, since the fixtures
# would still be red. Containment cannot distinguish "refused where the author
# can act" from "refused eventually, somewhere". This gate can.
#
# WHAT IS ASSERTED, per fixture:
#   1. the compile FAILS (rc != 0);
#   2. stderr CONTAINS the front-end refusal — `wql!: source ` and the
#      declaration-site clause that only `reflect::report_column_refusal`
#      writes;
#   3. stderr does NOT contain the MLIR verifier's phrasing;
#   4. stderr does NOT contain a generated-blob location;
#   5. no `logos-mlir-verify-fail.mlir` was written — mlir-gen drops that dump
#      into the WORKING DIRECTORY on a module-verification failure, so its
#      existence is independent evidence that MLIR was reached at all, read
#      from the filesystem rather than from the text.
# (3) and (4) are the ones the harness cannot make. (5) is the one that does not
# go through stderr at all, so a message that changed wording cannot silence it.
#
# ⚠ IT PROVES ITS OWN MATCHERS BEFORE IT REPORTS ANYTHING. A negative assertion
# is the easiest kind to write inoperative: a typo in a forbidden string is a
# gate that passes forever. Each matcher is run against a PLANTED stderr holding
# the exact historical messages, where it must FIRE; a matcher that stays silent
# on its own canary fails the gate as broken (exit 9) rather than passing.
#
# ⚠ NO `| grep -q`. `set -o pipefail` plus a `grep -q` that exits at the first
# match makes the writer die of SIGPIPE and reports 141 as the pipeline status —
# recorded, and in the NEGATIVE form it inverts the verdict, so a forbidden
# string that IS present reads as absent. Every match here reads a FILE.
set -uo pipefail

LOGOSC="${1:?logosc}"
FAILDIR="${2:?fail fixture dir}"

# ⚠ ABSOLUTE, BECAUSE THE RUN BELOW CHANGES DIRECTORY. Measured while writing
# this: a RELATIVE `build/bin/logosc` became "command not found" inside the
# per-fixture directory, the shell answered 127 — a NON-ZERO status, so the
# "compile must fail" assertion passed — and the gate then reported the missing
# front-end message as a defect in the TREE. A gate that cannot run its subject
# must say so, not describe the tree.
case "$LOGOSC" in /*) ;; *) LOGOSC="$PWD/$LOGOSC" ;; esac
[ -x "$LOGOSC" ] || { echo "FAIL: '$LOGOSC' is not an executable — nothing in this run is evidence about the tree"; exit 2; }
[ -d "$FAILDIR" ] || { echo "FAIL: '$FAILDIR' is not a directory"; exit 2; }
case "$FAILDIR" in /*) ;; *) FAILDIR="$PWD/$FAILDIR" ;; esac

# The four fixtures whose refusal must come from the front end. Listed here and
# checked against the directory below: a name that no longer has a fixture is a
# silent loss of coverage, so it is an error rather than a skip.
FIXTURES=(
    wql_column_decl_option_col_fail
    wql_column_decl_data_enum_col_fail
    wql_column_decl_tuple_col_fail
    wql_domain_layer_mlir_tuple_col_fail
)

# REQUIRED substrings — only `reflect::report_column_refusal` writes both.
REQ1='wql!: source `'
REQ2='Refused HERE, at the source'
# FORBIDDEN substrings — the two lower layers, in their own words.
#
# ⚠ NOT the bare token `arith.cmpi`: the refusal's own GROUND quotes it while
# explaining what used to happen, so the bare token is present on a correct run.
# What identifies the layer is the VERIFIER'S SENTENCE.
FORB_MLIR='op operand #0 must be signless-integer-like'
FORB_BLOB='<metaprog-blob-subst>'

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Every call site passes a LITERAL in 2..10 — the nine distinct reasons this
# gate can say no, written out at each site — and no code in this file is ever
# computed, so the 8-bit ceiling is unreachable.
fail() { echo "FAIL: $2"; exit "$1"; }  # lint:exit-ok — $1 is always a literal 2..10, never arithmetic

# ── SELF-TEST: every matcher must fire on a planted stderr ────────────────────
CANARY="$WORK/canary.txt"
{
    echo "error: 'arith.cmpi' op operand #0 must be signless-integer-like, but got '!llvm.ptr'"
    echo "<metaprog-blob-subst>:1898: error [fn q_run]: method 'Vec__push' arg 1: expected i64, got (i64, &[u8])"
} > "$CANARY"
caught=0
if grep -qF -- "$FORB_MLIR" "$CANARY"; then caught=$((caught + 1)); fi
if grep -qF -- "$FORB_BLOB" "$CANARY"; then caught=$((caught + 1)); fi
# The REQUIRED matchers must be SILENT on this same text — a required-string
# matcher that matches anything is the mirror of a forbidden one that matches
# nothing, and the canary pins both directions with one file.
if grep -qF -- "$REQ1" "$CANARY"; then fail 9 "REQ1 matched the canary, which holds no front-end message — the required matcher is inoperative"; fi
if grep -qF -- "$REQ2" "$CANARY"; then fail 9 "REQ2 matched the canary, which holds no front-end message — the required matcher is inoperative"; fi
if [ "$caught" -ne 2 ]; then
    fail 9 "the forbidden-string matchers caught $caught of 2 on their own canary — this GATE is wrong, not the tree"
fi

# ── THE POPULATION ───────────────────────────────────────────────────────────
for base in "${FIXTURES[@]}"; do
    src="$FAILDIR/$base.logos"
    [ -f "$src" ] || fail 2 "$src does not exist — this gate names a fixture the corpus no longer has"
done

# ── THE RUN ──────────────────────────────────────────────────────────────────
# Each compile runs in its OWN empty directory so that (5) is a question about
# THIS compile and not about a dump some earlier run left behind.
checked=0
for base in "${FIXTURES[@]}"; do
    d="$WORK/$base"
    mkdir -p "$d"
    err="$d/stderr.txt"
    ( cd "$d" && "$LOGOSC" "$FAILDIR/$base.logos" -o /dev/null ) > "$err" 2>&1
    rc=$?
    [ "$rc" -ne 0 ] || fail 3 "$base compiled successfully (rc 0) — the refusal is gone"
    grep -qF -- "$REQ1" "$err" || fail 4 "$base: stderr has no \`$REQ1\` — the refusal is not the front end's"
    grep -qF -- "$REQ2" "$err" || fail 5 "$base: stderr has no \`$REQ2\` — the refusal did not come from the declaration site"
    if grep -qF -- "$FORB_MLIR" "$err"; then
        fail 6 "$base: the MLIR VERIFIER answered — the refusal moved down a layer, which is a regression even though the test is red"
    fi
    if grep -qF -- "$FORB_BLOB" "$err"; then
        fail 7 "$base: a GENERATED BLOB answered — the refusal moved down a layer, which is a regression even though the test is red"
    fi
    if [ -e "$d/logos-mlir-verify-fail.mlir" ]; then
        fail 8 "$base: mlir-gen wrote logos-mlir-verify-fail.mlir — MLIR was reached, so the front-end refusal did not stop the build"
    fi
    checked=$((checked + 1))
done

# ⚠ THE COUNT IS THE FLOOR, and it is derived from the list rather than restated:
# a loop that ran zero times would otherwise reach this line reporting success.
if [ "$checked" -ne "${#FIXTURES[@]}" ]; then
    fail 10 "checked $checked of ${#FIXTURES[@]} fixtures — the loop did not run"
fi
echo "OK: $checked fixtures refused by the front end; MLIR verifier and generated-blob layers both silent; 2 canaries caught"
exit 0
