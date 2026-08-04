#!/usr/bin/env bash
# root_coord_lint.sh — the virtual-root COORDINATE has exactly one definition,
# and every site that writes or reads it must consume that definition.
#
# WHY. `WG_ROOT_PARENT/KEY/IDX/ORD` (stdlib/mem/wql/writ_graph.logos) collapsed
# four hand-written spellings of one rule into one. That collapsed the VALUE.
# It did not collapse the MEMBERSHIP: nothing stopped a fifth producer, or a
# second reader, from open-coding `0i64, "", -1i64` again — and the failure mode
# is silent. A producer and the reader disagreeing by one literal makes
# `from g .field …` compile and answer ZERO ROWS. No test sees it, because a
# test that asserts rows over a document the producer never emitted a root for
# has nothing to compare against.
#
# This is the same lesson as separator_split_lint.sh, and it is the lesson the
# whole arc keeps re-learning: A GATE'S POPULATION IS DERIVED FROM THE ARTIFACT,
# OR IT CERTIFIES WHATEVER SOMEBODY LISTED. Before this file, the site set was a
# four-bullet PROSE COMMENT. Prose does not fail a build.
#
# WHAT IT COUNTS: occurrences of the root-idx literal `-1i64` and of the
# root-parent comparison against a bare `0i64` inside the three packages that
# carry this vocabulary, per file, against a ledger — in BOTH directions, so the
# ledger moves only by deliberate edit. Comments are stripped first: a site that
# EXPLAINS the rule must not read as a violation of it, or the incentive becomes
# to document less.
#
# ⚠ WHAT THIS DOES NOT COVER, stated so the census is not read as total:
#   * `-1i64` is not unique to this rule — a not-found sentinel is spelled the
#     same way. That is exactly why this is a LEDGER and not a prohibition: the
#     legitimate uses are counted, and a NEW one has to be looked at by a human
#     who then either routes it through the constants or writes it down.
#   * only the two literals that have actually drifted are counted. `""` as a
#     root key and `0i64` as a root ord are unspellable to a lint — they are the
#     most common literals in the language.
#   * a site OUTSIDE these three packages is invisible. The vocabulary is
#     package-scoped by construction (the constants are `pub` and imported), so
#     an outside writer would have to reimplement the relation, which is a
#     different defect with a different name.
set -u

ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
LEDGER="$(dirname "$0")/root_coord.ledger"
FAIL=0

# The packages that carry the graph-edge vocabulary. Derived from the constant's
# own consumers, not listed by hand: whoever imports writ_graph is in scope.
SCOPE=(
  "$ROOT/stdlib/mem/wql"
  "$ROOT/stdlib/mem/deem"
  "$ROOT/stdlib/mem/compiler/metaprog"
)

strip_comments() { sed 's://.*$::' "$1"; }

count_file() {
  # The COORDINATE, not its parts. `-1i64` alone is also the not-found sentinel
  # and appears dozens of times legitimately; counting it would make this lint
  # cry wolf, and a lint that cries wolf gets disabled — which is the defect one
  # level up. What is unambiguous is the TRIPLE: a root parent, an empty key and
  # the root idx written adjacently. Whitespace is collapsed first so a call
  # broken across lines counts the same as one written on a single line.
  local n1 n2 flat
  flat=$(strip_comments "$1" | tr '\n' ' ' | tr -s ' ')
  n1=$(printf '%s' "$flat" | grep -o -- '0i64 *, *"" *, *0i64 - 1i64\|0i64 *, *"" *, *-1i64' | wc -l)
  # …and the READER's anchor: `parent` compared against a bare integer literal.
  n2=$(printf '%s' "$flat" | grep -o -- '"parent"[^)]*) *, *make_int_lit([^,]*, *[0-9-]' | wc -l)
  echo $(( n1 + n2 ))
}

declare -A SEEN=()
while IFS= read -r f; do
  rel="${f#"$ROOT"/}"
  n=$(count_file "$f")
  [ "$n" -eq 0 ] && continue
  SEEN["$rel"]=$n
done < <(find "${SCOPE[@]}" -name '*.logos' -type f 2>/dev/null | sort)

# ── the ledger, held in BOTH directions ────────────────────────────────────
declare -A WANT=()
while read -r want rel; do
  case "$want" in ''|'#'*) continue ;; esac
  WANT["$rel"]=$want
done < "$LEDGER"

for rel in "${!SEEN[@]}"; do
  got=${SEEN[$rel]}
  want=${WANT[$rel]:-0}
  if [ "$got" -ne "$want" ]; then
    echo "::error:: $rel: $got root-coordinate literal(s), ledger says $want."
    echo "          If this is a NEW site of the root-row rule, it must consume"
    echo "          WG_ROOT_PARENT/KEY/IDX/ORD from logos.std.wql.writ_graph"
    echo "          instead of spelling the coordinate. If it is an unrelated"
    echo "          use of the same literal, add it to $LEDGER deliberately."
    FAIL=1
  fi
done
for rel in "${!WANT[@]}"; do
  if [ -z "${SEEN[$rel]:-}" ]; then
    echo "::error:: $rel: ledger says ${WANT[$rel]} root-coordinate literal(s), found none."
    echo "          A count that drifts down on its own is a measurement nobody"
    echo "          is reading. Update the ledger in the same commit."
    FAIL=1
  fi
done

# ── SELF-CANARIES. A lint that cannot fail is the defect one level up ──────
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/stdlib/mem/wql"
cat > "$tmp/stdlib/mem/wql/canary.logos" <<'CANARY'
fn f() { out.push((0i64, "", -1i64, id)); }
CANARY
c1=$(count_file "$tmp/stdlib/mem/wql/canary.logos")
if [ "$c1" -lt 1 ]; then
  echo "::error:: SELF-CANARY 1 FAILED: an open-coded root idx counted $c1, expected >= 1."
  echo "          The counter is blind; every green result above is meaningless."
  FAIL=1
fi
cat > "$tmp/stdlib/mem/wql/canary2.logos" <<'CANARY'
// out.push((0i64, "", -1i64, id));   -- explained, not written
fn f() { g(); }
CANARY
c2=$(count_file "$tmp/stdlib/mem/wql/canary2.logos")
if [ "$c2" -ne 0 ]; then
  echo "::error:: SELF-CANARY 2 FAILED: a COMMENTED coordinate counted $c2, expected 0."
  echo "          Explaining the rule must not read as violating it."
  FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
  echo "root-coord lint: OK — every root-coordinate site matches the ledger; 2 self-canaries live."
fi
# $FAIL is a literal 0 or 1 — assigned only as `FAIL=0` at the top and `FAIL=1`
# at each violation, never a captured process status, never arithmetic — so the
# 8-bit ceiling cannot be reached.
exit "$FAIL"   # lint:exit-ok — $FAIL is only ever the literal 0 or 1, see above
