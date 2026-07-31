#!/usr/bin/env bash
# layout_engine_agreement_gate.sh LOGOSC LIB_DIR
#
# THE ENGINES THAT SIZE A VALUE MUST AGREE, AND THE CHECK MUST HAVE LOOKED.
#
# `verify_layout_engines()` runs inside every compile and compares, for every
# registered struct type: the TypeRef engine (`layout_of` — `size_of`, alloca
# sizes, container strides, sema, mono), the MLIR-type engine (`mlir_abi_size` —
# every value-copy memcpy byte count and the DWARF member offsets), and
# `llvm::DataLayout` on the mirrored `llvm::Type`, which is the layout the
# object file is actually emitted with. A disagreement is a hard compile error
# naming the type and both answers.
#
# So "the compiler compiled" already carries the claim. What it does NOT carry
# is that anything was LOOKED AT: a check that silently observes zero types is
# indistinguishable from a check that passes. Two rounds of exactly that shipped
# — `{i32,i64}` sized 12 against ISel's 16, then `{i56,i8,i64}` sized 16 against
# LLVM's 24 — and both were found by running programs, not by a gate.
#
# This gate therefore declares the MINIMUM IT MUST OBSERVE and fails when it
# observes less:
#
#   * a LATTICE program, generated here, whose structs are the product
#     (scalar type × struct shape) — every width including the odd ones, in
#     every position relative to a second sub-64-bit field. Its own struct count
#     is known exactly, and the gate asserts the verifier saw AT LEAST that many
#     MORE types than a baseline program does. That difference is what proves
#     the lattice REACHED the checker rather than the stdlib alone being
#     counted.
#   * the two authored fixtures COMPILE AND RUN, exit 0.
#   * zero disagreements reported, and the report LINE PRESENT — a missing line
#     means the check did not run, which is red, not green.
#   * NO READER ASKS THE RETIRED ENGINE. `verify_layout_engines` compares the
#     engines with each other; it cannot see a reader that asks a FOURTH one,
#     because that reader's answer never reaches an engine comparison. Measured:
#     restoring one `dl.getTypeSize` at the array-literal element memcpy left
#     the verifier reporting 0 disagreements on the whole stdlib and produced 24
#     silent wrong answers. So the absence of `mlir::DataLayout` as a size
#     oracle is asserted on the SOURCE, which is where that class lives.
#
# MUTATION PROOFS (measured when the floors were written):
#   * restoring `dl.getTypeSize` at the array-literal element memcpy → the gate
#     goes red naming `{i56,i8,i64}`-shaped types, "mlir_abi_size says 16,
#     llvm::DataLayout says 24".
#   * restoring `pb = payload_bytes ? payload_bytes : 1` → red naming
#     `OptionIter$G1$ConvertError`, "layout_of says 4, llvm::DataLayout says 8".
#   * deleting the lattice program's structs → the DELTA floor below trips.
set -euo pipefail

LOGOSC="${1:?logosc path}"
LIB_DIR="${2:?lib dir}"
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# The lattice: 20 scalar leaves x 6 struct shapes = 120 outer structs, plus the
# 20 inner structs the `nest` shape needs = 140. The floor is that number:
# every one of them must reach the verifier. MEASURED delta when this was
# written: 280 — `register_struct` files each struct under BOTH its
# package-qualified key and its bare name, so the registry holds two entries per
# struct. The floor is deliberately the STRUCT count, not the measured registry
# count, so dropping the alias key is not a gate failure while dropping a SHAPE
# is.
MIN_LATTICE_DELTA=140
# The fixtures' own footprint, so a stdlib that shrinks cannot silently take the
# baseline to zero and make the delta meaningless.
MIN_BASELINE_TYPES=500

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

export LOGOS_LIB_DIR="$LIB_DIR"

# ── read one `layout-verify:` census out of a compile ────────────────────────
# NOT `logosc … | grep`: the whole stream goes to a file first, then it is
# matched. `grep -q` closes the pipe on its first hit and the writer dies of
# SIGPIPE, which `set -o pipefail` reports as a compiler failure — under load
# only, so intermittently.
census() {   # census <src> <out-var-prefix>; sets N_TYPES / N_FIELDS / N_DEFS
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
    N_FIELDS=$(sed -E 's/.* ([0-9]+) fields,.*/\1/'                    <<<"$line")
    N_DEFS=$(sed -E 's/.* ([0-9]+) defs,.*/\1/'                        <<<"$line")
    N_BAD=$(sed -E 's/.* ([0-9]+) disagreements$/\1/'                  <<<"$line")
    if [ "$N_BAD" != "0" ]; then
        echo "FAIL: $N_BAD layout disagreements on $src"; cat "$TMPD/err"; exit 1
    fi
}

# ── 0. no reader asks `mlir::DataLayout` how big a value is ──────────────────
# `mlir::DataLayout` accumulates a struct's members at their STORE size while
# `llvm::StructLayout` — the layout the object is emitted with — accumulates
# ALLOC sizes. For `{i56,i8,i64}` that is 16 against 24, and no `dlti.dl_spec`
# can reconcile it: the divergence is in the accumulation rule, not the leaf
# alignments. `8ba3c764` moved three engines onto one answer and stamped the
# spec on the module, and this fourth reader still disagreed.
#
# The queries are matched by SHAPE, not by call site, so a new one written
# tomorrow trips this too. `attach_target_data_layout`'s own use of
# `llvm::DataLayout` (building the spec string) is not a size query and is not
# matched.
SRC_ROOT=$(cd "$HERE/../../src/compiler" && pwd)
BAD=$(grep -rn -E 'DataLayout::closest|\.getTypeSize\(|getTypeSizeInBits\(' \
          "$SRC_ROOT" --include=*.cpp --include=*.hpp \
      | grep -v '^\s*//' | grep -vE ':[0-9]+: *//' || true)
if [ -n "$BAD" ]; then
    echo "FAIL: a value's size is being asked of mlir::DataLayout again:"
    echo "$BAD"
    echo "       Use mlir_abi_size / mlir_abi_align / mlir_field_offset"
    echo "       (mlir_gen_impl.hpp) — they are the engine verify_layout_engines"
    echo "       proves equal to llvm::DataLayout. mlir::DataLayout is not."
    exit 1
fi
echo "[layout-gate] no mlir::DataLayout size query in src/compiler"

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
# a list somebody maintains. Each struct is CONSTRUCTED and read back, so it is
# reachable code and really gets registered.
python3 - "$TMPD/lattice.logos" <<'PY'
import sys

# 20 distinct scalar leaves — every integer width the language has, both
# signednesses, the odd widths included, plus the floats, bool and char.
TYPES = ["i8","u8","i16","u16","i24","u24","i32","u32","i56","u56",
         "i64","u64","i128","u128","isize","usize","f32","f64","bool","char"]
SHAPES = ["iso","adj","nnw","tail","nest","narrow"]

def lit(t):
    if t == "bool": return "true"
    if t == "char": return "'a'"
    if t in ("f32","f64"): return f"1.0{t}"
    return f"1{t}"

out = ["package layout_gate_lattice;"]
mains = []
n_structs = 0
for t in TYPES:
    tag = t
    for sh in SHAPES:
        sn = f"S_{tag}_{sh}"
        if sh == "iso":
            fs = [("p", t), ("id", "i64")]
        elif sh == "adj":
            fs = [("p", t), ("n", "u8"), ("id", "i64")]
        elif sh == "nnw":
            fs = [("n", "u8"), ("p", t), ("m", "u24"), ("id", "i64")]
        elif sh == "tail":
            fs = [("id", "i64"), ("p", t), ("n", "u8")]
        elif sh == "narrow":
            fs = [("p", t), ("n", "u8"), ("m", "u24")]
        else:  # nest
            out.append(f"struct In_{tag} {{ pub p: {t}, pub n: u8 }}")
            n_structs += 1
            fs = None
        if fs is None:
            out.append(f"struct {sn} {{ pub inner: In_{tag}, pub id: i64 }}")
            init = f"{sn} {{ inner: In_{tag} {{ p: {lit(t)}, n: 1u8 }}, id: 7i64 }}"
        else:
            out.append("struct " + sn + " { "
                       + ", ".join(f"pub {n}: {ty}" for n, ty in fs) + " }")
            def v(n, ty):
                if n == "p":  return lit(t)
                if n == "id": return "7i64"
                if n == "n":  return "1u8"
                return "2u24"
            init = sn + " { " + ", ".join(f"{n}: {v(n, ty)}" for n, ty in fs) + " }"
        n_structs += 1
        # `narrow` has no `id`; read `n`, which every shape has.
        probe = "n" if sh == "narrow" else "id"
        acc = ".inner.n" if (sh == "nest" and probe == "n") else "." + probe
        mains.append(f"    {{ let a: [{sn}; 2] = [{init}, {init}]; "
                     f"if a[1i64]{acc} != a[0i64]{acc} {{ return 1; }} }}")

out.append("")
out.append("fn main() -> i64 {")
out.extend(mains)
out.append("    return 0;")
out.append("}")
open(sys.argv[1], "w").write("\n".join(out) + "\n")
sys.stderr.write(f"lattice structs: {n_structs}\n")
PY

census "$TMPD/lattice.logos"
LAT_TYPES=$N_TYPES
DELTA=$(( LAT_TYPES - BASE_TYPES ))
echo "[layout-gate] lattice: $LAT_TYPES struct types ($DELTA more than the baseline), $N_FIELDS fields"
if [ "$DELTA" -lt "$MIN_LATTICE_DELTA" ]; then
    echo "FAIL: the lattice added only $DELTA struct types to the verifier's view"
    echo "       (floor $MIN_LATTICE_DELTA). Its structs did not reach the check,"
    echo "       so 'no disagreements' is a statement about the stdlib alone."
    exit 1
fi

# ── 3. the authored fixtures compile, run, and exit 0 ────────────────────────
ARCHIVES=()
while IFS= read -r a; do ARCHIVES+=("$a"); done < <(find "$LIB_DIR" -maxdepth 1 -name '*.a' | sort)
if [ "${#ARCHIVES[@]}" -eq 0 ]; then
    echo "FAIL: no archives in $LIB_DIR — nothing could have been linked."; exit 1
fi

for f in layout_adjacent_narrow_fields layout_zero_size_enum_payload; do
    src="$HERE/pass/$f.logos"
    [ -f "$src" ] || { echo "FAIL: missing fixture $src"; exit 1; }
    census "$src"
    if ! cc "$TMPD/x.o" -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group \
            -lpthread -lm -lstdc++ -Wl,--gc-sections \
            -Wl,--allow-multiple-definition -o "$TMPD/$f" 2>"$TMPD/link"; then
        echo "FAIL: link of $f:"; cat "$TMPD/link"; exit 1
    fi
    set +e; "$TMPD/$f"; rc=$?; set -e
    if [ "$rc" -ne 0 ]; then
        echo "FAIL: $f exited $rc (each return code names the assertion in the source)"
        exit 1
    fi
    echo "[layout-gate] $f: exit 0, $N_TYPES struct types verified"
done

echo "[layout-gate] OK — baseline $BASE_TYPES, lattice +$DELTA (floor $MIN_LATTICE_DELTA), 0 disagreements"
