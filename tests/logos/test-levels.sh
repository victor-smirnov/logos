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
# IMPORTED TESTS: the Rust-derived conformance ports (tests/imported/, ~55% of
# the suite, survivor-biased and largely duplicating the core coverage) are
# EXCLUDED by default at every level — the everyday loop runs the core-logos +
# spec net only. Add a trailing `imp` (or set LOGOS_IMPORTED=1) to include them
# when you want the full conformance gate. Nothing is deleted; this is a
# `ctest -LE imported` split (see tests/logos/CMakeLists.txt).
#
# THE ENUMERATOR: `tests/exhaustive` is a GENERATOR, not a corpus member, so the
# samplers above — which enumerate .logos files — can never select it. Its SMOKE
# tier (86 programs / 11 316 cases, ~34 s) is therefore run EXPLICITLY at every
# level L1-L3, ahead of the sampled corpus; its FULL tier (246 programs / 13 508
# cases, ~153 s) is a plain ctest test and so runs at L4 with everything else.
# Set LOGOS_NO_EXHAUSTIVE=1 to skip it — for BISECTING a corpus failure, not for
# making a commit green.
#
# Examples:
#   bash ../tests/logos/test-levels.sh L0 nested-by-ref-b167
#   bash ../tests/logos/test-levels.sh L1            # default variant 1, core only
#   bash ../tests/logos/test-levels.sh L1 3          # 3rd member of each group
#   bash ../tests/logos/test-levels.sh L2 2          # 2nd window of 10/group
#   bash ../tests/logos/test-levels.sh L3 1
#   bash ../tests/logos/test-levels.sh L4            # full core+spec
#   bash ../tests/logos/test-levels.sh L4 imp        # full incl. imported ports
#   bash ../tests/logos/test-levels.sh L2 2 imp      # L2, imported groups too
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../.." && pwd)
SUMMARY="$SCRIPT_DIR/ctest-summary.sh"

# ── HOT groups: the feature prefixes that break most from compiler changes ────
# L1 and L2 run these groups IN FULL (not just the per-group sample), so a
# quick tier maximises the chance of catching a regression where regressions
# actually land. Seeded from the areas that repeatedly tripped during the
# 2026-07 sema arcs (coercion / const-array-length / cast / drop / borrow /
# pattern) plus the mangling-sensitive generic/trait family and the diagnostic
# canaries (spec *_diag_ tests are hot by nature). TUNE THIS as areas stabilise
# or new fragile ones appear — it is a plain list, no other machinery.
HOT_TOKENS="coerce coercion cast array arr const slice deref drop borrow move \
pattern pat match enum generic generics trait traits impl mangling g156 g162 \
mono nll lt lifetime self assoc closure fn dup diag inferred hole repr variance \
memstore relptr box unsize dst tuple"

LEVEL=${1:-}
shift || true

if [ -z "$LEVEL" ]; then
    grep -E '^#( |$)' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi

# Imported ports are off unless `imp` is passed anywhere in the args or
# LOGOS_IMPORTED=1 is set. Strip the token so it doesn't disturb VARIANT/name.
WITH_IMPORTED=${LOGOS_IMPORTED:-0}
_args=()
for a in "$@"; do
    if [ "$a" = "imp" ]; then WITH_IMPORTED=1; else _args+=("$a"); fi
done
set -- "${_args[@]:-}"
[ "${1:-}" = "" ] && shift 2>/dev/null || true

# ── L0 / L4: trivial ────────────────────────────────────────────────────────
if [ "$LEVEL" = "L4" ]; then
    if [ "$WITH_IMPORTED" = "1" ]; then
        echo "[test-levels] L4 — full suite (incl. imported ports)"
        bash "$SUMMARY"
    else
        echo "[test-levels] L4 — full suite (core+spec; imported excluded, add 'imp' for full)"
        bash "$SUMMARY" -LE imported
    fi
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

# ── The enumerator's smoke tier — every level, ahead of the sampled corpus ───
# It is not a .logos file, so the samplers below cannot reach it; it is named
# here instead. Its failure is the level's failure.
EXH_FAIL=0
if [ "${LOGOS_NO_EXHAUSTIVE:-0}" != "1" ]; then
    echo "[test-levels] enumerator — smoke tier (11 316 generated cases)"
    if ! ctest --output-on-failure -R '^logos_26_exhaustive_smoke$'; then
        EXH_FAIL=1
    fi
fi

# ── Enumerate (group, base) from the .logos corpus ───────────────────────────
# Group key: category dir for imported tests, "native_{pass,fail}" otherwise.
# Emit "<group>\t<base>" lines, then sort so per-group ordering is stable.
emit_files() {
    # imported, grouped by category subdir — SKIPPED by default (survivor-biased
    # conformance ports; `imp`/LOGOS_IMPORTED=1 opts them back in).
    if [ "$WITH_IMPORTED" = "1" ]; then
    while IFS= read -r f; do
        local cat base
        cat=$(basename "$(dirname "$f")")
        base=$(basename "$f" .logos)
        printf '%s\t%s\n' "imp_$cat" "$base"
    done < <(find "$REPO/tests/imported/pass" "$REPO/tests/imported/fail" \
                  -name '*.logos' 2>/dev/null)
    fi
    # native pass/fail: the core net is now the everyday default, so it must
    # sub-sample meaningfully rather than being one flat group. The dir is
    # flat, but the filenames carry a feature prefix (core_/struct_/trait_/
    # match_/generic_/nll_/…) — group by that leading token, mirroring how the
    # imported tests group by category. L1 then samples one per feature-prefix
    # (broad quick smoke); L2/L3 widen toward the full core+spec set.
    for mode in pass fail; do
        while IFS= read -r f; do
            local base tok
            base=$(basename "$f" .logos)
            tok=$(printf '%s' "$base" | sed -E 's/[_-].*//')
            printf '%s\t%s\n' "n${mode:0:1}_${tok}" "$base"
        done < <(find "$REPO/tests/logos/$mode" -maxdepth 1 -name '*.logos' 2>/dev/null)
    done
}

# Selected base names (one per line) computed per group.
SELECTED=$(emit_files | sort | awk -F'\t' -v level="$LEVEL" -v variant="$VARIANT" \
    -v hot="$HOT_TOKENS" '
    BEGIN { nh = split(hot, ha, /[ \t]+/); for (i=1;i<=nh;i++) if (ha[i]!="") HOT[ha[i]]=1 }
    # A group is hot if its token (the part after the np_/nf_/imp_ prefix) is listed.
    function is_hot(g,   t) { t = g; sub(/^[a-z]+_/, "", t); return (t in HOT) }
    { group[$1] = group[$1] $2 "\n"; }    # accumulate bases per group (sorted by sort above)
    END {
        for (g in group) {
            n = split(group[g], a, "\n");
            # a[n] is empty (trailing newline) — real count is n-1
            cnt = n - 1;
            if (cnt <= 0) continue;
            # HOT groups get extra weight at the quick tiers — that is where
            # regressions bite. L2 runs them IN FULL; L1 samples them deeper
            # than a normal group (up to 6) so it stays quick but fragile-biased.
            if (level == "L2" && is_hot(g)) {
                for (k = 1; k <= cnt; k++) print a[k];
                continue;
            }
            if (level == "L1" && is_hot(g)) {
                lim = (cnt < 6 ? cnt : 6);
                start = ((variant - 1) * lim) % cnt;
                for (k = 0; k < lim; k++) print a[((start + k) % cnt) + 1];
                continue;
            }
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
# Chunk size: as large as KWSys's regex cap allows (a single ~811-name `-R`
# compiles; ~2594 doesn't). Bigger chunks ⇒ fewer ctest invocations ⇒ less
# tail-idle (each chunk waits for its slowest test before the next starts).
CHUNK=700
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
[ "$had_fail" -eq 0 ] && [ "$EXH_FAIL" -eq 0 ] && echo "(none)"
[ "$EXH_FAIL" -ne 0 ] && echo "logos_26_exhaustive_smoke ***Failed (see above)"
echo
echo "=== Pass/fail ==="
echo "$(( tot_run - tot_fail ))/$tot_run tests passed, $tot_fail failed  (level $LEVEL.$VARIANT)"
# The enumerator is counted separately BECAUSE it is not one of the $tot_run:
# it is 11 316 generated cases behind one ctest name, and folding it into the
# corpus count would misreport both.
if [ "${LOGOS_NO_EXHAUSTIVE:-0}" = "1" ]; then
    echo "PLUS: the enumerator's smoke tier was SKIPPED (LOGOS_NO_EXHAUSTIVE=1)"
elif [ "$EXH_FAIL" -ne 0 ]; then
    echo "PLUS: the enumerator's smoke tier FAILED (11 316 generated cases)"
else
    echo "PLUS: the enumerator's smoke tier passed (11 316 generated cases)"
fi
[ "$tot_fail" -eq 0 ] && [ "$EXH_FAIL" -eq 0 ]
