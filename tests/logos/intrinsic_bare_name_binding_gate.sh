#!/usr/bin/env bash
# intrinsic_bare_name_binding_gate.sh LOGOSC PASS_DIR
#
# A BUILTIN NAME IS NOT AN IDENTITY — the claims an exit code cannot see.
#
# The three fixtures this gate reads already assert VALUES through run_test.sh,
# and those values are the primary oracle. But two facts of the fix are
# invisible to a value comparison, and both were the actual defect:
#
#   (a) WHICH SYMBOL THE CALL SITE BINDS. A program can print the right number
#       for the wrong reason. The pre-fix compiler emitted the user's
#       `fn popcount_u64` as a DEFINED symbol and never called it — `main` was
#       const-folded from llvm.intr.ctpop — so "the definition exists" is not
#       evidence. This gate asserts a RELOCATION out of `main` naming the
#       package-qualified user symbol.
#
#   (b) THAT THE INTRINSIC ARM STILL FIRES, AND THAT ITS ONE EXEMPTION IS NOT
#       A HATCH. An `extern fn` of an intrinsic name is deliberately NOT a
#       shadow (it declares the very runtime symbol the op replaces), so the
#       abuse program — a user re-declaring `extern fn logos_atomic_fetch_add64`
#       — must still lower to an MLIR atomic RMW and leave NO undefined
#       reference to the assembly stub. An exit code cannot tell an inlined
#       atomic from a call to a stub that computes the same thing.
#
# ⚠ nm/objdump output goes to a FILE and is matched there — never
# `objdump … | grep -q`, which under `pipefail` reports the producer's SIGPIPE
# as the pipeline's status and turns a present symbol into an absent one.
set -euo pipefail

LOGOSC="${1:?logosc}"
PASS_DIR="${2:?pass corpus dir}"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fail=0
checks=0

build() {  # build <fixture-base>
    local base="$1"
    if ! "$LOGOSC" "$PASS_DIR/$base.logos" -o "$TMPD/$base.o" >/dev/null 2>"$TMPD/err.txt"; then
        echo "FAIL: $base did not compile:"
        cat "$TMPD/err.txt"
        fail=1
        return 1
    fi
    # ⚠ ONLY the relocation lines. `objdump -dr` also prints a header line
    # `0000… <test$popcount_u64__f__u64>:` for every DEFINED symbol, and the
    # pre-fix compiler emitted exactly that definition while calling llvm's
    # ctpop instead — so a grep over the whole disassembly matches on the
    # defect and the gate reports OK. MEASURED: it did, on the control revert.
    objdump -dr "$TMPD/$base.o" | grep -E 'R_X86_64_(PLT32|PC32|64|32S?)' \
        > "$TMPD/$base.reloc" || true
    nm -u "$TMPD/$base.o" > "$TMPD/$base.undef"
    return 0
}

want() {   # want <file> <substring> <why>
    checks=$((checks + 1))
    if ! grep -q -F -- "$2" "$1"; then
        echo "FAIL: $1 does not contain '$2'"
        echo "      $3"
        fail=1
    fi
}

forbid() { # forbid <file> <substring> <why>
    checks=$((checks + 1))
    if grep -q -F -- "$2" "$1"; then
        echo "FAIL: $1 contains '$2', which must not be there"
        echo "      $3"
        fail=1
    fi
}

# ── 1. the sema-intercepted family binds the user's definition ──────────────
# MEASURED before the fix: `T test$popcount_u64__f__u64` DEFINED, zero
# relocations naming it, and main const-folded to popcount(3) = 2.
if build intrinsic_bare_name_homonym_sema; then
    want "$TMPD/intrinsic_bare_name_homonym_sema.reloc" \
         'test$popcount_u64__f__u64' \
         "the call site must RELOCATE to the user's fn; before the fix the symbol was defined and never referenced"
fi

# ── 2. the mlir-gen-only family binds the user's definition ─────────────────
# MEASURED before the fix: main held an inline atomic load of the pointee and
# no call at all; `test\$logos_atomic_load32__f__pcst_i32` was defined, unused.
if build intrinsic_bare_name_homonym_mlirgen; then
    want "$TMPD/intrinsic_bare_name_homonym_mlirgen.reloc" \
         'test$logos_atomic_load32__f__pcst_i32' \
         "the call site must RELOCATE to the user's fn; the package-stripping match used to swallow it"
fi

# ── 3. the arm still fires, and the extern exemption is not a hatch ─────────
if build intrinsic_bare_name_no_homonym_arm_fires; then
    # The extern declaration is NOT a shadow: no call to the assembly stub.
    forbid "$TMPD/intrinsic_bare_name_no_homonym_arm_fires.undef" \
           'logos_atomic_fetch_add64' \
           "an extern declaration of an intrinsic name must still lower to an MLIR atomic RMW, not a call to stdlib/rt's stub"
    # pdep_u64 reached the intrinsic: on a generic target it dispatches to the
    # cpuid fallback, which IS an undefined reference to the runtime symbol.
    # Nothing but the intercept can produce that name.
    want "$TMPD/intrinsic_bare_name_no_homonym_arm_fires.undef" \
         'logos_pdep_u64' \
         "pdep_u64 must still reach the intrinsic intercept (generic target => the runtime cpuid fallback)"
    # And no user symbol was minted for any of the intrinsic names.
    forbid "$TMPD/intrinsic_bare_name_no_homonym_arm_fires.reloc" \
           'test$popcount_u64' \
           "there is no user definition in this fixture; a symbol of that name would mean the guard invented one"
fi

if [ "$fail" != 0 ]; then
    echo "intrinsic_bare_name_binding_gate: FAILED ($checks checks)"
    exit 1
fi
echo "intrinsic_bare_name_binding_gate: OK ($checks checks)"
