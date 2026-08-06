#!/usr/bin/env bash
# abi_closure_gate — the ABI spec must be CLOSED under reachability.
#
#   abi_closure_gate.sh <logosc> <lib-dir> <exempt-file>
#
# THE DEFECT THIS EXISTS FOR, MEASURED. `logos.mem.deem.QEnv` is recorded in
# abi/logos.abi carrying `f_ptrs:[fn(&[RtVal]) -> RtVal; 8]`, and `RtVal` — a
# `pub enum`, the entire UDF/UDA registration surface — had NO record, because
# the deem policy admitted five type names by allowlist and nothing checked that
# an admitted type only names admitted types. Consequence, re-measured on this
# tree before the fix: building an `F32(f32)` arm into the real `RtVal` produced
# a BYTE-IDENTICAL 12881-line spec and `logosc --abi-diff` exited 0 with
# "VERDICT: ABI-PRESERVING". The spec's own gate could not see a payload retype
# of a public type. Same run after the fix: the RtVal record changes and
# --abi-diff exits 1 with ABI-BREAKING.
#
# WHAT IS CHECKED: every nominal type named by a recorded field list or enum
# payload must itself have a record, or be named in the exemption file with the
# reason the EMITTER derives for it this build. Derived structurally at emit
# time from the TypeRefs (`.abi-closure` sidecars) — never by parsing the spec
# text, which cannot even name its own field types: records are keyed
# fully-qualified while field types print bare, and Weak/Ordering/Ident/Error/
# DirEntry/ControlFlow/Bytes are each already shared across records.
#
# ⚠ CANARIES, IN THE SAME RUN, THROUGH THE SAME TOOL.
#
#   1. THE EXEMPTIONS THEMSELVES. `--abi-closure` fails an exemption that
#      matches no violation. So a closure walk that has stopped reaching — the
#      one failure mode where this gate would print "CLOSED" while seeing
#      nothing — turns every exemption stale and comes back RED. A dead check
#      cannot pass. (It also refuses to run at all on zero reference edges.)
#   2. A SYNTHETIC VIOLATION THROUGH THE SAME RESOLVER. A scratch lib dir whose
#      records name a type that deliberately has none must come back as a
#      VIOLATION, exit 1. Pins the resolver from the failing side; canary 1
#      pins it from the passing side.
#   3. AN EXEMPTION WHOSE REASON NO LONGER MATCHES fails. Checked here by
#      handing the real run an exemption file with a deliberately wrong reason
#      and requiring EXEMPTION-REASON-CHANGED — so an exemption cannot outlive
#      the cause it claims.
set -uo pipefail

LOGOSC=${1:?usage: abi_closure_gate.sh <logosc> <lib-dir> <exempt-file>}
LIB_DIR=${2:?}
EXEMPT=${3:?}

fail() { echo "::error:: $*"; exit 1; }

[ -x "$LOGOSC" ] || fail "logosc not executable: $LOGOSC"
[ -d "$LIB_DIR" ] || fail "lib dir not found: $LIB_DIR"
[ -f "$EXEMPT" ] || fail "exemption file not found: $EXEMPT"

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

# ── the real check ────────────────────────────────────────────────────────────
out="$workdir/real.txt"
"$LOGOSC" --abi-closure -L "$LIB_DIR" --abi-exempt "$EXEMPT" > "$out" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "::error:: the ABI spec is NOT closed — a recorded type names a type with"
    echo "          no record of its own. Either record it, or add it to"
    echo "          $EXEMPT with the reason printed below."
    cat "$out"
    exit 1
fi
grep -q '^abi-closure: CLOSED' "$out" || {
    echo "::error:: --abi-closure exited 0 without printing its verdict line."
    cat "$out"; exit 1; }
echo "abi-closure: $(sed -n '1p' "$out")"

# ── CANARY 2: a synthetic violation must come back named ──────────────────────
# Same binary, same `--abi-closure`, same parse — only the inputs are fabricated.
# `Absent` has a record nowhere, so the resolver must report it.
mkdir -p "$workdir/fake"
printf 'type\tcanary.pkg.Holder\tfields=[a:Absent]\n' > "$workdir/fake/x.a.abi-layout"
{
    printf 'decl\tcanary.pkg.Holder\trecorded\n'
    printf 'decl\tcanary.pkg.Absent\tnot-pub\n'
    printf 'ref\tcanary.pkg.Holder\tfield-byval\tcanary.pkg.Absent\n'
} > "$workdir/fake/x.a.abi-closure"
cout="$workdir/canary.txt"
# ⚠ `--no-system` IS LOAD-BEARING, NOT TIDINESS. main.cpp appends the resolved
# system lib dir to the search paths AFTER any `-L`, so without it this
# "fabricated scratch dir" run silently merges the real records and the real
# edges. MEASURED at the commit that introduced this gate: a fake dir holding
# ZERO edges still satisfied the assertion below, because two REAL unexempted
# violations answered it. The canary could not fail — inside an artifact whose
# own thesis is that a canary which cannot fail is a check that is not looking.
"$LOGOSC" --abi-closure --no-system -L "$workdir/fake" > "$cout" 2>&1
crc=$?
if [ "$crc" -eq 0 ] || ! grep -q '^VIOLATION[[:space:]]canary\.pkg\.Absent' "$cout"; then
    echo "::error:: CANARY 'synthetic violation' NOT CAUGHT — a fabricated record naming"
    echo "          a type with no record came back rc=$crc without a VIOLATION line."
    echo "          The clean run above is therefore not evidence. GATE BROKEN."
    cat "$cout"
    exit 1
fi
# ...and the by-value refinement must be load-bearing, not decoration: the SAME
# fabricated `not-pub` type reached only through a pointer must come back with
# the narrowed reason, which is the only reason an exemption may claim.
printf 'ref\tcanary.pkg.Holder\tfield-indirect\tcanary.pkg.Absent\n' > "$workdir/fake/x.a.abi-closure.tmp"
{
    printf 'decl\tcanary.pkg.Holder\trecorded\n'
    printf 'decl\tcanary.pkg.Absent\tnot-pub\n'
    printf 'ref\tcanary.pkg.Holder\tfield-indirect\tcanary.pkg.Absent\n'
} > "$workdir/fake/x.a.abi-closure"
rm -f "$workdir/fake/x.a.abi-closure.tmp"
# ⚠ `--no-system` IS LOAD-BEARING, NOT TIDINESS. main.cpp appends the resolved
# system lib dir to the search paths AFTER any `-L`, so without it this
# "fabricated scratch dir" run silently merges the real records and the real
# edges. MEASURED at the commit that introduced this gate: a fake dir holding
# ZERO edges still satisfied the assertion below, because two REAL unexempted
# violations answered it. The canary could not fail — inside an artifact whose
# own thesis is that a canary which cannot fail is a check that is not looking.
"$LOGOSC" --abi-closure --no-system -L "$workdir/fake" > "$cout" 2>&1
grep -q 'reason=not-pub-behind-indirection' "$cout" || {
    echo "::error:: CANARY 'indirection narrowing' NOT CAUGHT — a not-pub type reached"
    echo "          only through a pointer did not derive the narrowed reason, so the"
    echo "          exemptions in $EXEMPT are matching a reason nothing computes."
    cat "$cout"; exit 1; }

# ── CANARY 3: an exemption whose reason drifted must FAIL ─────────────────────
sed 's/\tnot-pub-behind-indirection$/\tno-longer-true/' "$EXEMPT" > "$workdir/wrong.exempt"
if ! cmp -s "$EXEMPT" "$workdir/wrong.exempt"; then
    "$LOGOSC" --abi-closure -L "$LIB_DIR" --abi-exempt "$workdir/wrong.exempt" > "$workdir/wrong.txt" 2>&1
    wrc=$?
    if [ "$wrc" -eq 0 ] || ! grep -q '^EXEMPTION-REASON-CHANGED' "$workdir/wrong.txt"; then
        echo "::error:: CANARY 'exemption reason' NOT CAUGHT — an exemption claiming a"
        echo "          reason the emitter does not derive came back rc=$wrc. Exemptions"
        echo "          in $EXEMPT are then unchecked prose. GATE BROKEN."
        cat "$workdir/wrong.txt"
        exit 1
    fi
else
    fail "CANARY 3 could not mutate any exemption reason in $EXEMPT — the file's"$'\n'"          format changed and this canary silently stopped testing anything."
fi

echo "abi_closure_gate: OK (closed; 3 canaries caught)"
exit 0
