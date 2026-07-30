#!/usr/bin/env bash
# plan_size_gate.sh LOGOSC TEST_LOGOS
#
# The PLAN's size record (ADR 0024 S4): a source that DECLARES a size operation is
# asked for one, and the answer's provenance is reported; a source that declares
# none is reported as unmeasured. Both halves, in one compile.
#
# ⚠ WHY A GATE AND NOT A PASS TEST. The fixture already runs and asserts its rows,
# and it would keep passing with the whole size plane deleted — nothing consumes
# the number yet, by design (this phase supplies the fact). So the only thing that
# can hold the property is the trace, which is the plan's own account of what it
# asked. `plan_trace` writes to STDERR under LOGOS_TRACE_PLAN=1 and appears in no
# artifact, exactly like the join-strategy trace before it.
#
# ⚠ AND BOTH HALVES ARE REQUIRED. A plan that reported only the sizes it obtained
# would leave "no line" meaning both "did not ask" and "asked and got none" — the
# failure `access_plan_explain` was written to avoid for the access decision. So
# the absence is asserted as a POSITIVE line, and the measured one must name the
# reporter AND the binding the number lands in: a plan that recorded the fact
# without recording where it came from cannot be audited.
set -euo pipefail

LOGOSC="$1"
TEST_LOGOS="$2"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if ! LOGOS_TRACE_PLAN=1 "$LOGOSC" "$TEST_LOGOS" --gen-dir "$TMPD/gen" \
        -o "$TMPD/test.o" 2>"$TMPD/err"; then
    echo "FAIL: logosc failed:"; cat "$TMPD/err"; exit 1
fi

fail=0

# The MEASURED source: a generated container family declares `size entry = …`, so
# the plan asked, and says which reporter answered and where the number lands.
if ! grep -Eq '^\[plan\] m -> size on __ctr_size_[A-Za-z0-9_]+ -> __sz_m ' "$TMPD/err"; then
    echo "FAIL: no measured-size trace for the container rel 'm'"
    fail=1
fi
if ! grep -q 'measured: the source reports its size, read once before the first row' "$TMPD/err"; then
    echo "FAIL: the measured size carries no ground"
    fail=1
fi

# The UNMEASURED source declares no size operation. The plan must SAY so.
if ! grep -Eq '^\[plan\] s -> size unknown ' "$TMPD/err"; then
    echo "FAIL: no unmeasured-size trace for the rel 's', which declares no size"
    fail=1
fi
if ! grep -q 'unmeasured: the source declares no size operation' "$TMPD/err"; then
    echo "FAIL: the absent size carries no ground"
    fail=1
fi

# ASKED ONCE, and in the PRELUDE: the emitted fn obtains the number before the
# walk, so the call cannot be inside a loop. One call, one binding, in the
# generated source itself — the artifact, not an expectation about it.
shopt -s nullglob
# The user module emits one dump per metacall round, so the query's own dump is
# not a fixed name — every `test.*` dump is searched and the calls are SUMMED.
# (`logos.gen.*` dumps are the family's own and hold the reporter's DEFINITION.)
DUMPS=("$TMPD"/gen/test.*.gen.logos)
if [ "${#DUMPS[@]}" -lt 1 ]; then
    echo "FAIL: no test.*.gen.logos dump — the emitted side was not asserted"
    fail=1
else
    n=$(awk '/__ctr_size_[A-Za-z0-9_]*\(m\)/{c++} END{print c+0}' "${DUMPS[@]}")
    if [ "$n" -ne 1 ]; then
        echo "FAIL: the size reporter is called $n times in the emitted fn (want 1):"
        grep -n '__ctr_size_' "${DUMPS[@]}" || true
        fail=1
    fi
    if ! grep -Eqh '^ *let __sz_m: u64 = __ctr_size_[A-Za-z0-9_]+\(m\);' "${DUMPS[@]}"; then
        echo "FAIL: the emitted size binding is not the annotated prelude let:"
        grep -n '__sz_m' "${DUMPS[@]}" || true
        fail=1
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
