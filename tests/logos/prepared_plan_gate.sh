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
#
#   • A PLAN WITH NO CANDIDATES CARRIES NO CANDIDATE TABLE (ADR 0024 S4l). The
#     fixture cannot see a field it never reads. S4k put `pub tbl: JCTable` — 272
#     bytes — on EVERY plan and made every fixed `prepare` call `jc_table_none()` to
#     fill it: measured at -O2, the `evens` direct fn's frame went 32 → 312 bytes and
#     its call count 1 → 2, per invocation, for a field the query cannot read. So the
#     field must appear on the ONE plan here that has candidates and on neither of
#     the other two, `jc_table_none` must not appear at all, and the plans without a
#     table must SAY they have none — an absence that is not recorded is the defect,
#     a declared one with its ground is not.
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

# ── the candidate table is carried ONLY where there are candidates ─────────
# `by_key` has an `order by` and two admissible orders; `no_order` and `evens` have
# none. One table field, therefore, not three — and no `jc_table_none()` anywhere:
# the empty set is the ABSENCE of a table, not a table full of zeros fetched by an
# out-of-line call on every invocation.
n_tbl=$(count 'pub tbl: JCTable,')
if [ "$n_tbl" -ne 1 ]; then
    echo "FAIL: $n_tbl plan types carry 'pub tbl: JCTable' (want 1 — only by_key has candidates)"
    grep -En 'pub (tbl|struct [A-Za-z]+Plan)' "${DUMPS[@]}" || true
    fail=1
fi
n_none=$(count 'jc_table_none')
if [ "$n_none" -ne 0 ]; then
    echo "FAIL: $n_none uses of jc_table_none in the emitted code — a plan with no candidates must carry no table, not an empty one (272 bytes and a PLT call per invocation)"
    grep -En 'jc_table_none' "${DUMPS[@]}" || true
    fail=1
fi
# The plan that HAS a table still builds it in `prepare` and decides through the one
# cost function — the saving must not have cost the decision.
if ! grep -Eq 'let __tb: JCTable = JCTable \{' "${DUMPS[@]}"; then
    echo "FAIL: no plan builds a JCTable — the query with candidates lost its table"
    fail=1
fi
if ! grep -Eq 'jc_order_pick\(\(&__tb\), __n0,' "${DUMPS[@]}"; then
    echo "FAIL: the prepared decision does not go through join_cost::jc_order_pick"
    fail=1
fi
# ⚠ AND THE ABSENCE IS DATA. Both table-less plans record it in the ground they
# carry, so `considered() == 0` reads as "there was no table" and not as "a table
# that answered nothing".
n_abs=$(count 'no candidate table: nothing was considered')
if [ "$n_abs" -ne 2 ]; then
    echo "FAIL: $n_abs plans record having no candidate table (want 2 — no_order and evens)"
    fail=1
fi
# ⚠ A FIXED PLAN NAMES ORDER 0 AND MEANS IT (ADR 0024 S4m). The deferred half now
# writes `order_ix: -1i64` — "no order is held, `run` names one" — and the two
# absences must not be spelled the same way: a fixed plan HAS an order and it is the
# query's own. This is the contrast that keeps -1 meaningful; the deferred side of it
# is `deferred_plan_gate.sh`.
n_fix0=$(count 'dyn_order: false, defer_order: false, swap: false, order_ix: 0i64')
if [ "$n_fix0" -lt 1 ]; then
    echo "FAIL: no fixed plan records order 0 — a plan with one order holds it, and -1 would say it does not"
    grep -En 'order_ix: ' "${DUMPS[@]}" || true
    fail=1
fi
n_fixneg=$(count 'defer_order: false, swap: false, order_ix: \(-1i64\)')
if [ "$n_fixneg" -ne 0 ]; then
    echo "FAIL: $n_fixneg fixed plan(s) claim no order is held — -1 is the DEFERRED half's answer"
    fail=1
fi

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
# The trace and `explain()` are composed from ONE buffer, so the channel says what
# the object says — including about the table it does not carry.
if ! grep -q 'no candidate table: nothing was considered' "$TMPD/err"; then
    echo "FAIL: the trace does not report that a candidate-less plan carries no table"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
