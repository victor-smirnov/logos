#!/usr/bin/env bash
# layout_engine_agreement_gate.sh LOGOSC LIB_DIR
#
# THE ENGINES THAT SIZE A VALUE MUST AGREE — ALL OF THEM — AND THE CHECK MUST
# HAVE LOOKED.
#
# `verify_layout_engines()` runs inside every compile and compares, for every
# registered struct type:
#
#   A  `layout_of` / `struct_def_layout`   — over TypeRef; `size_of`, alloca
#                                            sizes, container strides;
#   B  `mlir_abi_size` / `mlir_field_offset` — over emitted MLIR types; every
#                                            value-copy memcpy byte count and
#                                            the DWARF member offsets;
#   C  `llvm::DataLayout` on the mirrored `llvm::Type` — the layout the object
#                                            file is actually emitted with;
#   D  `sema_abi_layout`                    — the byte offset at which a custom
#                                            DST's `[T]` tail begins and at
#                                            which `offset_of!` points;
#   E  `mono_abi_layout`                    — the same offsets, after mono.
#
# A disagreement is a hard compile error naming the type and both answers.
#
# D and E do not exist any more when C is reachable: they run and are consumed
# before mlir-gen. A comparison that cannot reach an engine reports "no
# disagreements" about an engine it never asked — the exact shape of the failure
# this gate is about. So D and E RECORD every answer (layout_law.cpp's ledger)
# keyed by `layout::type_key`, and the record is checked against C here. The
# first run of that check went straight to red on the BASELINE program, naming
# four stdlib types where sema's answer and the object file's differed —
# `PanicInfo` 24 against 48, and three `*Error` structs 8 against 4.
#
# What "the compiler compiled" does NOT carry is that anything was LOOKED AT: a
# check that silently observes zero types is indistinguishable from a check that
# passes. Two rounds of exactly that shipped — `{i32,i64}` sized 12 against
# ISel's 16, then `{i56,i8,i64}` sized 16 against LLVM's 24 — and both were
# found by running programs, not by a gate.
#
# This gate therefore declares the MINIMUM IT MUST OBSERVE and fails when it
# observes less:
#
#   * a LATTICE program, generated here, whose types are the product of
#     (scalar × struct shape) AND of the COMPOSITION shapes — union, tagged
#     enum, C-like enum, niche-packed enum, zero-sized payload, generic
#     instantiation, and nestings of those. The old lattice was scalars only,
#     which is why three engines could be missing their whole `is_union()`
#     branch behind a green gate.
#   * PER-ENGINE floors on the census. A total lets one engine go silent while
#     another carries the number; `sema_abi_layout 0` must be red, and it is.
#   * an ORACLE PROGRAM, generated here, that RUNS: for every prefix shape it
#     measures where a custom DST's `[T]` tail actually lands by writing through
#     the fat pointer and scanning, and measures `offset_of!` against a POINTER
#     DIFFERENCE. Both are facts about memory, not claims by an engine.
#   * the authored fixtures COMPILE AND RUN, exit 0.
#   * zero disagreements, and the report LINE PRESENT — a missing line means the
#     check did not run, which is red, not green.
#   * NO TRANSLATION UNIT OUTSIDE THE TWO THAT MAY CAN EVEN ASK A `DataLayout`.
#     The previous form of this assertion named CALL SPELLINGS (`dl.getTypeSize`)
#     and an adversarial reader got past it by writing `dl->getTypeSize`. A gate
#     that names spellings is guessing. The assertion is now on the INCLUDE: a
#     TU that does not include a DataLayout header cannot ask the question under
#     any spelling, because the declaration is not there. Exactly two TUs may
#     include one — the verifier and the module's dlti-spec builder — and both
#     are named below; every other TU, including all of sema and all of mono and
#     layout_law.hpp itself, is out of reach of a second oracle by construction.
#
# MUTATION PROOFS. Every one below was RUN against this gate and its output is
# quoted; each names the ENGINE and the TYPE, which is what makes the report
# actionable rather than "something disagrees".
#
#   * sema loses its `is_union()` branch (the accumulator SUMS a union):
#       "layout_gate_lattice.C_u_big_pre: size — sema_abi_layout says 32,
#        llvm::DataLayout says 24"  (+6 more rows, incl. the nested NestOU)
#   * sema reads enum payload types UNSUBSTITUTED:
#       "logos.lang.panic.PanicInfo: size — sema_abi_layout says 32,
#        llvm::DataLayout says 48"                     — red on the BASELINE
#   * sema loses its niche branch (`Option<&T>` gets a disc word):
#       "layout_gate_lattice.C_opt_ref_pre: size — sema_abi_layout says 24,
#        llvm::DataLayout says 16"
#   * mono loses its Enum case (back to `default: {8,8}`):
#       "logos.lang.panic.PanicInfo: size — mono_abi_layout says 24,
#        llvm::DataLayout says 48"  (+ ParseIntError/TryFromIntError {8,8} vs {4,4})
#   * sema stops RECORDING (the end-of-run sweep is skipped):
#       "FAIL: sema_abi_layout had only 0 answers checked (floor 40)."
#       — the per-engine floor, which is why there is no total.
#   * a DataLayout becomes reachable from sema (`#include <llvm/IR/DataLayout.h>`
#     in sema_expr.cpp, the file an adversarial reader would put it in):
#       "FAIL: a translation unit that is not a layout ORACLE includes a
#        DataLayout: …/sema_expr.cpp"
#       — caught by the INCLUDE, so no call spelling has to be guessed.
#   * sema's DST prefix offset drifts by 8 bytes: the compile-time verifier is
#     BLIND to it (an unsized struct has no comparable `llvm::DataLayout` size),
#     and the RUN oracle catches it: "FAIL: oracle exited 1". The two nets are
#     complementary and both are required.
#
# Measured by the PREVIOUS round, on the same verifier:
#   * restoring `dl.getTypeSize` at the array-literal element memcpy → red,
#     naming `{i56,i8,i64}`-shaped types, "mlir_abi_size says 16,
#     llvm::DataLayout says 24".
#   * restoring `pb = payload_bytes ? payload_bytes : 1` → red naming
#     `OptionIter$G1$ConvertError`, "layout_of says 4, llvm::DataLayout says 8".
set -euo pipefail

LOGOSC="${1:?logosc path}"
LIB_DIR="${2:?lib dir}"
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# The lattice floor is the GENERATOR'S OWN COUNT, read back from it — not a
# number maintained here. 20 scalar leaves x 6 struct shapes + the 20 `nest`
# inner structs + the COMPOSITION block (unions, tagged/C-like/niche enums,
# zero-sized payload, generic instantiations, nestings), each in a `pre` and a
# `post` position. Adding an axis raises the floor by construction; deleting
# shapes cannot lower it silently, because the count comes from the same loop
# that emits them. MEASURED delta at 202 generated types: 560 — `register_struct`
# files each struct under BOTH its package-qualified key and its bare name, and
# the composition shapes pull in their own instantiations. The floor is
# deliberately the GENERATED count, not the measured registry count, so dropping
# the alias key is not a gate failure while dropping a SHAPE is.
# The fixtures' own footprint, so a stdlib that shrinks cannot silently take the
# baseline to zero and make the delta meaningless.
MIN_BASELINE_TYPES=500
# PER-ENGINE floors on the early-engine ledger. MEASURED on the lattice program
# when these were written: sema_abi_layout 272, mono_abi_layout 2114. The floors
# sit well under the measurement so ordinary stdlib churn does not trip them,
# but an engine that stops recording — or stops being asked — goes to 0 and is
# red (proved: skipping sema's sweep gives "only 0 answers checked"). There is
# deliberately NO total: a total lets one engine hide behind another's number.
MIN_SEMA_CHECKED=40
MIN_MONO_CHECKED=800

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

export LOGOS_LIB_DIR="$LIB_DIR"

# ── read one `layout-verify:` census out of a compile ────────────────────────
# NOT `logosc … | grep`: the whole stream goes to a file first, then it is
# matched. `grep -q` closes the pipe on its first hit and the writer dies of
# SIGPIPE, which `set -o pipefail` reports as a compiler failure — under load
# only, so intermittently.
census() {   # census <src>; sets N_TYPES / N_FIELDS / N_DEFS / N_SEMA / N_MONO
    local src=$1
    if ! LOGOS_VERIFY_LAYOUT=1 "$LOGOSC" "$src" -o "$TMPD/x.o" >"$TMPD/out" 2>"$TMPD/err"; then
        echo "FAIL: logosc failed on $src:"; cat "$TMPD/err"; exit 1
    fi
    local line
    line=$(grep -m1 '^layout-verify:' "$TMPD/err" || true)
    if [ -z "$line" ]; then
        echo "FAIL: no 'layout-verify:' census from $src — the check did NOT run."
        echo "       A gate that could not look must not report that nothing is wrong."
        exit 1
    fi
    N_TYPES=$(sed -E 's/^layout-verify: ([0-9]+) struct types.*/\1/'   <<<"$line")
    N_FIELDS=$(sed -E 's/.*, ([0-9]+) fields,.*/\1/'                   <<<"$line")
    N_DEFS=$(sed -E 's/.*, ([0-9]+) defs.*/\1/'                        <<<"$line")
    N_BAD=$(sed -E 's/.*, ([0-9]+) disagreements$/\1/'                 <<<"$line")
    # Per-engine counts appear as ", <engine> <n>" and are ABSENT when an engine
    # recorded nothing — absent must read as 0, never as "not measured".
    N_SEMA=$(sed -E 's/.*, sema_abi_layout ([0-9]+).*/\1/;t;s/.*/0/'   <<<"$line")
    N_MONO=$(sed -E 's/.*, mono_abi_layout ([0-9]+).*/\1/;t;s/.*/0/'   <<<"$line")
    if [ "$N_BAD" != "0" ]; then
        echo "FAIL: $N_BAD layout disagreements on $src"; cat "$TMPD/err"; exit 1
    fi
}

# ── 0. no TU outside the two allowed can even SEE a DataLayout ───────────────
# `mlir::DataLayout` accumulates a struct's members at their STORE size while
# `llvm::StructLayout` — the layout the object is emitted with — accumulates
# ALLOC sizes. For `{i56,i8,i64}` that is 16 against 24, and no `dlti.dl_spec`
# can reconcile it: the divergence is in the ACCUMULATION RULE, not the leaf
# alignments. `8ba3c764` moved three engines onto one leaf table and stamped the
# spec on the module, and a fourth reader still disagreed.
#
# Asserted by INCLUDE, not by call spelling. A spelling list is a guess — an
# adversarial reader got past the previous one by writing `dl->getTypeSize(t)`
# instead of `dl.getTypeSize(t)`. A TU that does not include the header has no
# declaration to call, under any spelling, through any alias, behind any macro.
SRC_ROOT=$(cd "$HERE/../../src/compiler" && pwd)
ALLOWED_DL_TUS="mlir_gen_types.cpp mlir_gen.cpp"
DL_INCLUDERS=$(grep -rln -E '^[[:space:]]*#include[[:space:]]*[<"](llvm/IR/DataLayout\.h|mlir/Interfaces/DataLayoutInterfaces\.h)[>"]' \
                   "$SRC_ROOT" --include=*.cpp --include=*.hpp --include=*.h | sort || true)
BAD_INC=""
while IFS= read -r f; do
    [ -n "$f" ] || continue
    b=$(basename "$f")
    case " $ALLOWED_DL_TUS " in
        *" $b "*) ;;
        *) BAD_INC="$BAD_INC$f"$'\n' ;;
    esac
done <<<"$DL_INCLUDERS"
if [ -n "$BAD_INC" ]; then
    echo "FAIL: a translation unit that is not a layout ORACLE includes a DataLayout:"
    echo "$BAD_INC"
    echo "       Only these may: $ALLOWED_DL_TUS — the verifier, and the builder"
    echo "       of the module's dlti spec string (not a size query)."
    echo "       Everything else asks layout_law.hpp / mlir_abi_size, which"
    echo "       verify_layout_engines proves equal to llvm::DataLayout."
    exit 1
fi
# And the two that MAY see one must not use the MLIR one as a size oracle: its
# accumulation rule is the wrong one. (Building the spec string from an
# `llvm::DataLayout` is not a size query and is not matched.)
BAD=$(grep -rn -E 'mlir::DataLayout|DataLayout::closest' \
          "$SRC_ROOT" --include=*.cpp --include=*.hpp --include=*.h \
      | grep -v '^\s*//' | grep -vE ':[0-9]+: *//' || true)
if [ -n "$BAD" ]; then
    echo "FAIL: mlir::DataLayout is being used as a size oracle:"
    echo "$BAD"
    exit 1
fi
echo "[layout-gate] DataLayout reachable from exactly: $ALLOWED_DL_TUS"

# The law itself must stay a pure rule: if layout_law.hpp could see a
# DataLayout, the one place that is supposed to BE the answer would have a
# second answer in scope. (Covered by the include check above; asserted
# separately so the diagnostic names the reason.)
if grep -qE 'DataLayout' "$SRC_ROOT/layout_law.hpp"; then
    if grep -vE '^\s*(//|\*)' "$SRC_ROOT/layout_law.hpp" | grep -q 'DataLayout'; then
        echo "FAIL: layout_law.hpp — the law — has a DataLayout in scope."
        exit 1
    fi
fi

# ── 1. the baseline: a program with no structs of its own ────────────────────
cat >"$TMPD/base.logos" <<'EOF'
package layout_gate_base;
fn main() -> i64 { return 0; }
EOF
census "$TMPD/base.logos"
BASE_TYPES=$N_TYPES
echo "[layout-gate] baseline: $BASE_TYPES struct types, $N_FIELDS fields, $N_DEFS defs"
if [ "$BASE_TYPES" -lt "$MIN_BASELINE_TYPES" ]; then
    echo "FAIL: baseline saw only $BASE_TYPES struct types (floor $MIN_BASELINE_TYPES)."
    echo "       Either the stdlib collapsed or the verifier stopped walking the registry."
    exit 1
fi

# ── 2. the lattice ───────────────────────────────────────────────────────────
# Generated here, from the axes, so "which shapes are covered" is a loop and not
# a list somebody maintains. Each type is CONSTRUCTED and read back, so it is
# reachable code and really gets registered.
python3 "$HERE/layout_lattice_gen.py" lattice "$TMPD/lattice.logos" 2>"$TMPD/lat.count"
grep -v '^LATTICE_TYPES=' "$TMPD/lat.count" || true
MIN_LATTICE_DELTA=$(sed -nE 's/^LATTICE_TYPES=([0-9]+)$/\1/p' "$TMPD/lat.count")
if [ -z "$MIN_LATTICE_DELTA" ] || [ "$MIN_LATTICE_DELTA" -lt 150 ]; then
    echo "FAIL: the generator did not report its type count (got '${MIN_LATTICE_DELTA:-}')."
    echo "       The floor IS that count; without it the delta check is vacuous."
    exit 1
fi

census "$TMPD/lattice.logos"
LAT_TYPES=$N_TYPES
DELTA=$(( LAT_TYPES - BASE_TYPES ))
echo "[layout-gate] lattice: $LAT_TYPES struct types ($DELTA more than the baseline), $N_FIELDS fields"
echo "[layout-gate] early engines checked against llvm::DataLayout: sema $N_SEMA, mono $N_MONO"
if [ "$DELTA" -lt "$MIN_LATTICE_DELTA" ]; then
    echo "FAIL: the lattice added only $DELTA struct types to the verifier's view"
    echo "       (floor $MIN_LATTICE_DELTA). Its structs did not reach the check,"
    echo "       so 'no disagreements' is a statement about the stdlib alone."
    exit 1
fi
if [ "$N_SEMA" -lt "$MIN_SEMA_CHECKED" ]; then
    echo "FAIL: sema_abi_layout had only $N_SEMA answers checked (floor $MIN_SEMA_CHECKED)."
    echo "       Sema's answers are BYTE OFFSETS — a custom DST's tail, offset_of!."
    echo "       An engine that records nothing is an engine nobody checked."
    exit 1
fi
if [ "$N_MONO" -lt "$MIN_MONO_CHECKED" ]; then
    echo "FAIL: mono_abi_layout had only $N_MONO answers checked (floor $MIN_MONO_CHECKED)."
    exit 1
fi
LAT_SEMA=$N_SEMA
LAT_MONO=$N_MONO

# ── 3. the RUN oracle: measured tail offsets and measured field offsets ──────
# `size_of` is a CLAIM. Where the tail lands is a FACT: the program writes
# through the fat pointer and scans the allocation for the byte it wrote.
# `offset_of!` is a CLAIM. A pointer difference is a FACT. Every shape asserts
# the claim against the fact and returns a distinct code naming the row.
ARCHIVES=()
while IFS= read -r a; do ARCHIVES+=("$a"); done < <(find "$LIB_DIR" -maxdepth 1 -name '*.a' | sort)
if [ "${#ARCHIVES[@]}" -eq 0 ]; then
    echo "FAIL: no archives in $LIB_DIR — nothing could have been linked."; exit 1
fi

link_and_run() {   # link_and_run <obj> <name>
    if ! cc "$1" -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group \
            -lpthread -lm -lstdc++ -Wl,--gc-sections \
            -Wl,--allow-multiple-definition -o "$TMPD/$2" 2>"$TMPD/link"; then
        echo "FAIL: link of $2:"; cat "$TMPD/link"; exit 1
    fi
    set +e; "$TMPD/$2" >"$TMPD/$2.out" 2>&1; rc=$?; set -e
    if [ "$rc" -ne 0 ]; then
        echo "FAIL: $2 exited $rc"
        cat "$TMPD/$2.out"
        exit 1
    fi
}

python3 "$HERE/layout_lattice_gen.py" oracle "$TMPD/oracle.logos" 2>"$TMPD/or.count"
cat "$TMPD/or.count"
census "$TMPD/oracle.logos"
link_and_run "$TMPD/x.o" oracle
echo "[layout-gate] run oracle: exit 0 — every measured tail offset and every"
echo "              measured field offset matched the compiler's claim"

# ── 4. the authored fixtures compile, run, and exit 0 ────────────────────────
for f in layout_adjacent_narrow_fields layout_zero_size_enum_payload \
         layout_dst_prefix_and_offset_of; do
    src="$HERE/pass/$f.logos"
    [ -f "$src" ] || { echo "FAIL: missing fixture $src"; exit 1; }
    census "$src"
    link_and_run "$TMPD/x.o" "$f"
    echo "[layout-gate] $f: exit 0, $N_TYPES struct types verified"
done

echo "[layout-gate] OK — baseline $BASE_TYPES, lattice +$DELTA (floor $MIN_LATTICE_DELTA),"
echo "              on the lattice: sema $LAT_SEMA / mono $LAT_MONO early-engine"
echo "              answers checked against llvm::DataLayout, 0 disagreements"
