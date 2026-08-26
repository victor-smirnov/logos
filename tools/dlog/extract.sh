#!/usr/bin/env bash
# extract.sh <src.cpp> <outdir>
#
# Emit Datalog facts about PLACE WALKERS: functions that decompose a place
# expression (`a.b`, `t.0`, `v[i]`, `*p`) down to its root.
#
# ⚠ THE EXTRACTOR IS THE RISK, NOT THE RULES. Garbage facts give confidently
# wrong answers, which is worse than no tool because the form is authoritative.
# Every relation here is bite-proved in `selftest.sh` against a KNOWN answer.
set -uo pipefail
OUT="${1:?usage: extract.sh <outdir> <src.cpp>...}"; shift
SRCS=("$@")
mkdir -p "$OUT"

# ── THE DOMAIN ──────────────────────────────────────────────────────────────
# A PROJECTION is a step from a place to a sub-place. The set is not a matter of
# taste: it is every expression code that has a receiver whose value is a PLACE.
# Read from the schema so it cannot drift from it silently.
cat > "$OUT/projection_kind.facts" <<'EOF'
FieldRead
TupleIndex
IndexRead
SliceIndex
Deref
EOF
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
    grep -oE "::(FieldRead|TupleIndex|IndexRead|SliceIndex|Deref|VarRef)\b" /tmp/.body.$$ | sed 's/^:://' | sort -u \
        | while read -r k; do echo -e "${fn}\t${k}"; done >> "$OUT/handles.facts"
    rm -f /tmp/.body.$$
done < "$OUT/walker.facts"
wc -l < "$OUT/handles.facts" | sed 's/^/handles facts: /'
