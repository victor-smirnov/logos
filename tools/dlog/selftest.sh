#!/usr/bin/env bash
# selftest.sh — the KNOWN-ANSWER test for the place-walker rules.
#
# ⚠ NOT A CTEST TEST, ON PURPOSE. It needs souffle and libclang-20-dev, builds a
# worktree of an old revision, and grades a TOOL rather than the compiler. Run it
# by hand after touching lir_facts.cpp, place_walkers.dl, or not_projection.claim.
#
# WHY A CONTROL REVISION AND NOT A FIXTURE. A rule that reports nothing on a
# green tree has proved nothing: silence is not an answer. The only way to know
# these rules BITE is to point them at code whose defects are already known and
# already fixed, and require them to name exactly those. 28fc7c75 predates the
# week's place-walker fixes; every row below became a landed commit.
#
# It is also what licensed replacing the grep/awk extractor with an AST one: the
# new chain had to reproduce these six rows and these three coverage numbers
# before it was allowed to be the only one. A rewrite with no known answer is not
# a replacement — it is a different tool wearing the same name.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
ROOT=$PWD   # ⚠ NOT $OLDPWD: the subshells below rebind it
CTL=28fc7c7509456b034bfd77b093ec68a9ba9f7b3c
command -v souffle >/dev/null || { echo "SKIP: souffle not installed"; exit 0; }
# ⚠ ALWAYS REBUILD. The first version skipped make.sh when the binary existed,
# and then reported the known answer as reproduced while lir_facts.cpp DID NOT
# COMPILE — a green over a stale artefact, the oldest lie in this repo. A build
# failure is a failure, not a skip; only a MISSING toolchain is a skip.
if [ ! -f /usr/lib/llvm-20/include/clang/Tooling/Tooling.h ]; then
    echo "SKIP: libclang-20-dev not installed"; exit 0
fi
bash tools/dlog/make.sh >"${TMPDIR:-/tmp}/dlog_make.$$" 2>&1 || {
    echo "FAIL(2): lir_facts does not build"; cat "${TMPDIR:-/tmp}/dlog_make.$$"; exit 2; }

W=$(mktemp -d)
trap 'git worktree remove --force "$W/wt" >/dev/null 2>&1; rm -rf "$W"' EXIT

# ⚠ THE CONTROL NEEDS A COMPILATION DATABASE, and clang needs the headers the
# build GENERATES: logos_parser.hpp and four more. Configure only — no compile.
# Generate them AS A CLASS (every custom-command .hpp), not by chasing the one
# name the first error happened to print; the second name appeared right after
# the first was fixed, which is how that chase always goes.
git worktree add -q --detach "$W/wt" "$CTL" || exit 2  # lint:git-ok — git is the SOURCE of the control revision's text, not the oracle: the verdict is souffle's
cmake -S "$W/wt" -B "$W/wt/build" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G Ninja >/dev/null 2>&1 || {
    echo "FAIL(2): cannot configure the control revision"; exit 2; }
GEN=$(ninja -C "$W/wt/build" -t targets all 2>/dev/null |
      grep ": CUSTOM_COMMAND" | cut -d: -f1 | grep -E '\.(hpp|h|inc|def)$' | sort -u)
ninja -C "$W/wt/build" $GEN >/dev/null 2>&1 || { echo "FAIL(2): cannot generate headers"; exit 2; }

mkdir -p "$W/f"
(cd "$W/wt" && "$ROOT/build/dlog/lir_facts" -p build \
    --enum=::logos::compiler::lir_schema::expr::Code --out="$W/f" \
    src/compiler/sema_expr.cpp src/compiler/borrow_check.cpp) > "$W/log" 2>&1 || {
    echo "FAIL(2): lir_facts failed on the control revision"; cat "$W/log"; exit 2; }

grep -vE '^\s*(#|$)' tools/dlog/not_projection.claim > "$W/f/not_projection.facts"
echo VarRef > "$W/f/place_root_kind.facts"
(cd "$W/f" && souffle -F. -D. "$ROOT/tools/dlog/place_walkers.dl" >/dev/null 2>&1) || exit 2

RC=0
# The domain is DERIVED from the control revision's own enum — pinning its size
# proves the derivation RAN, and did not fall back to something smaller.
NC=$(wc -l < "$W/f/expr_code.facts"); NP=$(wc -l < "$W/f/projection_kind.csv")
[ "$NC" = 42 ] && [ "$NP" = 5 ] || { echo "FAIL: domain $NC codes / $NP projections, want 42 / 5"; RC=1; }

# ⚠ THE SIX ARE A SUBSET NOW, NOT THE WHOLE ANSWER. walker.facts used to be
# three names typed by hand; the walker set is DERIVED, so the control revision
# yields nineteen walkers and twenty-four findings. The six below are the ones whose
# fixes are landed commits, and they are what proves the chain still BITES.
# Pinning the total separately catches drift without pretending the hand-list
# was ever the complete answer — it never was, and that was the problem.
sort "$W/f/spelling_keyed.csv" > "$W/got"
sort > "$W/want" <<'EOF'
extract_borrow_place	TupleIndex
try_path	Deref
try_path	IndexRead
try_path	SliceIndex
try_path	TupleIndex
value_local_root	SliceIndex
EOF
MISS=$(comm -23 "$W/want" "$W/got")
if [ -n "$MISS" ]; then
    echo "FAIL: the rules no longer name defects whose fixes are landed commits:"
    echo "$MISS" | sed 's/^/      /'
    echo "      A rule that has stopped biting reports a green tree either way."
    RC=1
fi
NF=$(wc -l < "$W/got"); NW=$(wc -l < "$W/f/walker.csv")
[ "$NF" = 24 ] && [ "$NW" = 19 ] || {
    echo "FAIL: $NW walkers / $NF findings on the control, want 19 / 24"; RC=1; }

# try_path's ratio is pinned by itself: 1/5 is the shape of the headline finding,
# and a derivation that selects walkers by "handles >= 2" silently loses exactly
# that row — the worst walker in the tree stops being called a walker.
TP=$(awk -F'\t' '$1=="try_path"{print $2"/"$3}' "$W/f/coverage.csv")
[ "$TP" = "1/5" ] || { echo "FAIL: try_path coverage '$TP', want 1/5"; RC=1; }

[ "$RC" = 0 ] && echo "ok  known answer reproduced on $CTL: 19 walkers, 24 findings incl. the 6 landed, try_path 1/5, domain 42/5"
exit $RC  # lint:exit-ok — RC is set only from explicit checks above
