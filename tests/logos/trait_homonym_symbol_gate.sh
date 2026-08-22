#!/usr/bin/env bash
# trait_homonym_symbol_gate.sh LOGOSC LIB_DIR
#
# #100 HALF C — THE ASSERTION NO `.expected` CAN MAKE.
#
# The body-injection channel's strongest fact is about the OBJECT FILE, not
# about a diagnostic or an exit code. `lower_impl_block` used to resolve an
# impl's trait with a BARE `traits_.find(trait_name)` while `collect_impl`
# resolved it scope-aware, so the two disagreed about WHICH trait a user
# `trait ExactSizeIterator` denoted: collect registered the user's method list,
# lower walked the STDLIB homonym's and synthesised its default-bodied methods
# into the user's impl.
#
# MEASURED on the pre-fix compiler, from the five-line program below:
#     0000000000000000 T ZqH__is_empty                 ← STDLIB default body
#     0000000000000000 T test.ZqH__len__f__ref_ZqH     ← the only fn declared
# The injected symbol is not merely extra. It is emitted UNQUALIFIED and
# UNMANGLED — no `test.` package prefix, no `__f__sig` suffix, unlike every
# legitimate function beside it — so two packages taking this path collide at
# the LINKER, and any C symbol of that name collides with it too.
#
# ── WHY A GATE AND NOT A FIXTURE ────────────────────────────────────────────
# The corpus fixtures for this half are honest about their limits:
#   pass/trait_homonym_default_body_not_injected.logos — GREEN pre-fix. The
#     injected method was emitted but never entered METHOD RESOLUTION (the
#     collect side was already scoped), so no .logos source could call it.
#   fail/trait_homonym_default_body_injected_fail.logos — GREEN pre-fix, same
#     reason; it guards the second door, it does not witness the first.
#   pass/trait_homonym_default_body_datum.logos — BITES, but only because that
#     particular stdlib body happened to FAIL TO COMPILE. `unknown type 'DView'`
#     is a symptom of the shape, not of the channel.
# A body that compiles leaves no trace a fixture can read. It leaves a symbol.
#
# ── HELD IN THE ABUSE DIRECTION ─────────────────────────────────────────────
# FACT 0 below compiles a CONTROL whose trait name collides with nothing and
# requires the same absence there. If the gate ever passed by measuring nothing
# — wrong path, `nm` missing, compile silently failing — the control's POSITIVE
# assertion (its own `len` symbol IS present, package-qualified) fails first and
# the gate exits 2 rather than green.
#
# EXIT CODES: 0 pass, 1 an injected symbol is present, 2 THE GATE COULD NOT MEASURE.
set -u

LOGOSC=${1:?usage: trait_homonym_symbol_gate.sh <logosc> <lib dir>}
LIBDIR=${2:?usage: trait_homonym_symbol_gate.sh <logosc> <lib dir>}

[ -x "$LOGOSC" ] || { echo "FAIL(2): logosc does not resolve: $LOGOSC"; exit 2; }
[ -d "$LIBDIR" ] || { echo "FAIL(2): lib dir does not resolve: $LIBDIR"; exit 2; }
command -v nm >/dev/null || { echo "FAIL(2): no nm — the symbol table cannot be read."; exit 2; }

TMPD=$(mktemp -d) || { echo "FAIL(2): no temp dir"; exit 2; }
trap 'rm -rf "$TMPD"' EXIT
export LOGOS_LIB_DIR="$LIBDIR"

# $1 = trait name to declare.  Writes $TMPD/$1.o, echoes nothing.
build_one() {
    local T="$1"
    cat > "$TMPD/$T.logos" <<EOF
package test;
trait $T { fn len(&self) -> i64; }
struct ZqH { w: i64 }
impl $T for ZqH { fn len(&self) -> i64 { return self.w + 7i64; } }
fn main() -> i32 { let h = ZqH{w:20i64}; return h.len() as i32; }
EOF
    # ⚠ rc read directly, NOT after a $(...) — command substitution clobbers $?.
    "$LOGOSC" "$TMPD/$T.logos" -o "$TMPD/$T.o" > "$TMPD/$T.out" 2>&1
    return $?
}

# ── FACT 0: THE GATE'S OWN LIVENESS ─────────────────────────────────────────
# A control whose trait name collides with NOTHING. It must compile, and its own
# `len` must appear PACKAGE-QUALIFIED in the symbol table. This is the positive
# half: it proves the compile ran, the object exists, and `nm` reads it. Without
# it "no injected symbol found" is indistinguishable from "nothing was measured".
if ! build_one ZqExactSizeUniqueGate; then
    echo "FAIL(2): the control program did not compile — the gate measured nothing."
    sed -n '1,20p' "$TMPD/ZqExactSizeUniqueGate.out"
    exit 2
fi
CTL_SYMS=$(nm "$TMPD/ZqExactSizeUniqueGate.o" 2>/dev/null)
if [ -z "$CTL_SYMS" ]; then
    echo "FAIL(2): nm read no symbols from the control object — the gate is blind."
    exit 2
fi
if ! printf '%s\n' "$CTL_SYMS" | grep -q 'test\.ZqH__len'; then
    echo "FAIL(2): the control's own 'test.ZqH__len' is absent from its object."
    echo "  The gate's matcher is not seeing what it must see; a green below"
    echo "  would certify nothing. Symbols read:"
    printf '%s\n' "$CTL_SYMS" | sed 's/^/    /'
    exit 2
fi
# The control must ALSO carry no injected symbol — it collides with nothing, so
# a hit here means the matcher is matching something other than an injection.
if printf '%s\n' "$CTL_SYMS" | grep -qE '^[0-9a-f]+ T ZqH__'; then
    echo "FAIL(2): the CONTROL carries an unqualified 'ZqH__' symbol."
    echo "  Its trait name collides with nothing, so this cannot be a homonym"
    echo "  injection — the matcher below is over-broad and its verdict is void."
    printf '%s\n' "$CTL_SYMS" | grep -E '^[0-9a-f]+ T ZqH__' | sed 's/^/    /'
    exit 2
fi

# ── FACT 1: THE SUBJECT ─────────────────────────────────────────────────────
# `ExactSizeIterator` is a real stdlib trait (stdlib/lang/iter/iter.logos)
# carrying a default-bodied `is_empty`. The user trait below declares ONE
# method and no default bodies at all, so the object may contain no unqualified
# `ZqH__*` global whatsoever.
if ! build_one ExactSizeIterator; then
    echo "FAIL(1): the homonym program did not compile."
    echo "  A user trait sharing a stdlib trait's bare name must compile; this is"
    echo "  the #100 half-A/half-C shape and its refusal is the defect, not a pass."
    sed -n '1,20p' "$TMPD/ExactSizeIterator.out"
    exit 1
fi
SUBJ_SYMS=$(nm "$TMPD/ExactSizeIterator.o" 2>/dev/null)
if [ -z "$SUBJ_SYMS" ]; then
    echo "FAIL(2): nm read no symbols from the subject object."
    exit 2
fi
INJECTED=$(printf '%s\n' "$SUBJ_SYMS" | grep -E '^[0-9a-f]+ T ZqH__' || true)
if [ -n "$INJECTED" ]; then
    echo "FAIL(1): a stdlib DEFAULT METHOD BODY was injected into a user impl."
    echo
    echo "  The program declares one trait with one method, 'len', and implements"
    echo "  it. These UNQUALIFIED globals are in its object file and it wrote none"
    echo "  of them — they are the stdlib 'ExactSizeIterator' default bodies,"
    echo "  lowered against a user type across a package boundary:"
    printf '%s\n' "$INJECTED" | sed 's/^/    /'
    echo
    echo "  Note the missing 'test.' package prefix and '__f__sig' suffix that"
    echo "  every legitimate function in the same object carries: these symbols"
    echo "  collide at the LINKER with any other package doing the same."
    echo
    echo "  ROOT: a consult site resolving the impl's trait with a BARE"
    echo "  traits_.find(trait_name) while collect_impl resolves it with"
    echo "  find_trait_iter_scoped. The two must agree. See #100."
    exit 1
fi

echo "PASS: no stdlib default body injected into the user impl (subject + control measured)."
exit 0
