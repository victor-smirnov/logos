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
# tier (105 programs / 12 684 cases, ~34 s) is therefore run EXPLICITLY at every
# level L1-L3, ahead of the sampled corpus; its FULL tier (265 programs / 14 876
# cases, ~153 s) is a plain ctest test and so runs at L4 with everything else.
# Set LOGOS_NO_EXHAUSTIVE=1 to skip it — for BISECTING a corpus failure, not for
# making a commit green.
#
# THE GATES: the same blindness, one layer over. The samplers enumerate .logos
# files under tests/{imported,logos}/{pass,fail}; the lints are not .logos and
# tests/logos/ir/ is not one of those directories, so logos_00_* and
# logos_07_ir_snapshot_* were unreachable from every level — including the whole
# argument for noalias+readonly on a shared &T. They are now named explicitly at
# L1-L3, ahead of the sampled corpus, at a measured cost of ~10-13 s. Set
# LOGOS_NO_GATES=1 to skip, under the same caveat as above.
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
    exit $?  # lint:exit-ok — the status of the `bash` just run: a real wait status
fi
if [ "$LEVEL" = "L0" ]; then
    NAME=${1:-}
    [ -z "$NAME" ] && { echo "L0 needs a test name (substring)"; exit 2; }
    echo "[test-levels] L0 — '$NAME'"
    bash "$SUMMARY" -R "$NAME"
    exit $?  # lint:exit-ok — the status of the `bash` just run: a real wait status
fi

VARIANT=${1:-1}

# ── The enumerator's smoke tier — every level, ahead of the sampled corpus ───
# It is not a .logos file, so the samplers below cannot reach it; it is named
# here instead. Its failure is the level's failure.
EXH_FAIL=0
if [ "${LOGOS_NO_EXHAUSTIVE:-0}" != "1" ]; then
    echo "[test-levels] enumerator — smoke tier (12 684 generated cases)"
    # ⚠ `--no-tests=error`. This name is registered by a `if(EXISTS
    # tests/exhaustive/harness.py)` block, so a moved or deleted generator
    # un-registers it — and `ctest -R` that matches nothing exits 0. Every level
    # would then print "the enumerator's smoke tier passed (12 684 generated
    # cases)" about 0 cases. Measured on ctest 3.28.3: no match exits 0 without
    # the flag and 8 with it.
    if ! ctest --no-tests=error --output-on-failure -R '^logos_26_exhaustive_smoke$'; then
        EXH_FAIL=1
    fi
fi

# ── THE GATES TIER — every level L1-L3, ahead of the sampled corpus ─────────
# SAME REASON AS THE ENUMERATOR ABOVE, and the same shape of blindness. The
# samplers below enumerate `.logos` files under tests/imported/{pass,fail}/*/ and
# tests/logos/{pass,fail}/, then select by an anchored `-R _(name…)$`. NOTHING
# ELSE CAN EVER BE SELECTED — not tests/logos/ir/*.logos (wrong directory), not
# the lint scripts (not .logos at all). They carry ctest LABELS ("logos;lint",
# "logos;ir_snapshot;suite_ir") but this script never reads labels, so labelling
# alone could not pull them in either.
#
# The consequence was concrete: the whole argument for `noalias`+`readonly` on a
# shared `&T` — the lint that holds its claims and censuses, and the IR snapshots
# that pin the emission — sat OFF the per-commit path, alongside the sibling
# separator-split lint. A tier that runs the corpus but not the gates is a tier
# that cannot see a soundness ruling being edited.
#
# WHAT IS PULLED IN: all of logos_00_* (the whole gate family — running three of
# them per commit would be the same defect one level up) and all of
# logos_07_ir_snapshot_*. 24 tests today: 12 + 12. (The comment said "21: 9 + 12"
# and had been stale by two since before this arc — a count someone will trust,
# so it is re-measured here: `grep -c 'NAME logos_00_'` = 12,
# `ls tests/logos/ir/*.check | wc -l` = 12. The twelfth logos_00_ is
# `logos_00_abi_reachability`, added with the ABI-closure gate.)
#
# THE ACCEPTED COST, MEASURED HERE RATHER THAN ESTIMATED: 9.0 s of ctest wall,
# three consecutive runs at 8.98 / 9.01 / 9.00 s (-j12, 32-core box at load ~6).
# ⚠ THE SAME COMMAND MEASURED 21.1 s ON THE SAME TREE at load ~50 with a sibling
# worktree running its own suite — so this number is a property of an idle box,
# and a slow gates tier is evidence about the machine before it is evidence about
# the gates. The tier is dominated by logos_00_mlir_gen_bug_ledger (8.7 s) and
# logos_00_sep_symbol_shape (6.3 s), which run in parallel with everything else;
# the two freeze snapshots this arc added cost 5.5 s and 3.0 s and overlap them.
# Against the ~65 s L2 quoted at HEAD that is roughly +14%, paid on every commit,
# to put the soundness argument for noalias+readonly on the per-commit path.
#
# ⚠ `--no-tests=error`. `ctest -R` that matches NOTHING exits 0, so a renamed
# gate family would make this block print nothing and pass — the exact failure
# the enumerator block above already documents. Its failure is the level's
# failure, like EXH_FAIL.
GATE_FAIL=0
if [ "${LOGOS_NO_GATES:-0}" != "1" ]; then
    echo "[test-levels] gates — logos_00_* and logos_07_ir_snapshot_*"
    if ! ctest --no-tests=error -j12 --output-on-failure \
               -R '^logos_00_|^logos_07_ir_snapshot_'; then
        GATE_FAIL=1
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
    out=$(ctest --no-tests=error -j12 --output-on-failure -R "$rx" 2>&1)
    line=$(printf '%s\n' "$out" | grep -E "tests passed" | tail -1)
    # "X% tests passed, Y tests failed out of Z"
    z=$(printf '%s' "$line" | sed -nE 's/.*out of ([0-9]+).*/\1/p')
    y=$(printf '%s' "$line" | sed -nE 's/.*, ([0-9]+) tests failed.*/\1/p')
    # ⚠ A CHUNK THAT REPORTED NO SUMMARY RAN NOTHING, and `${z:-0}` used to fold
    # that into the totals as a zero — so a `-R` that matched nothing (a renamed
    # test, an alternation over the KWSys size cap, a build with no tests
    # registered) added 0 run and 0 failed and the level stayed green. ctest exits
    # 0 on "No tests were found!!!"; only the missing summary says so.
    if [ -z "$z" ]; then
        echo "***Failed: ctest produced no pass/fail summary for a chunk of ${#batch[@]} selected tests"
        printf '%s\n' "$out" | tail -15
        had_fail=1
        continue
    fi
    tot_run=$(( tot_run + z ))
    tot_fail=$(( tot_fail + ${y:-0} ))
    if [ "${y:-0}" -gt 0 ]; then
        had_fail=1
        printf '%s\n' "$out" | awk '/\*\*\*Failed/{print; c=0; f=1; next} f{print; if(++c>=40){f=0; print "---"}}'
    fi
done
# ⚠ THE LEVEL DECLARES THE MINIMUM IT MUST OBSERVE, AND IT IS ITS OWN SELECTION.
# The samplers above compute $COUNT names from the .logos corpus and ctest is
# asked for them by an anchored alternation; if fewer than $COUNT tests actually
# ran, the two halves disagree — a name the corpus has and ctest does not (a
# fixture never registered, a `.expected` cmake skipped, a regex escaped wrong) —
# and the pass count below would be a true statement about a smaller suite.
#
# It is a FLOOR AND ONLY A FLOOR: one base name can register more than one ctest
# name (measured at L2: 1773 names selected, 1859 tests run), so this cannot see
# a single name going missing. The EXACT pairing — every corpus .logos has the
# .expected that registers it — is `corpus_registration_gate.sh`, which is where
# the per-file answer belongs; this one catches a whole chunk evaporating.
if [ "$tot_run" -lt "$COUNT" ]; then
    echo "***Failed: $COUNT tests were selected from the corpus but only $tot_run ran."
    echo "  A selected name that ctest does not know is a test that exists on disk and"
    echo "  in no suite. This level cannot report on tests it never observed."
    had_fail=1
fi
[ "$had_fail" -eq 0 ] && [ "$EXH_FAIL" -eq 0 ] && [ "$GATE_FAIL" -eq 0 ] && echo "(none)"
[ "$EXH_FAIL" -ne 0 ] && echo "logos_26_exhaustive_smoke ***Failed (see above)"
[ "$GATE_FAIL" -ne 0 ] && echo "the gates tier (logos_00_* / logos_07_ir_snapshot_*) ***Failed (see above)"
echo
echo "=== Pass/fail ==="
echo "$(( tot_run - tot_fail ))/$tot_run tests passed, $tot_fail failed  (level $LEVEL.$VARIANT)"
# The enumerator is counted separately BECAUSE it is not one of the $tot_run:
# it is 12 684 generated cases behind one ctest name, and folding it into the
# corpus count would misreport both.
if [ "${LOGOS_NO_EXHAUSTIVE:-0}" = "1" ]; then
    echo "PLUS: the enumerator's smoke tier was SKIPPED (LOGOS_NO_EXHAUSTIVE=1)"
elif [ "$EXH_FAIL" -ne 0 ]; then
    echo "PLUS: the enumerator's smoke tier FAILED (12 684 generated cases)"
else
    echo "PLUS: the enumerator's smoke tier passed (12 684 generated cases)"
fi
# The gates are counted separately for the same reason as the enumerator: they
# are not among the $tot_run, and folding them in would misreport both.
if [ "${LOGOS_NO_GATES:-0}" = "1" ]; then
    echo "PLUS: the gates tier was SKIPPED (LOGOS_NO_GATES=1)"
elif [ "$GATE_FAIL" -ne 0 ]; then
    echo "PLUS: the gates tier FAILED (logos_00_* / logos_07_ir_snapshot_*)"
else
    echo "PLUS: the gates tier passed (logos_00_* / logos_07_ir_snapshot_*)"
fi
# ⚠ `had_fail` IS PART OF THE EXIT STATUS. It used to be equivalent to
# `tot_fail > 0` — the only thing that set it — and the two checks above set it
# for failures that produce NO failed-test count at all: a chunk that reported no
# summary ran zero tests, and a selection larger than the run is a test the suite
# does not know. Both print `***Failed` and both would have left `tot_fail` at 0,
# so the script would have printed the failure and exited 0. A message on stdout
# is not a verdict; the exit code is.
[ "$tot_fail" -eq 0 ] && [ "$had_fail" -eq 0 ] && [ "$EXH_FAIL" -eq 0 ] && [ "$GATE_FAIL" -eq 0 ]
