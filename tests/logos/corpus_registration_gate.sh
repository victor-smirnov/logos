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

# ⚠ THE FLOOR IS THE MEASURED VALUE. It was 5000 against a measured 6740 — a
# quarter of the corpus could stop being walked and this stayed green, and the
# ground written for it ("it is here to catch a broken walk, not to track corpus
# growth") is the argument for halving a floor, which is the argument for not
# noticing. The corpus only grows; a DROP is an event.
# MEASURED 2026-07-31 at `62835ad3`: 6740 .logos, 6726 .expected, 14 unregistered.
MIN_SCANNED=6740

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

# ONE walk, one pair of orphan rules — used for the real corpus and for the
# canary below, so the canary is judged by exactly the code that judges the tree.
scan_corpus() {   # scan_corpus <out-prefix> <root> <dir>...
    local pfx=$1 root=$2 f; shift 2
    find "$@" -name '*.logos'    -type f | sort > "$pfx.logos"
    find "$@" -name '*.expected' -type f | sort > "$pfx.expected"
    : > "$pfx.orphans"; : > "$pfx.orphan_exp"
    while IFS= read -r f; do
        [ -f "${f%.logos}.expected" ] || printf '%s\n' "${f#"$root"/}" >> "$pfx.orphans"
    done < "$pfx.logos"
    while IFS= read -r f; do
        [ -f "${f%.expected}.logos" ] || printf '%s\n' "${f#"$root"/}" >> "$pfx.orphan_exp"
    done < "$pfx.expected"
}

# ⚠⚠ THE CANARY, BEFORE ANY VERDICT. Every finding this gate can make is "a list
# came back non-empty"; on a walk that finds nothing, or an orphan rule that
# stopped pairing, every list is empty and the gate says the corpus is fully
# registered. A three-file corpus is planted here — one `.logos` with no
# `.expected`, one `.expected` with no `.logos`, and one COMPLETE pair — and
# `scan_corpus` must report the first two and NOT the third. Both directions:
# a rule that flags everything would satisfy the first half alone.
# RIDES: the same `find`, the same pairing test, the same relative-path
# stripping. DOES NOT RIDE: the ledger reconciliation (`comm`), whose two
# findings are `comm -23` and `comm -13` of the same list against the ledger.
mkdir -p "$TMPD/canary"
: > "$TMPD/canary/a_no_expected.logos"
: > "$TMPD/canary/b_no_logos.expected"
: > "$TMPD/canary/c_complete.logos"
: > "$TMPD/canary/c_complete.expected"
scan_corpus "$TMPD/can" "$TMPD/canary" "$TMPD/canary"
canary_bad=""
grep -qx 'a_no_expected.logos'  "$TMPD/can.orphans"    || canary_bad+="the .logos with no .expected was NOT reported; "
grep -qx 'b_no_logos.expected'  "$TMPD/can.orphan_exp" || canary_bad+="the .expected with no .logos was NOT reported; "
grep -qx 'c_complete.logos'     "$TMPD/can.orphans"    && canary_bad+="a COMPLETE pair was reported as an orphan; "
grep -qx 'c_complete.expected'  "$TMPD/can.orphan_exp" && canary_bad+="a COMPLETE pair's .expected was reported as an orphan; "
if [ -n "$canary_bad" ]; then
    echo "FAIL (CANARY 'orphan detection' NOT CAUGHT): $canary_bad"
    echo "      A planted three-file corpus was not classified correctly by the same"
    echo "      walk that judges the real tree, so 'no orphans' below is not evidence."
    echo "      THE GATE IS BROKEN, not the tree."
    echo "      orphans: $(paste -sd' ' - < "$TMPD/can.orphans")"
    echo "      orphan .expected: $(paste -sd' ' - < "$TMPD/can.orphan_exp")"
    exit 1
fi
echo "[corpus-gate] canary: a missing .expected and a missing .logos were both"
echo "              reported, and a complete pair was not"

scan_corpus "$TMPD/real" "$REPO" "${PRESENT[@]}"
cp "$TMPD/real.logos" "$TMPD/logos"; cp "$TMPD/real.expected" "$TMPD/expected"
cp "$TMPD/real.orphans" "$TMPD/orphans"; cp "$TMPD/real.orphan_exp" "$TMPD/orphan_exp"

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
