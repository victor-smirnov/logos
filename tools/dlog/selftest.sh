#!/usr/bin/env bash
# selftest.sh — the KNOWN-ANSWER test for the place-walker rules.
#
# ⚠ NOT A CTEST TEST, ON PURPOSE. It needs souffle, it checks out an old
# revision's sources, and it grades a TOOL rather than the compiler. Run it by
# hand after touching extract.sh, the rules, or not_projection.claim.
#
# WHY A CONTROL REVISION AND NOT A FIXTURE. A rule that reports nothing on a
# green tree has proved nothing: silence is not an answer. The only way to know
# these rules BITE is to point them at code whose defects are already known and
# already fixed, and require them to name exactly those. 28fc7c75 predates the
# week's place-walker fixes; every row below became a landed commit.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
ROOT=$PWD   # ⚠ NOT $OLDPWD: the `(cd f && …)` subshell below rebinds it to $W
CTL=28fc7c7509456b034bfd77b093ec68a9ba9f7b3c
command -v souffle >/dev/null || { echo "SKIP: souffle not installed"; exit 0; }

W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$W/src" "$W/inc/logos/compiler"
for f in sema_expr.cpp borrow_check.cpp; do git show "$CTL:src/compiler/$f" > "$W/src/$f" || exit 2; done  # lint:git-ok — git is the SOURCE of the control revision's text here, not the oracle: the verdict is souffle's
git show "$CTL:include/logos/compiler/lir_schema.hpp" > "$W/inc/logos/compiler/lir_schema.hpp" || exit 2  # lint:git-ok — git is the SOURCE of the control revision's text here, not the oracle: the verdict is souffle's

cd "$W" || exit 2
LOGOS_LIR_SCHEMA=inc/logos/compiler/lir_schema.hpp \
    bash "$ROOT/tools/dlog/extract.sh" f src/sema_expr.cpp src/borrow_check.cpp >/dev/null || exit 2
(cd f && souffle -F. -D. "$ROOT/tools/dlog/place_walkers.dl" >/dev/null 2>&1) || exit 2

# The domain is DERIVED from the control revision's own schema — pinning its
# size proves the derivation ran, and did not silently fall back to a default.
NC=$(wc -l < f/expr_code.facts); NP=$(wc -l < f/projection_kind.csv)
RC=0
[ "$NC" = 42 ] && [ "$NP" = 5 ] || { echo "FAIL: domain $NC codes / $NP projections, want 42 / 5"; RC=1; }

# Each row is a defect that was found, fixed and pinned this week.
sort f/spelling_keyed.csv > got
sort > want <<'EOF'
extract_borrow_place	TupleIndex
try_path	Deref
try_path	IndexRead
try_path	SliceIndex
try_path	TupleIndex
value_local_root	SliceIndex
EOF
if ! diff -u want got; then
    echo "FAIL: the rules no longer reproduce the known answer on $CTL."
    echo "      A rule that has stopped biting reports a green tree either way."
    RC=1
fi
[ "$RC" = 0 ] && echo "ok  known answer reproduced on $CTL: 6 findings, domain 42/5"
exit $RC  # lint:exit-ok — RC is set only from explicit checks above
