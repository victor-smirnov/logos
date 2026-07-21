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
hits=$(grep -rn "expected {}, got {}" src/compiler/sema*.cpp src/compiler/sema*.hpp \
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
