#!/usr/bin/env bash
# corpus_registration_gate.sh REPO_ROOT LEDGER
#
# EVERY CORPUS FILE IS IN A SUITE, OR IT IS WRITTEN DOWN.
#
# THE DEFECT CLASS. `tests/logos/CMakeLists.txt` registers tests by globbing
# `*.expected` and deriving the `.logos` from it. The asymmetry is total:
#
#   .expected with no .logos  → `message(WARNING "no .logos for …, skipping")`
#   .logos    with no .expected → NOTHING. No warning, no test, no trace.
#
# So a fixture can be written, committed and reviewed and never run once. It is
# worse than a skipped test, because a skipped test is visible: this one is
# indistinguishable from a passing one at every level of the suite.
# `test-levels.sh` even SELECTS its name — the samplers enumerate `.logos` files
# — and the anchored `-R` alternation then quietly matches one fewer test, which
# is invisible because one base name can register more than one ctest name.
#
# MEASURED 2026-07-31 when this gate was written: 6737 corpus `.logos` files, 14
# of them registered nowhere, 0 orphan `.expected`. Twelve of the fourteen are
# in `tests/logos/{pass,fail}` — hand-written feature tests, not ports.
#
# WHAT THIS ASSERTS, and it is the same shape as `tests/exhaustive/refusals.
# ledger`:
#
#   1. A FLOOR ON WHAT WAS SCANNED. If the walk finds fewer files than the
#      corpus has, "no orphans" means "no corpus" and the answer is red. A gate
#      whose input list can silently empty is the defect it is checking for.
#   2. NO UNLISTED ORPHAN. A `.logos` with no `.expected` that is not in the
#      ledger is a new test that nothing runs.
#   3. NO STALE LINE. A ledger path that now HAS its `.expected`, or that no
#      longer exists, is a line asserting nothing — the arc landed and the
#      record stopped describing the corpus.
#   4. NO ORPHAN `.expected`. cmake warns and continues; a warning in a
#      configure log nobody reads is not a verdict.
set -euo pipefail

REPO="${1:?repo root}"
LEDGER="${2:?ledger path}"

# The floor is deliberately far below the measured 6737: it is here to catch a
# broken walk (a moved directory, a `find` predicate that errored into "no
# matches"), not to track corpus growth.
MIN_SCANNED=5000

DIRS=(
    "$REPO/tests/logos/pass"   "$REPO/tests/logos/fail"
    "$REPO/tests/imported/pass" "$REPO/tests/imported/fail"
    "$REPO/tests/spec/pass"    "$REPO/tests/spec/fail"
)
PRESENT=()
for d in "${DIRS[@]}"; do
    [ -d "$d" ] && PRESENT+=("$d")
done
if [ "${#PRESENT[@]}" -ne "${#DIRS[@]}" ]; then
    echo "FAIL: ${#PRESENT[@]} of ${#DIRS[@]} corpus directories exist — the walk below"
    echo "      would report 'no orphans' about the ones it cannot see:"
    for d in "${DIRS[@]}"; do [ -d "$d" ] || echo "  missing: $d"; done
    exit 1
fi

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

find "${PRESENT[@]}" -name '*.logos'    -type f | sort > "$TMPD/logos"
find "${PRESENT[@]}" -name '*.expected' -type f | sort > "$TMPD/expected"

n_logos=$(wc -l < "$TMPD/logos")
n_exp=$(wc -l < "$TMPD/expected")

fail=0

# ── 1. the walk saw a corpus ────────────────────────────────────────────────
if [ "$n_logos" -lt "$MIN_SCANNED" ]; then
    echo "FAIL: the walk found $n_logos .logos files under ${#PRESENT[@]} corpus"
    echo "      directories (want >= $MIN_SCANNED). Every check below is 'nothing was"
    echo "      found', which on an empty list is indistinguishable from 'nothing is"
    echo "      wrong'. Either the corpus moved or the walk broke."
    exit 1
fi

# ── 2/3. .logos without .expected, against the ledger, BOTH ways ───────────
: > "$TMPD/orphans"
while IFS= read -r f; do
    [ -f "${f%.logos}.expected" ] || printf '%s\n' "${f#"$REPO"/}" >> "$TMPD/orphans"
done < "$TMPD/logos"

sed 's/#.*//' "$LEDGER" | sed 's/[[:space:]]*$//' | grep -v '^$' | sort > "$TMPD/ledger"
sort "$TMPD/orphans" > "$TMPD/orphans.sorted"

new=$(comm -23 "$TMPD/orphans.sorted" "$TMPD/ledger")
stale=$(comm -13 "$TMPD/orphans.sorted" "$TMPD/ledger")

if [ -n "$new" ]; then
    n=$(printf '%s\n' "$new" | grep -c .)
    echo "FAIL: $n corpus .logos file(s) have no .expected and are not in the ledger."
    echo "      cmake registers tests by globbing .expected, so these are in NO suite:"
    echo "      they exist on disk, test-levels.sh selects their names, and ctest has"
    echo "      never heard of them. Give each one an .expected, or record it in"
    echo "      $LEDGER with its ground."
    printf '  %s\n' $new
    fail=1
fi
if [ -n "$stale" ]; then
    n=$(printf '%s\n' "$stale" | grep -c .)
    echo "FAIL: $n ledger line(s) no longer describe the corpus — each names a file that"
    echo "      now HAS its .expected (so it is registered and the line asserts nothing)"
    echo "      or that no longer exists. Remove them:"
    for p in $stale; do
        if [ ! -f "$REPO/$p" ];               then echo "  $p   (file is gone)"
        elif [ -f "$REPO/${p%.logos}.expected" ]; then echo "  $p   (registered now)"
        else                                       echo "  $p   (?)"; fi
    done
    fail=1
fi

# ── 4. .expected without .logos ────────────────────────────────────────────
: > "$TMPD/orphan_exp"
while IFS= read -r f; do
    [ -f "${f%.expected}.logos" ] || printf '%s\n' "${f#"$REPO"/}" >> "$TMPD/orphan_exp"
done < "$TMPD/expected"
if [ -s "$TMPD/orphan_exp" ]; then
    echo "FAIL: $(wc -l < "$TMPD/orphan_exp") .expected file(s) name no .logos. cmake prints"
    echo "      'no .logos for …, skipping' and carries on — a warning in a configure log"
    echo "      is not a verdict, and the suite stays green one test smaller:"
    sed 's/^/  /' "$TMPD/orphan_exp"
    fail=1
fi

if [ "$fail" -ne 0 ]; then exit 1; fi
echo "OK: $n_logos corpus .logos, $n_exp .expected;" \
     "$(wc -l < "$TMPD/orphans.sorted") unregistered, all in the ledger; 0 orphan .expected"
exit 0
