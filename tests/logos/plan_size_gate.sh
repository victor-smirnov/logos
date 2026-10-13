#!/usr/bin/env bash
# plan_size_gate.sh LOGOSC TEST_LOGOS
#
# The PLAN's size record (ADR 0024 S4): a source that DECLARES a size operation is
# asked for one, and the answer's provenance is reported; a source that declares
# none is reported as unmeasured. Both halves, in one compile.
#
# ⚠ WHY A GATE AND NOT A PASS TEST. The fixture already runs and asserts its rows,
# and it would keep passing with the whole size plane deleted — nothing in it reads
# the number (its one reader is a prepared join order). So the only thing that
# can hold the property is the trace, which is the plan's own account of what it
# asked. `plan_trace` writes to STDERR under LOGOS_TRACE_PLAN=1 and appears in no
# artifact, exactly like the join-strategy trace before it.
#
# ⚠ AND BOTH HALVES ARE REQUIRED. A plan that reported only the sizes it obtained
# would leave "no line" meaning both "did not ask" and "asked and got none" — the
# failure `access_plan_explain` was written to avoid for the access decision. So
# the absence is asserted as a POSITIVE line, and the measured one must name the
# reporter: a plan that recorded the fact without recording where it came from
# cannot be audited.
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
# the plan asked, and says which reporter answers.
if ! grep -Eq '^\[plan\] m -> size on __ctr_size_[A-Za-z0-9_]+ ' "$TMPD/err"; then
    echo "FAIL: no measured-size trace for the container rel 'm'"
    fail=1
fi
if ! grep -q 'measured: the source reports its size, asked in `prepare` where a join order reads it and nowhere else' "$TMPD/err"; then
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

# ASKED ONLY WHERE READ (ADR 0026 F2). No query here has a join order to pick,
# so the emitted code calls the reporter NOWHERE: `_run` used to bind
# `__sz_<r>` and never read it (66 such bindings in the corpus).
shopt -s nullglob
# The user module emits one dump per metacall round, so every `test.*` dump is
# searched. (`logos.gen.*` dumps are the family's own and hold the reporter's
# DEFINITION.)
DUMPS=("$TMPD"/gen/test.*.gen.logos)
if [ "${#DUMPS[@]}" -lt 1 ]; then
    echo "FAIL: no test.*.gen.logos dump — the emitted side was not asserted"
    fail=1
else
    n=$(awk '/__ctr_size_[A-Za-z0-9_]*\(m\)/{c++} END{print c+0}' "${DUMPS[@]}")
    if [ "$n" -ne 0 ]; then
        echo "FAIL: the size reporter is called $n times with no reader (want 0):"
        grep -n '__ctr_size_' "${DUMPS[@]}" || true
        fail=1
    fi
fi

# THE ADMIT TWIN: a prepared join order reads the sizes. Every `__sz_<r>`
# binding of that fixture sits in a `_prepare` fn and is read there.
TWIN="$(dirname "$TEST_LOGOS")/wql_join_ndv_order_e2e.logos"
if ! "$LOGOSC" "$TWIN" --gen-dir "$TMPD/gen2" -o "$TMPD/twin.o" 2>"$TMPD/err2"; then
    echo "FAIL: logosc failed on $TWIN:"; cat "$TMPD/err2"; exit 1
fi
read -r nb np nr < <(awk '
    /^ *(pub )?fn [A-Za-z0-9_]+/ { fn = $0 }
    match($0, /let __sz_[a-z0-9_]+/) {
        v = substr($0, RSTART + 4, RLENGTH - 4); nb++
        if (fn ~ /_prepare\(/) np++
        want[v] = 1
    }
    { for (v in want) { t = $0; while (i = index(t, v)) { use[v]++; t = substr(t, i + length(v)) } } }
    END { r = 0; for (v in want) if (use[v] > 1) r++; print nb + 0, np + 0, r }' "$TMPD"/gen2/test.*.gen.logos)
if [ "$nb" -lt 1 ] || [ "$nb" -ne "$np" ] || [ "$nr" -lt 1 ]; then
    echo "FAIL: $TWIN — $nb size binding(s), $np in a _prepare fn, $nr name(s) read (want all in _prepare, read)"
    grep -n '__sz_' "$TMPD"/gen2/test.*.gen.logos || true
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
