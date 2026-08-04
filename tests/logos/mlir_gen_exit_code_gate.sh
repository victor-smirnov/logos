#!/usr/bin/env bash
# mlir_gen_exit_code_gate.sh LOGOSC FAIL_DIR
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

if [ "$rc_gate" -eq 0 ]; then
    echo "mlir_gen exit-code gate: rc=$subj_rc, no object file, R2 sink reached;"
    echo "  control compiled clean (rc=0, object written)."
fi
exit "$rc_gate"  # lint:exit-ok — `rc_gate` is set only to the literals 0 and 1
