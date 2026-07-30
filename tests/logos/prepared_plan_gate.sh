#!/usr/bin/env bash
# prepared_plan_gate.sh LOGOSC TEST_LOGOS
#
# THE PLAN IS A RUNTIME VALUE (ADR 0024 S4i). The fixture asserts behaviour — the
# rows, the facts, and that two plans drive different sides of the same data — but
# there are three properties it cannot see, and each of them is the difference
# between a prepared plan and a decorative one:
#
#   • the SURFACE EXISTS AND IS UNIFORM. Four items per query — plan type,
#     `prepare`, `run`, direct — for the query with a run-time choice AND for the
#     ones without. A compiler that emitted the triple only where it had something
#     to decide would pass every behavioural assertion and leave callers unable to
#     write one loop over the queries they hold.
#
#   • `prepare` DOES NOT RUN THE QUERY. Its body must contain no loop and no
#     materialization: a prepare that scanned a source would make `agrees` —
#     re-prepare and compare — cost as much as the query it is checking, and the
#     method would be advice nobody can take. This is the one property that is
#     invisible from the outside and expensive to lose.
#
#   • THE PER-ROW BODY IS EMITTED ONCE. The direct fn must be two statements
#     (prepare, then run), not a second copy of the nest. Two copies return the
#     same rows, so nothing behavioural notices; what notices is compile time and
#     code size, and the next person to change the emitter.
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

shopt -s nullglob
DUMPS=("$TMPD"/gen/test.*.gen.logos)
if [ "${#DUMPS[@]}" -lt 1 ]; then
    echo "FAIL: no test.*.gen.logos dump — the emitted side was not asserted"
    exit 1
fi

# ⚠ `grep -c` over many files exits non-zero on a zero total, which under
# `set -euo pipefail` would kill this script with no message — a gate that
# returns 1 while saying nothing. Guarded, and every count is printed on failure.
count() { grep -Ec "$1" "${DUMPS[@]}" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}' || true; }

# ── the surface, once per query in the fixture (by_key, no_order, evens) ────
want=3
for kind in 'pub struct [A-Za-z]+Plan \{' \
            '^impl [A-Za-z]+Plan \{' \
            '^pub fn [a-z_]+_prepare\(' \
            '^pub fn [a-z_]+_run\(__pl: &[A-Za-z]+Plan, '; do
    n=$(count "$kind")
    if [ "$n" -ne "$want" ]; then
        echo "FAIL: $n items matching /$kind/ (want $want, one per query)"
        fail=1
    fi
done

# The plan carries THE FACTS and the methods that read them — not just a bit.
for member in 'pub base_n: i64' 'pub step_n: i64' 'pub why: str' \
              'pub fn agrees\(&self, fresh: &[A-Za-z]+Plan\)' \
              'pub fn margin\(&self\)' 'pub fn explain\(&self\)'; do
    if ! grep -Eq "$member" "${DUMPS[@]}"; then
        echo "FAIL: the plan type has no /$member/ — a decision without its facts is not re-checkable"
        fail=1
    fi
done

# ── prepare measures and returns; it does not run ──────────────────────────
for f in "${DUMPS[@]}"; do
    grep -Eq '^pub fn [a-z_]+_prepare\(' "$f" || continue
    # ⚠ Anchored at STATEMENT position, not anywhere in the file: the plan's
    # recorded ground is a string literal in this very body and the prose in it
    # contains the word "for".
    if grep -Eq '^[[:space:]]*(while|loop|for)[[:space:](]' "$f"; then
        echo "FAIL: a prepare fn contains a loop — it would run the query it is meant to plan:"
        sed -n '/^pub fn /,$p' "$f"
        fail=1
    fi
    if ! grep -Eq 'return [A-Za-z]+Plan \{' "$f"; then
        echo "FAIL: a prepare fn does not return a plan: $f"
        fail=1
    fi
done

# ── the direct fn is prepare-then-run, and the body lives only in run ──────
n_direct=0
for f in "${DUMPS[@]}"; do
    grep -Eq '^pub fn (by_key|no_order|evens)\(' "$f" || continue
    n_direct=$((n_direct + 1))
    if ! grep -Eq '^    let __pl: [A-Za-z]+Plan = [a-z_]+_prepare\(' "$f"; then
        echo "FAIL: the direct fn does not prepare: $f"
        fail=1
    fi
    if ! grep -Eq '^    return [a-z_]+_run\(\(&__pl\)' "$f"; then
        echo "FAIL: the direct fn does not delegate to run: $f"
        fail=1
    fi
    if grep -Eq '^[[:space:]]*(while|loop|for)[[:space:](]' "$f"; then
        echo "FAIL: the direct fn carries a loop — the per-row body was emitted twice:"
        sed -n '/^pub fn /,$p' "$f"
        fail=1
    fi
done
if [ "$n_direct" -ne 3 ]; then
    echo "FAIL: $n_direct direct query fns in the dump (want 3)"
    fail=1
fi

# ── the decision reaches the one channel, per query, with its ground ───────
n_trace=$(grep -Ec '^\[plan\] [a-z_]+ -> prepared plan on [A-Za-z]+Plan ' "$TMPD/err" || true)
if [ "$n_trace" -ne 3 ]; then
    echo "FAIL: $n_trace prepared-plan trace lines (want 3, one per query)"
    grep '^\[plan\]' "$TMPD/err" || true
    fail=1
fi
if ! grep -q 'decided once in .prepare.' "$TMPD/err"; then
    echo "FAIL: the dynamic plan's ground does not say the decision was taken in prepare"
    fail=1
fi
if ! grep -q 'the access path is decided where the query compiles' "$TMPD/err"; then
    echo "FAIL: a plan with no run-time decision carries no ground for having none"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
