#!/usr/bin/env bash
# wql_shadowed_column_gate.sh LOGOSC TEST_LOGOS
#
# A COLUMN'S TYPE IS A FACT ABOUT A BINDING (row var, column), NOT ABOUT A FIELD
# NAME. The type dictionary keyed columns on the bare spelling, so two joined
# sources declaring the same column name overwrote each other's type.
#
# The fixture asserts the ROWS, and the rows catch the emit half only by luck:
# the surviving type reached `Vec__push` and the host compiler refused it. What
# the fixture cannot see is the two things this gate holds:
#
#   • THE EMITTED ELEMENT TYPE, as TEXT. `q_ai` selects an i64 column and
#     `q_bu` a u64 one off sources that both declare `n`; before the fix BOTH
#     generated fns returned `Vec<u64>` — whichever source was stamped last. A
#     rows assertion on a build that does not link says nothing.
#   • THE PLANNER'S DECISION, which is the SERIOUS half and produced no error at
#     all. The planner reads the same dictionary to type the `order by` key, so
#     `P { k: f64 }` joined with `Q { k: i64 }` had its f64 key re-typed i64 and
#     the reorder axis was GRANTED — the licence `24141965` (C1) exists to
#     refuse. Only a downstream type error stopped that artifact; a query whose
#     types agreed would have shipped a nest chosen over a partial order.
#
# ⚠ THE CONTROL IS THE POINT. Query `q_g` sorts the SAME two sources by the
# genuinely-i64 key and must still be SEARCHED. A gate that only asserted the
# refusal would pass on a planner that had stopped reordering altogether.
#
# ⚠ ATTRIBUTION IS BY ROW VAR (`plan_trace` names a decision by its base var), so
# each query in the fixture owns a unique one: `a`/`c` for the i64-vs-u64 pair,
# `f` for the refused f64 key, `p` for the i64 control.
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

# ── THE EMITTED ELEMENT TYPE, PER QUERY ────────────────────────────────────
# `q_ai` projects A.n (i64); `q_bu` projects B.n (u64). Both sources declare
# `n`, and the collision used to give both queries the type of whichever source
# the emitter stamped last.
if ! grep -qE '^pub fn q_ai_run\(.*\) -> Result<Vec<i64>, ElError> \{' "${DUMPS[@]}"; then
    echo "FAIL: q_ai_run does not return Vec<i64> — A.n's i64 did not survive B.n's stamping"
    grep -hE '^pub fn q_ai_run' "${DUMPS[@]}" || echo "  (no q_ai_run emitted at all)"
    fail=1
fi
if ! grep -qE '^pub fn q_bu_run\(.*\) -> Result<Vec<u64>, ElError> \{' "${DUMPS[@]}"; then
    echo "FAIL: q_bu_run does not return Vec<u64> — B.n's u64 did not survive A.n's stamping"
    grep -hE '^pub fn q_bu_run' "${DUMPS[@]}" || echo "  (no q_bu_run emitted at all)"
    fail=1
fi
# The NESTED path: `x.b.c` has no row var, so it is keyed on its base's declared
# TYPE. `Other.c` is an i64 on the joined source and used to re-type it.
if ! grep -qE '^pub fn q_n_run\(.*\) -> Result<Vec<u64>, ElError> \{' "${DUMPS[@]}"; then
    echo "FAIL: q_n_run does not return Vec<u64> — Inner.c's u64 lost to Other.c's i64 on a nested path"
    grep -hE '^pub fn q_n_run' "${DUMPS[@]}" || echo "  (no q_n_run emitted at all)"
    fail=1
fi

# ── THE DECISION: an f64 key is refused even when a same-named i64 exists ───
if ! grep -q 'ORDER AXIS ENTERED AND REFUSED BEFORE ENUMERATION' "$TMPD/err"; then
    echo "FAIL: no order-axis refusal at all — the f64 key was licensed"
    fail=1
fi
if ! grep -qE '^\[plan\] f -> ' "$TMPD/err"; then
    echo "FAIL: query q_f (base var 'f') reported no decision — the trace is not attributable"
    fail=1
fi
# The refusal must be q_f's, must name the KEY TYPE that failed, and must have
# enumerated nothing.
if ! awk '/^\[plan\] f -> /' "$TMPD/err" | grep -q 'This key (`f64`) fails it'; then
    echo "FAIL: q_f's decision does not name the f64 key as the failed antecedent:"
    awk '/^\[plan\] f -> /' "$TMPD/err" | cut -c1-200
    fail=1
fi
if awk '/^\[plan\] f -> /' "$TMPD/err" | grep -q 'ORDER AXIS SEARCHED'; then
    echo "FAIL: q_f SEARCHED the order axis over an f64 key — the i64 column named 'k' on the"
    echo "      other source was read as this key's type"
    fail=1
fi

# ── THE CONTROL: the same two sources, i64 key, axis SEARCHED ──────────────
if ! grep -qE '^\[plan\] p -> order search on r' "$TMPD/err"; then
    echo "FAIL: q_g (base var 'p') did not search the order axis — the refusal above is about"
    echo "      these SOURCES rather than about the f64 key, which is not the claim"
    awk '/^\[plan\] p -> /' "$TMPD/err" | cut -c1-200
    fail=1
fi

# ── And the colliding-name pair still reorders (their keys are orderable) ──
for v in 'a -> order search on b' 'c -> order search on d'; do
    if ! grep -qE "^\[plan\] ${v}" "$TMPD/err"; then
        echo "FAIL: expected '[plan] ${v}' — a query whose columns collide by NAME lost its axis"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then exit 1; fi
echo "PASS: column types and the order-axis licence key on the BINDING, not the field name"
