#!/usr/bin/env bash
# extract.sh <src.cpp> <outdir>
#
# Emit Datalog facts about PLACE WALKERS: functions that decompose a place
# expression (`a.b`, `t.0`, `v[i]`, `*p`) down to its root.
#
# ⚠ THE EXTRACTOR IS THE RISK, NOT THE RULES. Souffle saturates or it does not;
# it has nothing to lie with. THIS FILE does: garbage facts give confidently wrong
# answers, worse than no tool because the form is authoritative. selftest.sh runs
# the whole chain against revision 28fc7c75, where six defects are already known,
# and requires it to name exactly those six.
set -uo pipefail
OUT="${1:?usage: extract.sh <outdir> <src.cpp>...}"; shift
SRCS=("$@")
mkdir -p "$OUT"

# ── THE DOMAIN, DERIVED ─────────────────────────────────────────────────────
# ⚠ THE FIRST CUT LISTED FIVE NAMES I HAD TYPED — the detector for
# enumeration-instead-of-property, keyed on an enumeration. A walker was then
# certified complete against a domain that shared my blind spot exactly.
# The domain is now every expr::Code in the schema; what is NOT a projection is
# claimed in not_projection.claim and SUBTRACTED. A new code is a projection
# until someone says otherwise, so it arrives loud instead of absent.
SCHEMA="${LOGOS_LIR_SCHEMA:-include/logos/compiler/lir_schema.hpp}"
awk '/^namespace expr \{/ {inb=1} inb && /^inline constexpr/ {exit}
     inb && match($0, /^ *[A-Za-z_][A-Za-z0-9_]* *=/) {sub(/ *=.*/,""); gsub(/ /,""); print}' \
    "$SCHEMA" > "$OUT/expr_code.facts"
N=$(wc -l < "$OUT/expr_code.facts")
[ "$N" -ge 20 ] || { echo "EXTRACTOR: read $N expr codes from $SCHEMA — refusing" >&2; exit 3; }
grep -vE '^\s*(#|$)' "$(dirname "$0")/not_projection.claim" > "$OUT/not_projection.facts"
echo "VarRef" > "$OUT/place_root_kind.facts"

# ── THE WALKERS ─────────────────────────────────────────────────────────────
# Named explicitly: a function is a place walker because of what it MEANS, and
# no grep can decide that. The list is the claim; the rules check it.
# One name per line. A walker is one because of what it MEANS; no grep decides
# that. The list is the CLAIM, and the rules check it against the domain.
cat > "$OUT/walker.facts" <<'EOF'
try_path
extract_borrow_place
value_local_root
EOF
# ⚠ TWO NAMES WERE REMOVED FROM THIS LIST BY THE TOOL ITSELF, on its first run.
# `is_temporary_value_expr` and `collect_borrow_locals` yielded ZERO projection
# kinds — not an extractor bug, a wrong CLAIM: the first classifies expressions
# that PRODUCE A VALUE (LitInt, Call, BinOp) and the second walks value
# CONSTRUCTORS. Neither decomposes a place. The division is the point: the facts
# are mechanical, the walker list is a claim, and the rule checks the claim
# against the domain — so a wrong claim shows up as a wall of violations rather
# than passing quietly.

# ── handles(Walker, Kind) ───────────────────────────────────────────────────
# Body extraction by brace balance from the definition line. Crude, and its
# limits are measured rather than assumed: see selftest.sh, which pins the
# extracted body length of a known function.
: > "$OUT/handles.facts"
while read -r fn; do
    [ -n "$fn" ] || continue
    SRC=""; start=""
    for cand in "${SRCS[@]}"; do
        l=$(grep -nE "(^|[^A-Za-z_])${fn} *=|[A-Za-z_>] +${fn}\(" "$cand" | head -1 | cut -d: -f1)
        if [ -n "$l" ]; then SRC="$cand"; start="$l"; break; fi
    done
    if [ -z "$start" ]; then echo "EXTRACTOR: no definition found for '$fn'" >&2; continue; fi
    awk -v s="$start" -v fn="$fn" '
        NR < s { next }
        { line=$0
          n=gsub(/\{/,"{",line); m=gsub(/\}/,"}",line)
          depth += n - m; body = body "\n" $0
          if (started && depth <= 0) { print body; exit }
          if (n > 0) started=1 }
    ' "$SRC" > /tmp/.body.$$
    # ⚠ KEYED ON THE DOMAIN, NOT ON THE ALIAS. The first cut listed the aliases
    # it had seen — `EC::`, `Code::`, `ec::Code::` — and returned ZERO facts for
    # `is_temporary_value_expr`, which spells them `EK::`. The extractor built to
    # catch enumeration-instead-of-property committed it on its first run. Any
    # qualifier followed by a kind IN THE DOMAIN counts; the domain is the thing
    # we actually mean.
    ALT=$(paste -sd'|' "$OUT/expr_code.facts")
    grep -oE "::($ALT)\b" /tmp/.body.$$ | sed 's/^:://' | sort -u \
        | while read -r k; do echo -e "${fn}\t${k}"; done >> "$OUT/handles.facts"
    rm -f /tmp/.body.$$
done < "$OUT/walker.facts"
wc -l < "$OUT/handles.facts" | sed 's/^/handles facts: /'
