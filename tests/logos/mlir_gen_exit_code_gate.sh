#!/usr/bin/env bash
# mlir_gen_exit_code_gate.sh LOGOSC FAIL_DIR [SRC_COMPILER_DIR]
#
# A COMPILER THAT REPORTS AN ERROR AND RETURNS SUCCESS IS A GATE THAT LIES, AND
# NO FAIL TEST IN THE CORPUS CAN SEE IT.
#
# `run_test.sh`'s fail mode runs logosc under `|| true` and asserts only that a
# string appears on stderr. That is right for a diagnostic assertion and USELESS
# for an exit code: a fail test passes identically whether logosc exited 0 or 1,
# so the property "mlir-gen's self-diagnosis reaches the exit code" has no
# expression anywhere in the corpus. This gate is that expression, and it is a
# separate file precisely so that pinning it costs the 1900-odd fail tests
# nothing — a change to `run_test.sh` is a change to every one of them.
#
# MEASURED at 0fb56500 on `mlir_gen_unsupported_cast_fail.logos`:
#     rc = 0, stderr "mlir_gen: unsupported cast", 1288-byte object ON DISK,
# with the program's assignment silently not emitted. The asserted properties:
#
#   1. rc != 0                      — the report reaches the exit code;
#   2. NO object file is written    — nothing downstream can consume the
#                                     miscompile even if it ignores rc;
#   3. stderr carries an `mlir_gen: internal:` line and the enforcement line
#                                   — the report went through the R2 sink and
#                                     not a raw fprintf that happens to look
#                                     similar.
#
# ⚠ AND IT PROVES ITS OWN INSTRUMENT IN THE SAME RUN. A gate that only ever
# asserts "this compile failed" passes when logosc is missing, when the archive
# path is wrong, when the fixture does not parse — every way of failing looks
# like the way it wants. So a CONTROL program with no unsupported cast is
# compiled through the identical invocation and must succeed WITH an object
# file. If the control does not compile, the apparatus is dead and this file
# reports ITSELF broken rather than reporting the compiler clean.
set -euo pipefail

LOGOSC="${1:?logosc}"
FAILDIR="${2:?tests/logos/fail}"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

rc_gate=0

# ── THE INSTRUMENT CANARY: a clean program must compile and leave an object ──
CTRL="$TMPD/control.logos"
cat > "$CTRL" <<'EOF'
package test;
fn main() -> i32 {
    let s: i64 = 3i64;
    let mut a: f64 = 1.0f64;
    a = s as f64;
    if a > 2.0f64 { return 0i32; }
    return 1i32;
}
EOF
CTRL_OBJ="$TMPD/control.o"
set +e
"$LOGOSC" "$CTRL" -o "$CTRL_OBJ" >"$TMPD/c.out" 2>"$TMPD/c.err"
ctrl_rc=$?
set -e
if [ "$ctrl_rc" -ne 0 ]; then
    echo 'GATE BROKEN: the control program (an `i64 as f64`, a SUPPORTED cast)'
    echo "  did not compile — rc=$ctrl_rc. Nothing below measures the compiler;"
    echo "  it measures this harness. stderr:"
    cat "$TMPD/c.err"
    exit 1
fi
if [ ! -s "$CTRL_OBJ" ]; then
    echo "GATE BROKEN: the control program compiled (rc=0) but wrote no object"
    echo "  file, so 'no object file' below cannot distinguish anything."
    exit 1
fi

# ── THE SUBJECT ─────────────────────────────────────────────────────────────
SUBJ="$FAILDIR/mlir_gen_unsupported_cast_fail.logos"
if [ ! -f "$SUBJ" ]; then
    echo "GATE BROKEN: $SUBJ does not exist."
    exit 1
fi
OBJ="$TMPD/subject.o"
set +e
"$LOGOSC" "$SUBJ" -o "$OBJ" >"$TMPD/s.out" 2>"$TMPD/s.err"
subj_rc=$?
set -e

if [ "$subj_rc" -eq 0 ]; then
    echo "FAIL: logosc exited 0 on a program whose cast mlir-gen cannot lower."
    echo "      That is the defect this gate exists for: an error was printed and"
    echo "      success was returned. stderr was:"
    cat "$TMPD/s.err"
    rc_gate=1
fi

if [ -e "$OBJ" ]; then
    echo "FAIL: an object file was written ($(stat -c%s "$OBJ") bytes) for a"
    echo "      program mlir-gen self-diagnosed on. Whatever the cast fed was"
    echo "      not emitted, so this object is a miscompile on disk."
    rc_gate=1
fi

# ⚠ Materialise, then match. Under `pipefail` a `grep -q` that exits at the
# first hit turns the producer's SIGPIPE into the pipeline's status.
if ! grep -qF -- "mlir_gen: internal:" "$TMPD/s.err"; then
    echo "FAIL: stderr carries no 'mlir_gen: internal:' line, so the report did"
    echo "      not go through the R2 sink (bugs_/bug_null) — a raw fprintf that"
    echo "      merely looks like a diagnostic cannot fail the compile. stderr:"
    cat "$TMPD/s.err"
    rc_gate=1
fi
if ! grep -qF -- "self-diagnosed malfunction(s) — COMPILE FAILED" "$TMPD/s.err"; then
    echo "FAIL: the R2 enforcement line is absent, so the compile failed for some"
    echo "      OTHER reason and this gate would be certifying the wrong event."
    cat "$TMPD/s.err"
    rc_gate=1
fi

# ═══════════════════════════════════════════════════════════════════════════
# PART 2 — THE POPULATION, NOT THE SPECIMEN  (#103)
# ═══════════════════════════════════════════════════════════════════════════
#
# Part 1 above proves ONE program's malfunction reaches the exit code. That is
# a specimen. It says nothing about the other emission sites, and the other
# sites are where the class lived: MEASURED 2026-08-22 at f5897299, FORTY of the
# 53 `"mlir_gen: …"` emission points in src/compiler were raw
# `std::fprintf(stderr, …)` calls that bypassed `bugs_` entirely — they printed
# the compiler's own diagnosis that a store, a call or a whole field-drop chain
# had not been emitted, and then the compile exited 0 with the object on disk.
# Five pass fixtures were emitting such lines while green; one of them
# (vec_struct_homonym_stride_shapes) was a live miscompile — a user struct that
# shares a name with `logos.std.compiler.metaprog.Item` had EVERY field
# destructor skipped, 0 drops against the renamed control's 2.
#
# So this part pins the POPULATION, derived from the source every run:
#
#   * the roster of raw `mlir_gen:` emission sites, keyed by FILE + MESSAGE
#     PREFIX — never by line number, which a one-line edit invalidates while
#     still looking authoritative;
#   * a CELL letter for each, and the rule that the interesting cell is EMPTY:
#       I   — not a malfunction report at all (a trace, the tally line itself,
#             the ledger's own note, the where-to-look pointer, the sink);
#       II  — already fatal at the site (returns nullptr / aborts);
#       III — a real condition that legitimately CONTINUES, spelled `warning:`
#             with its ground at the site;
#       IV  — a raw malfunction report after which the compile continues.
#             ⚠ CELL IV MUST BE EMPTY. A site that belongs there routes through
#             `bug*()` instead, which counts it and fails the compile.
#
# A NEW raw site therefore reds this gate at birth, and the author has exactly
# two honest moves: route it through the sink, or add it to the roster with its
# cell and the ground for that cell written at the site. There is deliberately
# no third.
#
# WHY THIS LIVES HERE AND NOT IN A NEW FILE. A second gate asserting a
# neighbouring property is how one class grows two ledgers that disagree — the
# `mlir_gen_bug.ledger` header records that happening once already. The exit
# code, the sink and the roster are three faces of one property: a
# self-diagnosed malfunction must not reach a successful exit. One file.
#
# ⚠ AND IT FAILS LOUD (exit 2) WHEN IT CANNOT MEASURE. A roster gate whose
# extraction silently returns nothing is a gate that certifies an empty tree
# green. Zero measured sites is IMPOSSIBLE (the sink's own `internal: %s` is
# always one of them), so it is reported as a broken instrument, not a pass.

SRC="${3:-}"
if [ -z "$SRC" ]; then
    SRC="$(cd "$(dirname "$0")/../../src/compiler" 2>/dev/null && pwd)" || true
fi
if [ ! -d "$SRC" ]; then
    echo "GATE BROKEN: compiler source dir not found (arg 3 = '${3:-<unset>}')."
    echo "  Nothing below measures anything. Pass \$CMAKE_SOURCE_DIR/src/compiler."
    exit 2
fi

# The extraction. One awk, so the pinned roster below and the measured roster
# are produced by the SAME function — a pin written by a different tool than the
# measurement is a pin that drifts on formatting.
measure_roster() {
    grep -rH '"mlir_gen: ' "$SRC"/mlir_gen*.cpp "$SRC"/mlir_gen*.hpp 2>/dev/null \
    | awk -F: '
    {
      file=$1; sub(/^.*\//,"",file);
      line=$0; sub(/^[^:]*:/,"",line);
      if (line ~ /^[[:space:]]*\/\//) next;          # a mention in a comment
      i=index(line,"\"mlir_gen: ");
      if (i==0) next;
      rest=substr(line,i+11);
      j=index(rest,"\""); k=index(rest,"\\");
      end=length(rest)+1;
      if (j>0 && j<end) end=j;
      if (k>0 && k<end) end=k;
      printf "%s|%s\n", file, substr(rest,1,end-1);
    }' | sort
}

# ── THE PIN ─────────────────────────────────────────────────────────────────
# `<cell>|<file>|<message prefix>`. Kept sorted by file|prefix.
PINNED_WITH_CELLS=$(cat <<'ROSTER'
II |mlir_gen.cpp|could not write %s
I  |mlir_gen.cpp|lazy reach = %zu/%zu kept (%zu pruned)
I  |mlir_gen.cpp|LEDGERED (%s) — compile CONTINUES; the ledger suspends
II |mlir_gen.cpp|module verification failed
I  |mlir_gen.cpp|module written to %s (%zu lines) — 
III|mlir_gen.cpp|warning: undefined '%s'
III|mlir_gen.cpp|warning: undefined '%s'
I  |mlir_gen.cpp|%zu functions total, %zu binary-skip
I  |mlir_gen.cpp|%zu self-diagnosed malfunction(s) — COMPILE FAILED
I  |mlir_gen_expr.cpp|method '%s' not found
II |mlir_gen_expr.cpp|unresolved GenericRef '%.*s' — mono failed to substitute its type-args
III|mlir_gen_expr.cpp|warning: undefined '%s' in fn '%s'
I  |mlir_gen_impl.hpp|internal: %s
I  |mlir_gen_impl.hpp|LOGOS_MLIRGEN_BUG_LEDGER='%s' cannot be opened —
III|mlir_gen_impl.hpp|warning: metaprog round — deferred, not refused: %s
I  |mlir_gen_stmt.cpp|transparent-audit: block in '%s' matches the retired 
III|mlir_gen_types.cpp|warning: no MLIR type for AssocType '%s::%s::%s' —
III|mlir_gen_types.cpp|warning: no MLIR type for ConstVar '%s' —
III|mlir_gen_types.cpp|warning: no MLIR type for InferredType '_' —
III|mlir_gen_types.cpp|warning: no MLIR type for TypeVar '%s' —
ROSTER
)
# Per-row ground, one line each. Cell I = not a report; II = fatal at the site;
# III = continues on purpose, ground written at the site and MEASURED:
#   mlir_gen.cpp  warning: undefined '%s'          — get_struct_ptr / get_subscript_ptr;
#       0 firings over the 2275-fixture pass corpus AND over the whole stdlib
#       build (2026-08-22). Un-guarded rather than deleted: the arm's `return
#       nullptr` is still reachable, and deleting the only REPORT of a
#       malfunction is the one direction #103 forbids.
#   mlir_gen_expr.cpp warning: undefined '%s' in fn — stale VarRefs from mono's
#       void-payload specs; 8 firings across 4 GREEN fixtures, measured with the
#       old env guard forced on.
#   mlir_gen_impl.hpp warning: metaprog round       — struct_reg_fail; a metaprog
#       fixpoint round is a SNAPSHOT (#61) and the FINAL gen re-measures. In all
#       four fixtures that emit it the final round answers clean, 0 lines.
#   mlir_gen_types.cpp warning: no MLIR type for …  — logos_to_mlir is a TOTAL
#       QUERY; nullptr is its ANSWER, and the caller that needed a type reports
#       (register_struct now routes through struct_reg_fail → the sink).
# Cell I `method '%s' not found` — the decision is at the STATEMENT level via
#       method_lower_misses_ / last_method_miss_, so this line is a debug aid
#       over an already-instrumented channel, not the report itself.

PINNED=$(printf '%s\n' "$PINNED_WITH_CELLS" | sed -E 's/^[^|]*\|//' | sort)
MEASURED=$(measure_roster)

n_measured=$(printf '%s\n' "$MEASURED" | grep -c '|' || true)
if [ "$n_measured" -eq 0 ]; then
    echo "GATE BROKEN: the roster extraction measured ZERO raw 'mlir_gen:' sites."
    echo "  That is impossible while the R2 sink exists — its own"
    echo "  \"mlir_gen: internal: %s\" is one of them. The extraction, not the"
    echo "  compiler, is what changed. SRC='$SRC'"
    exit 2
fi

n_pinned=$(printf '%s\n' "$PINNED" | grep -c '|' || true)
if [ "$MEASURED" != "$PINNED" ]; then
    echo "FAIL: the raw 'mlir_gen:' emission roster is not this tree's."
    echo "      pinned $n_pinned sites, measured $n_measured."
    echo "      --- only in the tree (NEW raw sites) ---"
    comm -13 <(printf '%s\n' "$PINNED") <(printf '%s\n' "$MEASURED") | sed 's/^/      + /'
    echo "      --- only in the pin (site removed or reworded) ---"
    comm -23 <(printf '%s\n' "$PINNED") <(printf '%s\n' "$MEASURED") | sed 's/^/      - /'
    echo
    echo "      A raw fprintf that says 'mlir_gen: …' and then lets the compile"
    echo "      continue is CELL IV, and cell IV must stay EMPTY: that shape"
    echo "      printed the compiler's own diagnosis of a dropped store and"
    echo "      exited 0, invisible to every pass fixture by construction."
    echo "      Route it through bug()/bug_printf()/bug_null() — which counts it"
    echo "      and fails the compile — or, if it genuinely continues, spell it"
    echo "      'warning:', write the ground AT THE SITE, and add the row above"
    echo "      with its cell letter. There is no third move."
    rc_gate=1
fi

# ⚠ CELL IV IS EMPTY BY CONSTRUCTION OF THE PIN, so assert that the pin itself
# still says so — a future edit that adds a `IV |…` row would otherwise slip the
# whole property through the equality check above.
# ⚠ Materialise, then match — same lesson as the stderr checks above: `grep -q`
# exits at the first hit, the writer takes SIGPIPE, and pipefail reports the
# MATCH as a failed pipeline.
printf '%s\n' "$PINNED_WITH_CELLS" > "$TMPD/roster_cells.txt"
if grep -qE '^IV[[:space:]]*\|' "$TMPD/roster_cells.txt"; then
    echo "FAIL: the pinned roster now carries a CELL IV row — a raw malfunction"
    echo "      report after which the compile continues. That cell is the defect;"
    echo "      it cannot be pinned, only emptied."
    rc_gate=1
fi

# ── THE REPORTS THEMSELVES ARE COUNTED, because DELETING one was invisible ──
# This round's own verify planted `if (false)` around `gep_field`'s malfunction
# report and BOTH gates stayed green: the roster counts RAW `mlir_gen:` string
# literals, and a report routed through the sink has no such literal, so
# removing it moves nothing either gate reads. Deleting a malfunction report is
# the one direction this round declares forbidden, and it was the one direction
# nothing measured.
#
# So the SINK CALL SITES are pinned too — a count per file, derived from the
# source. A report deleted, commented out or guarded away moves its file's
# count and reds. The count is deliberately NOT a roster of messages: the text
# is already pinned by the raw roster above for the raw sites, and for sink
# sites the message is a format string that legitimately gets re-worded. What
# must not change silently is HOW MANY places can report.
# 2026-08-22, moved DELIBERATELY and in the ADDING direction: #104 gave the
# StringView capture arm a loud refusal where it previously built an invalid
# `ExtractValue` on a pointer and let the verifier core-dump the compiler. One
# new `bug_printf` in mlir_gen_expr.cpp, 14 -> 15. This is the census doing its
# job — it noticed a report appearing, which is the same mechanism that refuses
# one disappearing.
REPORT_PIN=$(cat <<'REPORTS'
mlir_gen.cpp 5
mlir_gen_expr.cpp 15
mlir_gen_impl.hpp 10
mlir_gen_stmt.cpp 15
mlir_gen_types.cpp 4
REPORTS
)
report_measure() {
    for f in "$SRC"/mlir_gen*.cpp "$SRC"/mlir_gen*.hpp; do
        [ -f "$f" ] || continue
        n=$(sed 's|//.*||' "$f" \
            | grep -o 'bug_printf(\|bug_raw(\|bug_null(\|struct_reg_fail(' \
            | grep -c . || true)
        [ "$n" -gt 0 ] && printf '%s %s\n' "$(basename "$f")" "$n"
    done | sort
}
REPORT_NOW=$(report_measure)
if [ -z "$REPORT_NOW" ]; then
    echo "FAIL(2): zero malfunction-report call sites found under $SRC."
    echo "         This compiler holds dozens; reading zero means the matcher"
    echo "         stopped matching, not that the reports went away."
    exit 2
fi
if [ "$REPORT_NOW" != "$REPORT_PIN" ]; then
    echo "FAIL: the malfunction-REPORT census moved."
    diff <(printf '%s\n' "$REPORT_PIN") <(printf '%s\n' "$REPORT_NOW") \
        | sed 's/^/         /' || true
    echo "      A report DELETED is the forbidden direction of this whole round:"
    echo "      the channel was too quiet, and making it quieter is the one move"
    echo "      that cannot be right. A report ADDED is fine — move the pin and"
    echo "      say what it reports. Neither may happen unnoticed."
    rc_gate=1
fi

# ── AND NEUTRALISATION, which the COUNT alone cannot see ────────────────────
# A report can be silenced without being deleted: leave the call spelled and
# guard it away. This round's verify did exactly that (`if (false) bug_printf(`)
# and the census above stays at 48 because the token is still there. So the
# constant-false guard is refused outright in these files. That is a blunt rule
# and it is meant to be: there is no legitimate reason for `if (false)` in
# compiler source, and the one thing this round must not allow is a malfunction
# report that is present, counted, and unreachable.
#
# ⚠ HONEST LIMIT, stated rather than left for the next verify: the census counts
# CALL SITES, so a report neutralised in some other way — a condition narrowed
# to unreachability, a caller that stops being called — still passes. What is
# held is deletion, addition, the matcher breaking, and the constant-false
# guard. Reachability is not a property a grep can decide.
NEUTRALISED=$(grep -nE 'if[[:space:]]*\([[:space:]]*(false|0)[[:space:]]*\)' \
                   "$SRC"/mlir_gen*.cpp "$SRC"/mlir_gen*.hpp 2>/dev/null || true)
if [ -n "$NEUTRALISED" ]; then
    echo "FAIL: a constant-false guard in mlir-gen source:"
    printf '%s\n' "$NEUTRALISED" | sed 's/^/         /'
    echo "      A malfunction report that is present, counted and unreachable is"
    echo "      the silencing this round exists to prevent — and the report"
    echo "      census cannot see it, because the call is still spelled."
    rc_gate=1
fi

if [ "$rc_gate" -eq 0 ]; then
    echo "mlir_gen exit-code gate: rc=$subj_rc, no object file, R2 sink reached;"
    echo "  control compiled clean (rc=0, object written);"
    echo "  roster: $n_measured raw 'mlir_gen:' sites, all in cells I/II/III, cell IV empty;"
    echo "  49 malfunction-report call sites over 5 files, unmoved."
fi
exit "$rc_gate"  # lint:exit-ok — `rc_gate` is set only to the literals 0 and 1
