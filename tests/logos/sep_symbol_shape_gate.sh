#!/usr/bin/env bash
# sep_symbol_shape_gate.sh LOGOSC PASS_DIR
#
# THE SEPARATOR-CLASS CLAIMS THAT AN EXIT CODE CANNOT SEE.
#
# Three of the separator fixtures RUN CORRECTLY on a compiler that emits the
# WRONG symbol, because the definition and the call site agree on the same
# wrong spelling. `run_test.sh` compares an exit code and stdout, so for those
# programs it is blind by construction — a shape gate must assert TEXT.
#
# Each claim below was MEASURED on the pre-fix compiler and on the fixed one;
# the recorded "before" is in the comment beside it. A gate that only asserted
# the after would pass equally on a compiler that never had the defect and on
# one where the fixture stopped exercising it, so every claim is paired with a
# FORBIDDEN string: the exact spelling the defect produced.
set -euo pipefail

LOGOSC="${1:?logosc}"
PASS_DIR="${2:?pass corpus dir}"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fail=0
checks=0

# Materialise the defined-symbol list of one fixture. `nm` output is written to
# a FILE and matched there — NOT `nm … | grep -q`, which under `pipefail` turns
# `nm`'s SIGPIPE into the pipeline's status and reports a present symbol absent.
syms_of() {
    local base="$1" out="$2"
    "$LOGOSC" "$PASS_DIR/$base.logos" -o "$TMPD/$base.o" >/dev/null 2>"$TMPD/err.txt"
    nm -g "$TMPD/$base.o" > "$out"
}

want() {   # want <symfile> <fixture> <substring> <why>
    checks=$((checks + 1))
    if ! grep -q -F -- "$3" "$2"; then
        echo "FAIL: $1: no emitted symbol contains '$3'"
        echo "      $4"
        fail=1
    fi
}

forbid() { # forbid <fixture> <symfile> <substring> <why>
    checks=$((checks + 1))
    if grep -q -F -- "$3" "$2"; then
        echo "FAIL: $1: an emitted symbol contains '$3', which must not exist"
        echo "      $4"
        fail=1
    fi
}

# ── 1. a method named `a__g__b` on a GENERIC struct ──────────────────────────
# MEASURED before: tr.w2$G1$i64__a__g__b__g__b__g__ref_w2$G1$T   ← `__g__b` twice
# MEASURED after:  tr.w2$G1$i64__a__g__b__g__ref_w2$G1$T
# The signature was recomposed from a short name recovered by cutting the
# template name at its first `__g__`, so the method's own `__g__b` was both
# kept as the short name AND re-appended as the signature.
syms_of sep_method_dunder_g "$TMPD/g.txt"
want sep_method_dunder_g "$TMPD/g.txt" 'w2$G1$i64__a__g__b__g__ref_w2$G1$T' \
     "the generic method's concrete instance must join owner, carried method base and signature exactly once"
forbid sep_method_dunder_g "$TMPD/g.txt" 'a__g__b__g__b__g__' \
     "a DOUBLED signature: the method name was cut at its own '__g__' and the fragment re-appended"

# ── 2. a method named `a__f__b` on a GENERIC struct ──────────────────────────
# MEASURED before: NO symbol containing `a__f__b` at all — the method was never
#                  emitted, the caller's `let` was dropped, and the program
#                  SIGSEGVed while logosc exited 0.
# MEASURED after:  tr.w$G1$i64__a__f__b__g__ref_w$G1$T
syms_of sep_method_dunder_f "$TMPD/f.txt"
want sep_method_dunder_f "$TMPD/f.txt" 'w$G1$i64__a__f__b__g__ref_w$G1$T' \
     "the method must be emitted at all; before the fix no symbol carried its name"
forbid sep_method_dunder_f "$TMPD/f.txt" 'a__f__b__f__b' \
     "the same doubled-signature shape as (1), on the '__f__' side"

# ── 3. a free fn `a__b` beside `impl a { fn b }` ─────────────────────────────
# MEASURED before: BOTH were mangled as METHODS —
#                    tr.a__b__f__ref_a__i64        (the method)
#                    tr.a__b__f__ref_a__i64__i64   (the FREE FN, wrongly `.`-joined)
# MEASURED after:  the free fn takes the free-fn `$` form, so the module policy
#                  that applies to free fns (and not to methods) reaches it:
#                    tr$a__b__f__ref_a__i64__i64
#                    tr.a__b__f__ref_a__i64
# "Is this a method" is a FACT the collector holds, not a property of whether
# the spelling contains `__`.
syms_of sep_free_fn_dunder "$TMPD/ff.txt"
want sep_free_fn_dunder "$TMPD/ff.txt" 'tr$a__b__f__ref_a__i64__i64' \
     "the FREE fn must keep the free-fn '\$'-joined form"
want sep_free_fn_dunder "$TMPD/ff.txt" 'tr.a__b__f__ref_a__i64' \
     "the METHOD must keep the method '.'-joined form"
forbid sep_free_fn_dunder "$TMPD/ff.txt" 'tr.a__b__f__ref_a__i64__i64' \
     "the free fn mangled as a method — the classification came from the spelling"

# ── 4. the mangled TYPE ARGUMENT of a `&dyn` receiver ────────────────────────
# MEASURED before: no `__logos_vtable__Sp__box_$G1$k_` and no
#                  `__drop_in_place__box_$G1$k_` in the object AT ALL —
#                  compile exit 0, ZERO stderr, program SIGSEGV. The owner had
#                  been cut at the first `__` AFTER `$G`, which lands inside the
#                  ARGUMENT `k_` (`box_$G1$k___s…` → `box_$G1$k`).
# MEASURED after:  both symbols present, spelled with the argument's own '_'.
# The control argument `k` passed both before and after, so only the `_`/`__`
# spellings carry the claim.
syms_of sep_dyn_typearg_trailing_us "$TMPD/dy.txt"
want sep_dyn_typearg_trailing_us "$TMPD/dy.txt" '__logos_vtable__Sp__box_$G1$k_' \
     "a vtable must exist for the instance whose TYPE ARGUMENT ends in '_'"
want sep_dyn_typearg_trailing_us "$TMPD/dy.txt" '__logos_vtable__Sp__box_$G1$k__j' \
     "and for the one whose TYPE ARGUMENT contains '__'"
want sep_dyn_typearg_trailing_us "$TMPD/dy.txt" '__logos_vtable__Sp__b__x$G1$k_' \
     "owner AND argument may both carry '__' — neither is a boundary"
forbid sep_dyn_typearg_trailing_us "$TMPD/dy.txt" '__logos_vtable__Sp__box_$G1$k__s' \
     "the vtable keyed on an owner cut INSIDE the type argument"

# ── 5. the bound-fingerprint instance on an owner ending in '_' ──────────────
# MEASURED before: `tr.arr_$G1$i64___pick__g__…` — THREE underscores, because
#                  the method rename kept `mn.substr(mn.find("__"))` and that
#                  find landed inside `arr_`. Nothing called it; SIGSEGV, exit 0.
# MEASURED after:  `tr.arr_$G1$i64__pick__g__…`, the composition every other
#                  instance performs.
syms_of sep_bounded_spec_trailing_us "$TMPD/bs.txt"
want sep_bounded_spec_trailing_us "$TMPD/bs.txt" 'tr.arr_$G1$i64__pick' \
     "the instance method must be <concrete>'__'<method>, joined exactly once"
forbid sep_bounded_spec_trailing_us "$TMPD/bs.txt" 'tr.arr_$G1$i64___pick' \
     "THREE underscores: the owner's own trailing '_' was re-emitted as separator"

# ── 6. a method-generic method whose name contains `__` ─────────────────────
# MEASURED before: the CALL `tr.w__a__b__g__ref_w$G1$T__U__i64__i32` was
#                  emitted and the DEFINITION was not — MLIR verifier error.
syms_of sep_method_dunder_g_generic "$TMPD/mg.txt"
want sep_method_dunder_g_generic "$TMPD/mg.txt" 'w__a__b__g__ref_w$G1$T__U__i64__i32' \
     "the method-generic specialisation must be DEFINED, not only called"

# ── THE INSTRUMENT, PROVED IN THE SAME RUN ───────────────────────────────────
# Every check above is a `grep` over an `nm` dump. If the compile silently
# produced no object, or `nm` printed nothing, every `want` would fail loudly
# but every `forbid` would pass VACUOUSLY — "no forbidden symbol" and "I could
# not look" are the same output. So assert a symbol that must be there.
# EVERY dump a `forbid` reads must be proved live, not just the first one.
for pair in "sep_free_fn_dunder:$TMPD/ff.txt" \
            "sep_dyn_typearg_trailing_us:$TMPD/dy.txt" \
            "sep_bounded_spec_trailing_us:$TMPD/bs.txt" \
            "sep_method_dunder_g:$TMPD/g.txt" \
            "sep_method_dunder_f:$TMPD/f.txt" \
            "sep_method_dunder_g_generic:$TMPD/mg.txt"; do
    fixture="${pair%%:*}"; dump="${pair#*:}"
    checks=$((checks + 1))
    if ! grep -q -E '(^| )T main$' "$dump"; then
        echo "GATE BROKEN: the symbol dump for $fixture does not define 'main'."
        echo "  Either the compile produced no object or nm printed nothing, in which"
        echo "  case every 'must not contain' check above passed by looking at an"
        echo "  empty file. This gate's verdict is about a measurement that did not happen."
        exit 4
    fi
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "OK: separator-class symbol TEXT holds — $checks claim(s), each paired with"
echo "    the exact spelling the defect produced, instrument proved live in the same run."
