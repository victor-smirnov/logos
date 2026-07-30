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
#     a declared one with its ground is not. And it must say WHICH absence: an axis
#     that was never entered is not an axis that proved orders and carried none
#     (ADR 0024 S4n), and `proved()` is the constant that separates them.
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
# ⚠ AND THE ABSENCE IS DATA — LITERALLY, NOW (ADR 0024 S4o). This rule used to grep
# a SENTENCE ('nothing was considered', then 'the order axis was never entered'), and
# each of those sentences was in turn found to be asserted where it was false: the
# first on every table-less plan including one that had proved eight orders, the
# second on 59 of 259 plans whose own ground said an anti join or a traversal had
# refused the axis. A gate that greps prose is an instance of the defect it is
# supposed to catch, so it asserts the RECORD: `axis()` is the state, emitted as a
# compile-time constant. `why::AX_NONE()` is 0 — and `wql_plan_census_e2e` pins that
# number against the function, in one run, so this literal cannot drift from it.
#
# ⚠ STRICTLY STRONGER, AND THE OLD COMMENT WAS WRONG. It said "both queries here
# have NO order axis at all". They do not: `evens` is one source (`AX_NONE`), and
# `no_order` is a JOIN whose axis was entered and refused for want of an `order by`
# (`AX_REFUSED`). The count was 2 either way, which is exactly how a sentence keyed
# on that count could assert "never entered" about a refusal for two rounds. Two
# table-less plans, one in EACH state, and neither number may absorb the other.
n_none=$(grep -A1 -h 'pub fn axis(&self)' "${DUMPS[@]}" 2>/dev/null | grep -c 'return 0i32;' || true)
if [ "$n_none" -ne 1 ]; then
    echo "FAIL: $n_none plans report axis() == AX_NONE (want 1 — evens, one source, no join)"
    grep -En -A1 'fn axis' "${DUMPS[@]}" || true
    fail=1
fi
n_ref=$(grep -A1 -h 'pub fn axis(&self)' "${DUMPS[@]}" 2>/dev/null | grep -c 'return 1i32;' || true)
if [ "$n_ref" -ne 1 ]; then
    echo "FAIL: $n_ref plans report axis() == AX_REFUSED (want 1 — no_order, a join with no \`order by\`)"
    grep -En -A1 'fn axis' "${DUMPS[@]}" || true
    fail=1
fi
# …and the ground names WHICH antecedent refused it: `why::WG_NO_SORT()` is 13.
n_g13=$(grep -A1 -h 'pub fn ground(&self)' "${DUMPS[@]}" 2>/dev/null | grep -c 'return 13i32;' || true)
if [ "$n_g13" -ne 1 ]; then
    echo "FAIL: $n_g13 plans record ground() == WG_NO_SORT (want 1 — no_order)"
    grep -En -A1 'fn ground' "${DUMPS[@]}" || true
    fail=1
fi
# The census is on the OBJECT as a compile-time constant, not only in the prose: a
# table-less plan whose axis was never entered reports `proved()` 0. `q5` in
# `wql_join_order_multi_e2e` is the contrasting artifact (proved 8, carried none) and
# `join_order_multi_gate.sh` pins it there.
n_pv0=$(grep -A1 -h 'pub fn proved(&self)' "${DUMPS[@]}" 2>/dev/null | grep -c 'return 0i64;' || true)
if [ "$n_pv0" -lt 2 ]; then
    echo "FAIL: $n_pv0 plans emit proved() == 0 (want >= 2 — no_order and evens never entered the axis)"
    grep -En 'fn proved' "${DUMPS[@]}" || true
    fail=1
fi
# …and `enumerated() == 0` is what makes that "never entered" rather than "entered and
# refused everything", which reports the same `proved()`.
n_en0=$(grep -A1 -h 'pub fn enumerated(&self)' "${DUMPS[@]}" 2>/dev/null | grep -c 'return 0i64;' || true)
if [ "$n_en0" -lt 2 ]; then
    echo "FAIL: $n_en0 plans emit enumerated() == 0 (want >= 2 — no permutation was walked for either)"
    grep -En 'fn enumerated' "${DUMPS[@]}" || true
    fail=1
fi
# ⚠ NO PIPE INTO `grep -q` HERE. Under `pipefail` the producer takes SIGPIPE (141)
# and the whole condition reads FALSE — a gate that cannot fire. The count is a
# variable, and the test is arithmetic.
n_old=$(count 'no candidate table: nothing was considered')
if [ "$n_old" -ne 0 ]; then
    echo "FAIL: $n_old plans carry the undifferentiated 'nothing was considered' suffix"
    fail=1
fi
# ⚠ AND THE SUFFIX THAT REPLACED IT IS GONE TOO. 'the order axis was never entered'
# was appended whenever `nperm == 0`, which is ALSO true of every chain the axis
# entered and refused before enumerating — measured at 59 of 259 plans. No emitter
# composes a census sentence any more; `why::why_render` selects one from `why_axis`,
# so the state and the ground come out of one record.
n_old2=$(count 'no candidate table: the order axis was never entered')
if [ "$n_old2" -ne 0 ]; then
    echo "FAIL: $n_old2 plans carry the count-keyed 'axis was never entered' suffix"
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
# The trace and `explain()` come from ONE RENDERER over ONE RECORD, so the channel
# says what the object says — including about the table it does not carry, and
# including WHICH of the three census states it is in.
if ! grep -q 'ORDER AXIS NOT ENTERED (`axis()` 0)' "$TMPD/err"; then
    echo "FAIL: the trace does not report that a candidate-less plan never entered the axis"
    fail=1
fi
# ⚠ AND THE TWO CHANNELS ARE COMPARED, NOT EYEBALLED. Every `[plan] <q> -> prepared
# plan on <T> (...)` payload must be EXACTLY the `why:` literal of `<T>`'s own plan
# struct. Before the record, the emitter appended a census suffix to its own copy of
# a ground the decision had composed, and the join-order verdict line then said
# something else again — 66 of 81 verdict lines disagreed with the plan the same
# decision produced.
n_cmp=0
while IFS= read -r line; do
    ty=$(printf '%s' "$line" | sed -n 's/^\[plan\] [^ ]* -> prepared plan on \([A-Za-z0-9_]*\) .*/\1/p')
    [ -n "$ty" ] || continue
    payload=$(printf '%s' "$line" | sed -n 's/^\[plan\].*   (\(.*\))$/\1/p')
    lit=$(grep -h -o "return ${ty} {[^\"]*why: \"[^\"]*\"" "${DUMPS[@]}" 2>/dev/null \
          | sed -n 's/.*why: "\(.*\)"$/\1/p' | head -1)
    if [ -z "$lit" ]; then
        echo "FAIL: no plan literal found for ${ty} — the trace names a plan the artifact does not"
        fail=1
        continue
    fi
    n_cmp=$((n_cmp + 1))
    if [ "$payload" != "$lit" ]; then
        echo "FAIL: ${ty}'s trace line and its explain() differ"
        echo "  trace: $payload"
        echo "  plan : $lit"
        fail=1
    fi
done < <(grep '^\[plan\] [a-z_0-9]* -> prepared plan on ' "$TMPD/err" || true)
# ⚠ A LOOP THAT COMPARED NOTHING IS NOT A GATE. This fixture emits three prepared
# surfaces; a regex that stopped matching would turn "no mismatch" into "no
# comparison" and the rule would pass forever.
if [ "$n_cmp" -ne 3 ]; then
    echo "FAIL: compared $n_cmp trace lines against plan literals (want 3 — by_key, no_order, evens)"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
