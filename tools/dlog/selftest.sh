#!/usr/bin/env bash
# selftest.sh — the KNOWN-ANSWER test for tools/dlog.
#
# ⚠ NOT A CTEST TEST, ON PURPOSE. It needs souffle and libclang-20-dev, builds a
# worktree of an old revision, and grades a TOOL rather than the compiler. Run it
# by hand after touching cxx_facts.cpp or any .dl.
#
# WHY A CONTROL REVISION AND NOT A FIXTURE. A rule that reports nothing on a
# green tree has proved nothing: silence is not an answer. The only way to know
# these rules BITE is to point them at code whose defects are already known and
# already fixed, and require them to name exactly those. 28fc7c75 predates the
# week's place-walker fixes; every row below became a landed commit.
#
# It is also what licensed each replacement of the extractor — grep/awk to
# clang, then question-shaped to general. Each rewrite had to reproduce these
# numbers before it was allowed to be the only one. A rewrite with no known
# answer is not a replacement; it is a different tool wearing the same name.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
ROOT=$PWD
CTL=28fc7c7509456b034bfd77b093ec68a9ba9f7b3c
FIX=756aed654059ceb409e0ca3b644af0c34922aaf2   # added the missing Assign-arm check
command -v souffle >/dev/null || { echo "SKIP: souffle not installed"; exit 0; }
[ -f /usr/lib/llvm-20/include/clang/Tooling/Tooling.h ] || {
    echo "SKIP: libclang-20-dev not installed"; exit 0; }
bash tools/dlog/make.sh >"${TMPDIR:-/tmp}/dlog_make.$$" 2>&1 || {
    echo "FAIL(2): cxx_facts does not build"; cat "${TMPDIR:-/tmp}/dlog_make.$$"; exit 2; }

W=$(mktemp -d)
trap 'for d in "$W"/wt-*; do git worktree remove --force "$d" >/dev/null 2>&1; done; rm -rf "$W"' EXIT

# Configure only — no compile — then generate the headers the build makes, AS A
# CLASS. Chasing the one name the first error prints just yields the next name.
prepare() {  # $1 = revision, $2 = dir
    git worktree add -q --detach "$2" "$1" || return 1  # lint:git-ok — git is the SOURCE of the control text, not the oracle: the verdict is souffle's
    cmake -S "$2" -B "$2/build" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G Ninja >/dev/null 2>&1 || return 1
    local gen
    gen=$(ninja -C "$2/build" -t targets all 2>/dev/null |
          grep ": CUSTOM_COMMAND" | cut -d: -f1 | grep -E '\.(hpp|h|inc|def)$' | sort -u)
    ninja -C "$2/build" $gen >/dev/null 2>&1
}
facts() {  # $1 = worktree, $2 = out dir, rest = sources
    local wt=$1 out=$2; shift 2
    mkdir -p "$out"
    (cd "$wt" && "$ROOT/build/dlog/cxx_facts" -p build --out="$out" "$@") >>"$W/log" 2>&1 || return 1
    cp "$ROOT"/tools/dlog/*.dl "$out/"
}

RC=0
prepare "$CTL" "$W/wt-ctl" || { echo "FAIL(2): cannot prepare $CTL"; exit 2; }
facts "$W/wt-ctl" "$W/ctl" src/compiler/sema_expr.cpp src/compiler/borrow_check.cpp || {
    echo "FAIL(2): cxx_facts failed on $CTL"; cat "$W/log"; exit 2; }

grep -vE '^\s*(#|$)' tools/dlog/not_projection.claim > "$W/ctl/not_projection.facts"
(cd "$W/ctl" && souffle -F. -D. -I. "$ROOT/tools/dlog/place_walkers.dl" >/dev/null 2>&1) || {
    echo "FAIL(2): place_walkers.dl failed"; exit 2; }

NC=$(wc -l < "$W/ctl/expr_code.csv"); NP=$(wc -l < "$W/ctl/projection_kind.csv")
NW=$(wc -l < "$W/ctl/walker.csv");    NF=$(wc -l < "$W/ctl/spelling_keyed.csv")
[ "$NC" = 42 ] && [ "$NP" = 5 ] || { echo "FAIL: domain $NC/$NP, want 42/5"; RC=1; }
[ "$NW" = 19 ] && [ "$NF" = 24 ] || { echo "FAIL: $NW walkers / $NF findings, want 19 / 24"; RC=1; }

# ⚠ THE SIX ARE A SUBSET, NOT THE WHOLE ANSWER — the walker set is DERIVED, and
# the hand-list of three names it replaced never was complete. These six are the
# ones whose fixes are landed commits, and they are what proves the chain BITES.
sort > "$W/want" <<'EOF'
extract_borrow_place	TupleIndex
try_path	Deref
try_path	IndexRead
try_path	SliceIndex
try_path	TupleIndex
value_local_root	SliceIndex
EOF
MISS=$(comm -23 "$W/want" <(sort "$W/ctl/spelling_keyed.csv"))
[ -z "$MISS" ] || { echo "FAIL: defects with landed fixes no longer named:"; echo "$MISS"; RC=1; }

# try_path's ratio by itself: 1/5 is the shape of the headline finding, and a
# derivation that selects walkers by "handles >= 2" silently loses exactly that
# row — the worst walker in the tree stops being called a walker.
TP=$(awk -F'\t' '$1=="try_path"{print $2"/"$3}' "$W/ctl/coverage.csv")
[ "$TP" = "1/5" ] || { echo "FAIL: try_path coverage '$TP', want 1/5"; RC=1; }

# ── THE SECOND KNOWN ANSWER: a rule that must DISCRIMINATE ──────────────────
# 756aed65 added the missing field_borrow_conflicts to visit_stmt's Assign arm.
# duty must name it BEFORE and not after. This is the test the CFG dominance
# rule failed — byte-identical output on both sides, because its `sites >= 2`
# selector excluded the very function under test.
for rev in "$FIX^" "$FIX"; do
    tag=$(echo "$rev" | tr -d '^'); [ "$rev" != "$FIX" ] && tag=before || tag=after
    prepare "$rev" "$W/wt-$tag" || { echo "FAIL(2): cannot prepare $rev"; exit 2; }
    facts "$W/wt-$tag" "$W/$tag" src/compiler/borrow_check.cpp || {
        echo "FAIL(2): cxx_facts failed on $rev"; exit 2; }
    grep -vE '^\s*(#|$)' tools/dlog/duty.claim | tr -s ' ' '\t' > "$W/$tag/duty.facts"
    (cd "$W/$tag" && souffle -F. -D. -I. "$ROOT/tools/dlog/duty.dl" >/dev/null 2>&1) || {
        echo "FAIL(2): duty.dl failed on $rev"; exit 2; }
done
B=$(grep -c 'visit_stmt.*Assign.*field_borrow_conflicts' "$W/before/neglects.csv")
A=$(grep -c 'visit_stmt.*Assign.*field_borrow_conflicts' "$W/after/neglects.csv")
[ "$B" = 1 ] && [ "$A" = 0 ] || {
    echo "FAIL: duty does not discriminate across $FIX (before=$B after=$A, want 1/0)"
    echo "      A rule that reports the same thing with and without the defect is not an oracle."
    RC=1; }

[ "$RC" = 0 ] && echo "ok  $CTL: 19 walkers / 24 findings / try_path 1-5 / domain 42-5;" \
                      "duty discriminates across ${FIX:0:8} (1 -> 0)"
exit $RC  # lint:exit-ok — RC is set only from explicit checks above
