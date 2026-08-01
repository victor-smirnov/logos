#!/usr/bin/env bash
# The mismatch-diagnostic MONOPOLY gate (coercion arc S2).
#
# The string "expected {}, got {}" is the type-mismatch verdict, and the ONE
# place allowed to emit it is SemaChecker::expect_type. A position that wants
# to reject an expression must route through the judgment — it may not
# re-implement the verdict. This lint makes that architectural rule a BUILD
# failure instead of a review hope: the sieve of per-site special cases grew
# back three times precisely because nothing mechanical stopped copy #36.
set -u
cd "$(dirname "$0")/.."
# Excluded: comment lines (documentation of the rule) and the variance
# diagnostic — "variance mismatch" is check_variance's own verdict class,
# not the type-mismatch verdict this gate protects.
# ⚠ THE POPULATION IS `src/`, NOT TWO FILENAME GLOBS. A monopoly gate whose
# scan is `src/compiler/sema*.{cpp,hpp}` reports "1 emitter" — green — for a
# tree in which mono, mlir-gen or the writ frontend emits the same template:
# the second emitter is outside the glob, so it is counted as zero. The
# question the gate asks is about the WHOLE compiler, so the scan is too.
# MEASURED 2026-08-01 at the widening: still exactly 1 emitter
# (src/compiler/sema_expr.cpp), which is what makes this a safe widening and
# not a re-baselining.
hits=$(grep -rn "expected {}, got {}" src include \
       | grep -vE "^\S+: *//|variance mismatch")
n=$(printf '%s' "$hits" | grep -c . || true)
if [ "$n" -gt 1 ]; then
    echo "lint: the mismatch verdict may only be emitted by expect_type; found $n emitters:"
    printf '%s\n' "$hits"
    exit 1
fi
if [ "$n" -lt 1 ]; then
    echo "lint: expect_type's own template not found — did the diagnostic change shape?"
    exit 1
fi
echo "lint: mismatch monopoly holds (1 emitter: expect_type)"
