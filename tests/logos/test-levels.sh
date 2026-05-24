#!/usr/bin/env bash
# test-levels.sh — tiered test selection for fast feedback during normal work.
#
# A "GROUP" is a source category: each subdirectory under tests/imported/{pass,
# fail}/<cat>/, plus tests/logos/{pass,fail} as their own groups. Level
# membership is computed DYNAMICALLY from the sorted .logos file list per group
# — so adding a test needs NO marking: it just joins its group's list and the
# samplers pick it up automatically.
#
# Levels (run from the build/ dir):
#   L0 <name>      one specific test (substring of the ctest name)
#   L1 [v]         one test per group   (variant v picks a different member)
#   L2 [v]         ten tests per group  (variant v shifts the 10-wide window)
#   L3 <1|2>       half of each group   (1 = first half, 2 = second half;
#                                        the two halves together = everything)
#   L4             every test (full suite)
#
# Normal work: L0–L2 give broad coverage cheaply. Run L4 only once a whole
# group/feature is finished.
#
# Examples:
#   bash ../tests/logos/test-levels.sh L0 nested-by-ref-b167
#   bash ../tests/logos/test-levels.sh L1            # default variant 1
#   bash ../tests/logos/test-levels.sh L1 3          # 3rd member of each group
#   bash ../tests/logos/test-levels.sh L2 2          # 2nd window of 10/group
#   bash ../tests/logos/test-levels.sh L3 1
#   bash ../tests/logos/test-levels.sh L4
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../.." && pwd)
SUMMARY="$SCRIPT_DIR/ctest-summary.sh"

LEVEL=${1:-}
shift || true

if [ -z "$LEVEL" ]; then
    grep -E '^#( |$)' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi

# ── L0 / L4: trivial ────────────────────────────────────────────────────────
if [ "$LEVEL" = "L4" ]; then
    echo "[test-levels] L4 — full suite"
    bash "$SUMMARY"
    exit $?
fi
if [ "$LEVEL" = "L0" ]; then
    NAME=${1:-}
    [ -z "$NAME" ] && { echo "L0 needs a test name (substring)"; exit 2; }
    echo "[test-levels] L0 — '$NAME'"
    bash "$SUMMARY" -R "$NAME"
    exit $?
fi

VARIANT=${1:-1}

# ── Enumerate (group, base) from the .logos corpus ───────────────────────────
# Group key: category dir for imported tests, "native_{pass,fail}" otherwise.
# Emit "<group>\t<base>" lines, then sort so per-group ordering is stable.
emit_files() {
    # imported, grouped by category subdir
    while IFS= read -r f; do
        local cat base
        cat=$(basename "$(dirname "$f")")
        base=$(basename "$f" .logos)
        printf '%s\t%s\n' "imp_$cat" "$base"
    done < <(find "$REPO/tests/imported/pass" "$REPO/tests/imported/fail" \
                  -name '*.logos' 2>/dev/null)
    # native pass/fail — each its own group
    while IFS= read -r f; do
        printf '%s\t%s\n' "native_pass" "$(basename "$f" .logos)"
    done < <(find "$REPO/tests/logos/pass" -maxdepth 1 -name '*.logos' 2>/dev/null)
    while IFS= read -r f; do
        printf '%s\t%s\n' "native_fail" "$(basename "$f" .logos)"
    done < <(find "$REPO/tests/logos/fail" -maxdepth 1 -name '*.logos' 2>/dev/null)
}

# Selected base names (one per line) computed per group.
SELECTED=$(emit_files | sort | awk -F'\t' -v level="$LEVEL" -v variant="$VARIANT" '
    { group[$1] = group[$1] $2 "\n"; }    # accumulate bases per group (sorted by sort above)
    END {
        for (g in group) {
            n = split(group[g], a, "\n");
            # a[n] is empty (trailing newline) — real count is n-1
            cnt = n - 1;
            if (cnt <= 0) continue;
            if (level == "L1") {
                idx = ((variant - 1) % cnt) + 1;
                print a[idx];
            } else if (level == "L2") {
                start = ((variant - 1) * 10) % cnt;
                for (k = 0; k < 10 && k < cnt; k++)
                    print a[((start + k) % cnt) + 1];
            } else if (level == "L3") {
                half = int((cnt + 1) / 2);
                if (variant == 2) { for (k = half + 1; k <= cnt; k++) print a[k]; }
                else              { for (k = 1; k <= half;  k++) print a[k]; }
            }
        }
    }
' | sort -u)

COUNT=$(printf '%s\n' "$SELECTED" | grep -c .)
if [ "$COUNT" -eq 0 ]; then
    echo "[test-levels] $LEVEL: no tests selected"; exit 2
fi

echo "[test-levels] $LEVEL.$VARIANT — $COUNT tests selected across groups"

# ctest's -R regex engine (KWSys) caps the compiled expression size, so a
# single `_(a|b|…)$` alternation over thousands of names fails ("Expression
# too big"). Run in chunks under the cap and aggregate. Each chunk is a
# separate `ctest -j12` invocation; failures are surfaced inline.
CHUNK=250
mapfile -t NAMES < <(printf '%s\n' "$SELECTED" | grep .)
tot_run=0 tot_fail=0 had_fail=0 n=${#NAMES[@]}
echo "=== Failures ==="
for ((i = 0; i < n; i += CHUNK)); do
    batch=("${NAMES[@]:i:CHUNK}")
    rx=$(printf '%s\n' "${batch[@]}" | sed 's/[].[^$*+?(){}|\\]/\\&/g' | paste -sd '|' -)
    rx="_(${rx})\$"
    out=$(ctest -j12 --output-on-failure -R "$rx" 2>&1)
    line=$(printf '%s\n' "$out" | grep -E "tests passed" | tail -1)
    # "X% tests passed, Y tests failed out of Z"
    z=$(printf '%s' "$line" | sed -nE 's/.*out of ([0-9]+).*/\1/p')
    y=$(printf '%s' "$line" | sed -nE 's/.*, ([0-9]+) tests failed.*/\1/p')
    tot_run=$(( tot_run + ${z:-0} ))
    tot_fail=$(( tot_fail + ${y:-0} ))
    if [ "${y:-0}" -gt 0 ]; then
        had_fail=1
        printf '%s\n' "$out" | awk '/\*\*\*Failed/{print; c=0; f=1; next} f{print; if(++c>=40){f=0; print "---"}}'
    fi
done
[ "$had_fail" -eq 0 ] && echo "(none)"
echo
echo "=== Pass/fail ==="
echo "$(( tot_run - tot_fail ))/$tot_run tests passed, $tot_fail failed  (level $LEVEL.$VARIANT)"
[ "$tot_fail" -eq 0 ]
