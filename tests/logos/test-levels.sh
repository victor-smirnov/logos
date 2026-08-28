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
# spec net only. Add a trailing `bc` for the borrow-check ports; `imported-unreviewed` for all
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
# argument for noalias+readonly on a shared &T. They now run at L1-L3, ahead of
# the sampled corpus, at a measured cost of ~10-13 s, selected by the tier LABEL
# each gate declares about itself (`-L '^tier_commit$'`) rather than by a list of
# names. Set LOGOS_NO_GATES=1 to skip, under the same caveat as above.
#
# Examples:
#   bash ../tests/logos/test-levels.sh L0 nested-by-ref-b167
#   bash ../tests/logos/test-levels.sh L1            # default variant 1, core only
#   bash ../tests/logos/test-levels.sh L1 3          # 3rd member of each group
#   bash ../tests/logos/test-levels.sh L2 2          # 2nd window of 10/group
#   bash ../tests/logos/test-levels.sh L3 1
#   bash ../tests/logos/test-levels.sh L4            # full core+spec
#   bash ../tests/logos/test-levels.sh L4 bc         # core+spec + the borrow-check imported ports
#   bash ../tests/logos/test-levels.sh L4 imported-unreviewed
#                                                    # ALL imported ports. The tier is UNREVIEWED;
#                                                    # `imp` is retired and errors with the reason.
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
# ⚠ `bc` ADDED 2026-08-12 (D1 round 9). `borrow` was already here and covered
# SIX files (fail/pass borrow_*), while the borrow-checker arc's own fixtures
# are named `bc_*` — 122 of them — and the grouping token is the basename up to
# the first `_`, so `bc_d1r9_…` grouped as `bc` and matched nothing in this
# list. The family that this list exists to protect was the one sampled at 10
# per group: 20 of 122 ran at L2, 84% of the arc's regression fixtures sat
# outside the per-commit net while a 6-file legacy family sat fully inside it.
# Measured, not assumed: selection 1841 -> 1943 names (+102, PREDICTED exactly
# — 122 bc fixtures now run in full where 20 were sampled), and L2.1 1937 ->
# 2034 tests, rc=0 both. ⚠ The tests-run delta is +97, FIVE SHORT of the name
# delta, and that gap is REPORTED rather than explained: selection is by name
# but execution is `ctest -R`, which matches by SUBSTRING, so a selected name
# that is a prefix of others already pulled tests no name of its own claimed —
# the two counts were never 1:1 (1841 names ran 1937 tests before this edit).
# The five are not a missing fixture: all 122 `bc_*.logos` have their
# `.expected` beside them and `ctest -N` registers exactly 122 bc tests, both
# checked. Whoever next tunes this list should close that arithmetic rather
# than inherit the sentence.
# ⚠ `deem` ADDED 2026-08-14 (ADR-0025 S5 audit): the S5 runtime claim set
# (packet count, pull count, exhaustion, empty-packet-not-absence,
# composition) lives in exactly two deem fixtures, and the `deem` group (35
# files) was sampled at 10 per group — the variant-1 window contained
# NEITHER new file, so L2 read 2120/2120 before and after they were added.
# The whole batch-cursor plane's e2e surface is this group; it runs in full.
HOT_TOKENS="coerce coercion cast array arr const slice deref drop borrow bc move \
deem \
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
#
# ── `bc`: THE IMPORTED HALF THAT IS WORTH RUNNING ────────────────────────────
# `imp` runs all 4158 imported ports. Victor 2026-08-28: STOP DOING THAT — the
# imported tier needs a revision and it is not currently known what is in it,
# which is why it was split into its own group in the first place. A tier whose
# contents nobody can vouch for is not an oracle; it is a number that goes red
# for reasons that have to be investigated one at a time, and five of them were
# just disabled precisely because that investigation had not happened.
#
# `bc` runs the imported ports that carry the `bc` label — the upstream
# borrow-check suites (borrowck, nll, moves, regions, lifetimes, dropck, drop,
# closures, variance), 1262 tests, whose subject matter is the one this arc
# actually changes. Everything else in `tests/imported` is left alone until it
# has been reviewed.
#
# ⚠ TWO ctest RUNS, NOT ONE, because ctest ANDs its filters and there is no
# union operator: `-LE imported` for the core, then `-L imported -L bc`. The
# summary prints both and the exit status is the worse of the two.
# ⚠ `imp` AND `LOGOS_IMPORTED` ARE RETIRED, AND THEY FAIL LOUDLY RATHER THAN
# QUIETLY DOING THE OLD THING. Victor 2026-08-28, on why a written rule was not
# enough: agents will type the short token anyway, on the principle of "what
# happens if I try". A rule that depends on nobody being curious is not a rule.
# So the old spellings are not ignored and not silently redirected — they stop
# the run and say what to use instead. The replacement is deliberately too long
# to reach for absent-mindedly, and names the reason in the token itself.
WITH_IMPORTED=${LOGOS_IMPORTED_UNREVIEWED:-0}
WITH_BC_IMPORTED=${LOGOS_BC_IMPORTED:-0}
if [ -n "${LOGOS_IMPORTED:-}" ]; then
    echo "[test-levels] LOGOS_IMPORTED is retired." >&2
    echo "  The imported tier is UNREVIEWED: nobody can currently say what is in" >&2
    echo "  it, which is why it lives in its own group. Five of its tests were" >&2
    echo "  disabled on 2026-08-27 because that review had never happened." >&2
    echo "  Use:  test-levels.sh L4 bc                (the borrow-check ports)" >&2
    echo "  Or, if you truly mean all ~4158 unreviewed ports:" >&2
    echo "        LOGOS_IMPORTED_UNREVIEWED=1 test-levels.sh L4" >&2
    exit 2
fi
_args=()
for a in "$@"; do
    if [ "$a" = "imp" ]; then
        echo "[test-levels] 'imp' is retired." >&2
        echo "  It ran all ~4158 imported ports. The tier is UNREVIEWED — nobody" >&2
        echo "  can currently say what is in it, which is why it was split into" >&2
        echo "  its own group, and five of its tests were disabled on 2026-08-27" >&2
        echo "  because that review had never happened. A tier nobody can vouch" >&2
        echo "  for is not an oracle; it is a number that goes red for reasons" >&2
        echo "  that each need their own investigation." >&2
        echo "  Use:  test-levels.sh L4 bc     — core+spec + the 1260 borrow-check ports" >&2
        echo "  Or:   test-levels.sh L4 imported-unreviewed   — if you truly mean all of them" >&2
        exit 2
    elif [ "$a" = "imported-unreviewed" ]; then WITH_IMPORTED=1;
    elif [ "$a" = "bc" ]; then WITH_BC_IMPORTED=1; else _args+=("$a"); fi
done
set -- "${_args[@]:-}"
[ "${1:-}" = "" ] && shift 2>/dev/null || true

# ── L0 / L4: trivial ────────────────────────────────────────────────────────
# ── _write_state: the gate's verdict, as a file the NEXT step reads ─────────
# Fields are one per line so `grep` is enough and nothing needs a JSON parser.
# The KEY fields are what make a stale record detectable: a reader that finds a
# different HEAD / worktree hash / logosc mtime knows the answer is about some
# other tree and must say so rather than hand it over.
_write_state() {
    local rc="$1" shape="$2"; shift 2
    local failing
    failing=$(cat "$@" 2>/dev/null | grep -E '^\s+[0-9]+ - ' | sed 's/^ *//' | sort -u)
    {
        echo "rc=$rc"
        echo "shape=$shape"
        echo "level=$LEVEL bc=${WITH_BC_IMPORTED} imported=${WITH_IMPORTED}"
        echo "head=$_head"
        echo "worktree=$_dirty"
        echo "logosc_mtime=$_bin"
        echo "at=$(date -Is)"
        echo "counts=$(cat "$@" 2>/dev/null | grep -oE '[0-9]+ tests? (passed|failed)' | tr '\n' ' ')"
        echo "failing_count=$(printf '%s' "$failing" | grep -c . || true)"
        echo "--- failing set (empty means none) ---"
        printf '%s\n' "$failing"
    } > "$_stamp"
}

if [ "$LEVEL" = "L4" ]; then
    # ── BARRIER: L4 IS ~12 MINUTES AND A FOREGROUND TOOL CALL IS CAPPED AT 10 ──
    # A foreground L4 cannot finish, so every foreground attempt becomes a poll
    # loop; one round spent 52% of its command time in them. Run it detached and
    # wait on the marker instead. LOGOS_L4_BG=1 is the acknowledgement.
    if [ "${LOGOS_L4_BG:-0}" != "1" ]; then
        echo "[test-levels] L4 takes ~708 s; a foreground tool call is capped at 600 s." >&2
        echo "  It cannot complete in the foreground, and polling it with pgrep wastes" >&2
        echo "  the whole wait. Run it detached and block on the marker:" >&2
        echo "" >&2
        _argsback="L4"; [ "$WITH_BC_IMPORTED" = "1" ] && _argsback="L4 bc"
        [ "$WITH_IMPORTED" = "1" ] && _argsback="L4 imported-unreviewed"
        echo "    nohup bash -c 'cd $PWD && LOGOS_L4_BG=1 bash ../tests/logos/test-levels.sh $_argsback > /tmp/l4.log 2>&1; echo RC=\$? >> /tmp/l4.log' >/dev/null 2>&1 &" >&2
        echo "    until grep -q '^RC=' /tmp/l4.log; do sleep 30; done; tail -20 /tmp/l4.log" >&2
        echo "" >&2
        echo "  (or the Bash tool's own run_in_background, which notifies on exit)" >&2
        exit 2
    fi
    # ── THE VERDICT IS AN ARTEFACT, NOT A REFUSAL ──────────────────────────
    # Victor 2026-08-28: *"прогон теста должен просто писать в файл состояние, и
    # следующий шаг его читает вместо полного прогона"*. So every L4 records what
    # it found, keyed on the tree state it found it in, and anyone can READ that
    # instead of invoking a 12-minute gate — `scripts/gate-state.sh`. Measured
    # before this existed: 21 L4 runs across the session's workflow phases, 5 of
    # them opening baselines over a tree the previous phase had just certified
    # green. The refusal below is now a fallback for a caller who ran the gate
    # anyway; the intended path is that nobody needs to.
    _stamp_dir="${LOGOS_GATE_STATE:-$PWD/gate-state}"; mkdir -p "$_stamp_dir"
    # lint:git-ok — the stamp asks WHICH TREE STATE this rc belongs to, which is
    # hygiene and nothing else; it makes no claim about the artefact being right.
    _head=$(git -C "$SCRIPT_DIR/../.." rev-parse HEAD 2>/dev/null || echo nogit)  # lint:git-ok — the stamp asks WHICH TREE STATE this rc belongs to; pure hygiene, no claim about the artefact
    _dirty=$(git -C "$SCRIPT_DIR/../.." status --porcelain 2>/dev/null | sha256sum | cut -c1-16)  # lint:git-ok — same ground
    _bin=$(stat -c %Y bin/logosc 2>/dev/null || echo 0)
    _key="$LEVEL-${WITH_BC_IMPORTED}-${WITH_IMPORTED}-$_head-$_dirty-$_bin"
    _stamp="$_stamp_dir/$(printf '%s' "$_key" | sha256sum | cut -c1-32)"
    if [ -f "$_stamp" ]; then
        echo "[test-levels] REFUSED — and the line below IS YOUR BASELINE." >&2
        cat "$_stamp" >&2
        echo "  Same HEAD, same worktree state, same logosc mtime as that run, so" >&2
        echo "  re-running measures nothing new and the rc above is still the answer." >&2
        echo "" >&2
        echo "  ⚠ IF YOU CAME HERE FOR AN OPENING BASELINE: you have it. Quote that" >&2
        echo "  rc, do not treat this exit 2 as breakage, and get on with the work." >&2
        echo "  Measured 2026-08-28: five workflow phases opened with a fresh L4 over" >&2
        echo "  a tree the previous phase had just certified green — an hour of the" >&2
        echo "  session spent re-reading an answer that was already written down." >&2
        echo "" >&2
        echo "  To get a NEW answer, change something: land an edit, rebuild, then ask." >&2
        exit 2
    fi
    if [ "$WITH_IMPORTED" = "1" ]; then
        echo "[test-levels] L4 — core+spec + ALL ~4158 imported ports. ⚠ THE TIER IS UNREVIEWED."
        _log=$(mktemp); bash "$SUMMARY" 2>&1 | tee "$_log"; _rc=${PIPESTATUS[0]}
        _write_state "$_rc" "core+spec+ALL imported (tier unreviewed)" "$_log"
        rm -f "$_log"
        exit $_rc  # lint:exit-ok
    fi
    if [ "$WITH_BC_IMPORTED" = "1" ]; then
        echo "[test-levels] L4 bc — core+spec, plus the borrow-check imported ports"
        _log=$(mktemp); bash "$SUMMARY" -LE imported 2>&1 | tee "$_log"; _rc_core=${PIPESTATUS[0]}
        echo "[test-levels] L4 bc — imported, borrow-check suites only"
        _log2=$(mktemp); bash "$SUMMARY" -L imported -L bc 2>&1 | tee "$_log2"; _rc_bc=${PIPESTATUS[0]}
        _rc_worse=$_rc_bc; [ "$_rc_core" -ne 0 ] && _rc_worse=$_rc_core
        _write_state "$_rc_worse" "core rc=$_rc_core / bc rc=$_rc_bc" "$_log" "$_log2"
        rm -f "$_log" "$_log2"
        exit "$_rc_worse"  # lint:exit-ok
    fi
    echo "[test-levels] L4 — full suite (core+spec; imported excluded, add 'bc' for the borrow-check ports, 'imported-unreviewed' for all)"
    _log=$(mktemp); bash "$SUMMARY" -LE imported 2>&1 | tee "$_log"; _rc=${PIPESTATUS[0]}
    _write_state "$_rc" "core only (imported excluded)" "$_log"
    rm -f "$_log"
    exit $_rc  # lint:exit-ok
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
    # ⚠ `--no-tests=error`. This tier is registered by a `if(EXISTS
    # tests/exhaustive/harness.py)` block, so a moved or deleted generator
    # un-registers it — and `ctest` that matches nothing exits 0. Every level
    # would then print "the enumerator's smoke tier passed (12 684 generated
    # cases)" about 0 cases. Measured on ctest 3.28.3: no match exits 0 without
    # the flag and 8 with it.
    #
    # SELECTED BY LABEL, NOT BY NAME, for the same reason as the gates tier
    # below. `tier_explicit` means exactly "run by name from a bespoke harness
    # block rather than through a tier selector" — which is what this block is —
    # so the smoke tier can declare the truth about when it runs instead of
    # claiming a tier it does not belong to. MEASURED identical to the
    # `-R '^logos_26_exhaustive_smoke$'` it replaces: one test, the same one.
    if ! ctest --no-tests=error --output-on-failure -L '^tier_explicit$'; then
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
# WHAT IS PULLED IN: whatever declares `tier_commit`. THE SCRIPT NO LONGER KNOWS
# ANY GATE NAMES, and that is the entire point of this paragraph — see below.
# 26 tests as this landed, but the count is deliberately NOT written into the
# selector or asserted against a literal: it is a property of the build (the
# logos_07_ir_snapshot_ family is conditional on FileCheck — 12 tests with it,
# one `..._UNAVAILABLE` without, none at all under -DLOGOS_ALLOW_NO_FILECHECK=ON)
# and it changes whenever a gate is added. The count is REPORTED instead, below,
# so a run says what it actually ran.
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
# ⚠ `--no-tests=error`. MEASURED on ctest 3.28.3, this tree: a real run whose
# `-L` matches NOTHING exits 0 without the flag and 8 with it. (The flag does NOT
# affect `-N`, which exits 0 either way — so a listing is never evidence here.)
# A tier label nobody ever applied would otherwise make this block print its line
# and pass having run zero tests. Same failure the enumerator block above
# documents; switching from `-R` to `-L` does not retire the need for the flag,
# it sharpens it. Its failure is the level's failure, like EXH_FAIL.
#
# ⚠ `-L` IS A REGEX, NOT AN EXACT MATCH — MEASURED on this tree: `-L 'uite_i'`
# selects 215 tests (suite_ir 12 + suite_integration 203) where `-L '^suite_ir$'`
# selects 12. Hence the anchors: a bare `-L tier_commit` would also swallow any
# future `tier_commit_*`.
#
# ⚠ `-L` AND `-R` COMPOSE AS AN AND — MEASURED: `-L '^tier_commit$'` alone
# selects 26, and with `-R '^logos_00_'` added it selects 12. That is why this
# block carries NO `-R`: one added here would silently shrink the tier to the
# intersection while still reporting a pass.
#
# ⚠ THE PREDECESSOR OF THIS BLOCK LISTED ITS POPULATION BY NAME, and said so:
#     -R '^logos_00_|^logos_07_ir_snapshot_|^logos_09_rtval_domain$|...'
# with a paragraph admitting the two `logos_09_` names were a stopgap and
# promising that "whoever writes the label retires these two names and this
# paragraph together". This is that commit. A listed population answers "is this
# name in my list?" and never "which tests want to run per commit?", so its
# failure mode is SILENCE: a gate added today matched no alternative, was in
# NEITHER tier, and nothing anywhere said so. That had already been paid for once
# — `logos_09_rtval_domain` was written FOR the value-domain arc, never ran on a
# commit that could break it, and the first such commit broke it, caught only by
# L4 a whole round later.
#
# ⚠ THE LABEL ALONE IS NOT THE FIX; THE CANARY IS. Selecting on a label a gate
# forgot to declare drops it from every tier just as silently — the same defect
# with extra steps. So `cmake/LogosTestTiers.cmake` makes "exactly one tier label
# per non-corpus test" a CONFIGURE-TIME FATAL ERROR, in every directory that
# registers tests, with the set of those directories derived by walking
# SUBDIRECTORIES rather than listed. It is not a test on purpose: it therefore
# has no label of its own to lose, and cannot be unselected by -R, -L or
# LOGOS_NO_GATES=1.
#
# THE MEASURED COST, at the population this landed with: ~9 s of ctest wall on an
# idle box, dominated by logos_00_mlir_gen_bug_ledger (8.7 s) and
# logos_00_sep_symbol_shape (6.3 s), which run in parallel with everything else.
# ⚠ THE SAME COMMAND MEASURED 21.1 s at load ~50 with a sibling worktree running
# its own suite — so this number is a property of an idle box, and a slow gates
# tier is evidence about the machine before it is evidence about the gates.
GATE_FAIL=0
GATE_RAN=""
if [ "${LOGOS_NO_GATES:-0}" != "1" ]; then
    echo "[test-levels] gates — every test declaring 'tier_commit'"
    # ⚠ `-L` ONLY. Adding a `-R` here would AND with the label and silently
    # shrink the tier; the corpus chunks below are separate ctest invocations
    # carrying only `-R`, and the two must not be crossed.
    gate_out=$(ctest --no-tests=error -j"$(nproc)" --output-on-failure \
                     -L '^tier_commit$' 2>&1) || GATE_FAIL=1
    printf '%s\n' "$gate_out"
    # THE TIER MUST REPORT WHAT IT RAN. The old line named a fixed population in
    # prose, so it would have kept saying "logos_00_* + …" no matter what ran.
    # Parse ctest's own summary instead — and treat a MISSING summary as failure,
    # exactly as the corpus chunk loop does: a block that cannot say how many
    # tests it ran has not reported on them.
    GATE_RAN=$(printf '%s\n' "$gate_out" \
               | sed -nE 's/^.*tests passed.*out of ([0-9]+).*$/\1/p' | tail -1)
    if [ -z "$GATE_RAN" ]; then
        echo "***Failed: the gates tier produced no ctest summary line — it cannot"
        echo "  report how many gates it ran, so it is not evidence that they ran."
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
[ "$GATE_FAIL" -ne 0 ] && echo "the gates tier (-L tier_commit) ***Failed (see above)"
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
# ⚠ THE COUNT IS PRINTED, NOT DESCRIBED. These lines used to name a fixed
# population in prose ("logos_00_* / logos_07_ir_snapshot_* / 2 named logos_09_")
# — which stayed word-for-word identical however the tier actually changed, so
# the output was a claim about the selector's source code rather than about the
# run. `$GATE_RAN` is ctest's own count from the run just performed.
if [ "${LOGOS_NO_GATES:-0}" = "1" ]; then
    echo "PLUS: the gates tier was SKIPPED (LOGOS_NO_GATES=1)"
elif [ "$GATE_FAIL" -ne 0 ]; then
    echo "PLUS: the gates tier FAILED (${GATE_RAN:-?} tests declaring tier_commit)"
else
    echo "PLUS: the gates tier passed ($GATE_RAN tests declaring tier_commit)"
fi
# ⚠ `had_fail` IS PART OF THE EXIT STATUS. It used to be equivalent to
# `tot_fail > 0` — the only thing that set it — and the two checks above set it
# for failures that produce NO failed-test count at all: a chunk that reported no
# summary ran zero tests, and a selection larger than the run is a test the suite
# does not know. Both print `***Failed` and both would have left `tot_fail` at 0,
# so the script would have printed the failure and exited 0. A message on stdout
# is not a verdict; the exit code is.
[ "$tot_fail" -eq 0 ] && [ "$had_fail" -eq 0 ] && [ "$EXH_FAIL" -eq 0 ] && [ "$GATE_FAIL" -eq 0 ]
