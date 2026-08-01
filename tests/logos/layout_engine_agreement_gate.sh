#!/usr/bin/env bash
# layout_engine_agreement_gate.sh LOGOSC LIB_DIR
#
# THE ENGINES THAT SIZE A VALUE MUST AGREE — ALL OF THEM — AND THE CHECK MUST
# PROVE, IN THE SAME RUN, THAT IT COULD STILL SEE A DISAGREEMENT.
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
# ⚠⚠ WHY THIS GATE IS NOT A LIST OF FLOORS ANY MORE.
#
# The previous form answered "did the check LOOK?" by ENUMERATING the ways it
# could go blind — an engine stops recording, the census line disappears, the
# lattice does not reach the registry — and putting a floor under each. That
# list is written by the same mind that wrote the check, so it is exactly as
# incomplete, and an adversarial reading found three more holes in one sitting:
# the gate stayed green with `defs` at 0 (parsed, never floored), with two of the
# six composition shapes deleted (the floor was the generator's own shrinking
# count, and the MEASURED delta was 560 against a floor of 202), and with the
# oracle generator's three counts printed but never asserted.
#
# EVERY ARM HERE NOW CARRIES A CANARY: a deliberately broken input, pushed
# through the SAME path as the real work, in the SAME invocation, which the gate
# MUST report as a failure. If a canary is not caught the gate reports ITSELF
# broken and exits non-zero naming the canary. Nobody has to think of the
# blinding mode: whatever kills the real comparison kills the canary with it,
# because there is one comparison and both ride it.
#
#   ARM                     CANARY                          WHAT IT RIDES
#   ─────────────────────── ─────────────────────────────── ────────────────────
#   the four-engine          `LOGOS_LAYOUT_CANARY=<engine>`  the recording door
#   comparison (A,B,D,E      moves that engine's answer by   (D,E) or the read
#   vs C)                    ONE BYTE, on the same program   (A,B), the key →
#                                                            DataLayout lookup,
#                                                            the per-engine
#                                                            count, `note()`,
#                                                            `bad.size()`, the
#                                                            census line and
#                                                            THIS FILE'S OWN
#                                                            `N_BAD != 0` test
#   the DataLayout-reach     a planted TU that includes      the same `grep -rln`
#   scan                     `<llvm/IR/DataLayout.h>`, put   invocation and the
#                            under the SAME grep as the      same allowlist
#                            real tree                       classification
#   the RUN oracle           the generator emits the same    the generator, the
#                            program with the first probe's  compile, the LINK,
#                            comparison INVERTED             the run, the `rc`
#                                                            read
#
# WHAT THE CANARIES DO NOT COVER, said plainly:
#   * the engine canary proves the comparison is live for the engine it names.
#     It does NOT prove the LATTICE reached the registry (that is the delta
#     floor's job) nor that any particular SHAPE is covered (the generator's own
#     count, cross-checked).
#   * the DataLayout canary proves the scan can still flag a TU. It does NOT
#     prove that one of the two ALLOWED TUs has not grown a fourth reader
#     internally — an include check cannot see that. The four-engine comparison
#     is the net for that case, and the engine canary is what proves that net
#     live.
#   * the oracle canary proves the probes run and their verdict reaches here. It
#     does NOT prove every probe is right; the probes themselves are facts about
#     memory (a byte written through a fat pointer and scanned for).
#
# FLOORS ARE MEASURED VALUES. Every number below was read off THIS gate on
# 2026-07-31 at `62835ad3` (build clean, L4 3119/3119) and is written with that
# measurement. None is a fraction of it. A drop is a deliberate edit whose ground
# goes in the commit message — halving a floor "for safety" is choosing not to
# notice.
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
#       "FAIL: sema_abi_layout had only 0 answers checked (floor 272)."
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
# Measured by an earlier round, on the same verifier:
#   * restoring `dl.getTypeSize` at the array-literal element memcpy → red,
#     naming `{i56,i8,i64}`-shaped types, "mlir_abi_size says 16,
#     llvm::DataLayout says 24".
#   * restoring `pb = payload_bytes ? payload_bytes : 1` → red naming
#     `OptionIter$G1$ConvertError`, "layout_of says 4, llvm::DataLayout says 8".
set -euo pipefail

LOGOSC="${1:?logosc path}"
LIB_DIR="${2:?lib dir}"
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# ── FLOORS: MEASURED VALUES, WITH THE MEASUREMENT ────────────────────────────
# All read off this gate on 2026-07-31 at `62835ad3`, x86_64-linux, from the
# lines this script prints. Written as `>=` because the stdlib grows; a DROP is
# a real event and must be looked at, not absorbed.
#
#   [layout-gate] baseline: 3676 struct types, 9810 fields, 3676 defs
#   [layout-gate] lattice: 4236 struct types (560 more than the baseline), 11256 fields
#   [layout-gate] early engines … : sema 272, mono 2114
#   [layout-gate] run oracle generated: 34 DST prefix shapes, 29 offset_of shapes, 124 codes
MIN_BASELINE_TYPES=3676
MIN_BASELINE_FIELDS=9810
# `defs` is the A-vs-C arm's own population — the types on which `layout_of` was
# actually asked. It was parsed and never asserted, so the gate was green with
# it at 0: the whole A arm could go silent behind B's number.
MIN_BASELINE_DEFS=3676
# The lattice's contribution, MEASURED. The generator's own count (202) is a
# cross-check below, not the floor: it shrinks when a shape is deleted, so using
# it as the floor is exactly the "half the measured value" hole — two of the six
# composition shapes could be removed and this stayed green at 560 >= 202.
MIN_LATTICE_DELTA=560
MIN_GENERATED_TYPES=202
# PER-ENGINE, on the lattice. There is deliberately NO total: a total lets one
# engine hide behind another's number.
MIN_SEMA_CHECKED=272
MIN_MONO_CHECKED=2114
# The RUN oracle's own population, read back from the generator.
MIN_ORACLE_PREFIXES=34
MIN_ORACLE_OFFSETS=29
MIN_ORACLE_CODES=124
# Exactly these TUs may see a DataLayout — an EQUALITY, checked in both
# directions. The old form only checked the complement, so a `grep` that matched
# NOTHING (a broken root, a typo in the pattern) read as "nobody includes one".
ALLOWED_DL_TUS="mlir_gen_types.cpp mlir_gen.cpp"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

export LOGOS_LIB_DIR="$LIB_DIR"

# ── one compile, one census line, parsed — DECIDING NOTHING ──────────────────
# NOT `logosc … | grep`: the whole stream goes to a file first, then it is
# matched. `grep -q` closes the pipe on its first hit and the writer dies of
# SIGPIPE, which `set -o pipefail` reports as a compiler failure — under load
# only, so intermittently.
#
# This function is deliberately verdict-free. The real path and the canary path
# both call it and both read the SAME `N_BAD`, which is the SAME `bad.size()`
# from the SAME census line: that is what makes "the canary was caught" and "the
# program is clean" two readings of ONE assertion rather than two mechanisms.
census_raw() {   # census_raw <src> [canary-engine]; sets RC / N_TYPES / N_FIELDS
                 # / N_DEFS / N_BAD / N_SEMA / N_MONO / LINE
    local src=$1 canary=${2:-}
    set +e
    # A CAUGHT CANARY ABORTS THE COMPILER — that is what `report_fatal_error`
    # does and it is the correct outcome. The compile runs one level down so the
    # shell's "Aborted (core dumped)" job report goes to that shell's stderr and
    # not into this gate's output, where it would read as a finding. logosc's own
    # stderr still lands in $TMPD/err, which is the only thing read below.
    env LOGOS_VERIFY_LAYOUT=1 ${canary:+LOGOS_LAYOUT_CANARY="$canary"} \
        bash -c '"$0" "$1" -o "$2" >"$3" 2>"$4"' \
        "$LOGOSC" "$src" "$TMPD/x.o" "$TMPD/out" "$TMPD/err" 2>/dev/null
    RC=$?
    set -e
    LINE=$(grep -m1 '^layout-verify:' "$TMPD/err" || true)
    N_TYPES=0; N_FIELDS=0; N_DEFS=0; N_BAD=-1; N_SEMA=0; N_MONO=0
    [ -n "$LINE" ] || return 0
    N_TYPES=$(sed -E 's/^layout-verify: ([0-9]+) struct types.*/\1/'   <<<"$LINE")
    N_FIELDS=$(sed -E 's/.*, ([0-9]+) fields,.*/\1/'                   <<<"$LINE")
    N_DEFS=$(sed -E 's/.*, ([0-9]+) defs.*/\1/'                        <<<"$LINE")
    N_BAD=$(sed -E 's/.*, ([0-9]+) disagreements$/\1/'                 <<<"$LINE")
    # Per-engine counts appear as ", <engine> <n>" and are ABSENT when an engine
    # recorded nothing — absent must read as 0, never as "not measured".
    N_SEMA=$(sed -E 's/.*, sema_abi_layout ([0-9]+).*/\1/;t;s/.*/0/'   <<<"$LINE")
    N_MONO=$(sed -E 's/.*, mono_abi_layout ([0-9]+).*/\1/;t;s/.*/0/'   <<<"$LINE")
}

census() {   # the REAL path: compiles, census present, ZERO disagreements
    census_raw "$1"
    if [ "$RC" -ne 0 ]; then
        echo "FAIL: logosc failed on $1 (exit $RC):"; cat "$TMPD/err"; exit 1
    fi
    if [ -z "$LINE" ]; then
        echo "FAIL: no 'layout-verify:' census from $1 — the check did NOT run."
        echo "       A gate that could not look must not report that nothing is wrong."
        exit 1
    fi
    if [ "$N_BAD" != "0" ]; then
        echo "FAIL: $N_BAD layout disagreements on $1"; cat "$TMPD/err"; exit 1
    fi
}

canary() {   # the SAME path with one engine moved by one byte: the census MUST
             # come back with a nonzero disagreement count NAMING that engine.
    local src=$1 engine=$2
    census_raw "$src" "$engine"
    if [ -z "$LINE" ]; then
        echo "FAIL (CANARY '$engine'): no 'layout-verify:' census at all."
        echo "       The instrument this gate reads is not producing its line, so"
        echo "       every 'no disagreements' above is a statement about nothing."
        sed -n '1,20p' "$TMPD/err"
        exit 1
    fi
    if [ "$N_BAD" -lt 1 ]; then
        echo "FAIL (CANARY '$engine' NOT CAUGHT): the compiler was told to answer"
        echo "       one byte wrong for '$engine' on $src and the verifier still"
        echo "       reported $N_BAD disagreements. The comparison this gate reads"
        echo "       is DEAD for that engine, so its green verdict on the real"
        echo "       program means nothing. THE GATE IS BROKEN, not the tree."
        echo "       census: $LINE"
        exit 1
    fi
    if ! grep -q -- "$engine says" "$TMPD/err"; then
        echo "FAIL (CANARY '$engine'): $N_BAD disagreements reported but none names"
        echo "       '$engine' — the canary was caught by SOMETHING ELSE, which"
        echo "       proves nothing about the engine under test."
        sed -n '1,20p' "$TMPD/err"
        exit 1
    fi
    echo "[layout-gate] canary '$engine': caught — $N_BAD disagreement(s), e.g. $(grep -m1 -- "$engine says" "$TMPD/err" | sed 's/^ *//')"
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
DL_RE='^[[:space:]]*#include[[:space:]]*[<"](llvm/IR/DataLayout\.h|mlir/Interfaces/DataLayoutInterfaces\.h)[>"]'

dl_scan() {   # dl_scan <root>...; prints the files that include a DataLayout and
              # are NOT in the allowlist. ONE implementation, used for the real
              # tree and for the canary, so the canary rides the same regex and
              # the same allowlist match.
    local f b
    # `|| true`: no match is an ANSWER here, not an error. Without it `pipefail`
    # turns "nobody includes one" into a command substitution that fails and a
    # `set -e` exit with no message.
    { grep -rlnE "$DL_RE" "$@" --include=*.cpp --include=*.hpp --include=*.h 2>/dev/null || true; } \
    | sort | while IFS= read -r f; do
        [ -n "$f" ] || continue
        b=$(basename "$f")
        case " $ALLOWED_DL_TUS " in
            *" $b "*) ;;
            *) printf '%s\n' "$f" ;;
        esac
    done
}

BAD_INC=$(dl_scan "$SRC_ROOT")
if [ -n "$BAD_INC" ]; then
    echo "FAIL: a translation unit that is not a layout ORACLE includes a DataLayout:"
    echo "$BAD_INC"
    echo "       Only these may: $ALLOWED_DL_TUS — the verifier, and the builder"
    echo "       of the module's dlti spec string (not a size query)."
    echo "       Everything else asks layout_law.hpp / mlir_abi_size, which"
    echo "       verify_layout_engines proves equal to llvm::DataLayout."
    exit 1
fi
# ⚠ AND THE ALLOWED SET MUST ACTUALLY HAVE BEEN FOUND. "No TU outside the two"
# is also true of a scan that matched nothing at all — a moved SRC_ROOT, a
# renamed header, a typo in the pattern. The two ARE expected to include one, so
# their absence is the scan reporting its own death.
# ⚠ `|| true` ON THE GREP, NOT ON THE PIPELINE. A pattern that matches nothing
# makes `grep` exit 1, and under `set -o pipefail` the command substitution
# fails, `set -e` kills the script — non-zero, but with NOT ONE WORD said. A gate
# that dies mute is only marginally better than one that lies; the empty result
# has to REACH the comparison below so the diagnostic can be printed.
FOUND_ALLOWED=$({ grep -rlE "$DL_RE" "$SRC_ROOT" --include=*.cpp --include=*.hpp --include=*.h || true; } \
                | xargs -r -n1 basename | sort -u | paste -sd' ' -)
EXPECT_ALLOWED=$(printf '%s\n' $ALLOWED_DL_TUS | sort -u | paste -sd' ' -)
if [ "$FOUND_ALLOWED" != "$EXPECT_ALLOWED" ]; then
    echo "FAIL: the DataLayout include scan found '{$FOUND_ALLOWED}' where the two"
    echo "      oracle TUs are '{$EXPECT_ALLOWED}'. Either a TU stopped being an"
    echo "      oracle, or the scan is looking at the wrong tree / for the wrong"
    echo "      header and its 'nobody else includes one' is about nothing."
    exit 1
fi
# ⚠ CANARY. A planted TU that DOES include a DataLayout, scanned by the SAME
# `dl_scan` in the SAME shape, must come back flagged. This is what separates
# "no TU is out of bounds" from "the scan cannot see a TU that is".
mkdir -p "$TMPD/dlcanary"
cat >"$TMPD/dlcanary/fourth_engine_canary.cpp" <<'EOF'
// canary — deliberately out of bounds; the gate's own scan must flag this file.
#include <llvm/IR/DataLayout.h>
uint64_t ask(const llvm::DataLayout* dl, llvm::Type* t) { return dl->getTypeAllocSize(t); }
EOF
CANARY_HIT=$(dl_scan "$SRC_ROOT" "$TMPD/dlcanary")
if ! grep -q 'fourth_engine_canary\.cpp$' <<<"$CANARY_HIT"; then
    echo "FAIL (CANARY 'DataLayout include scan' NOT CAUGHT): a planted TU that"
    echo "      includes <llvm/IR/DataLayout.h> and calls dl->getTypeAllocSize was"
    echo "      NOT reported by the same scan that just said the tree is clean."
    echo "      THE GATE IS BROKEN, not the tree. scan returned: '${CANARY_HIT}'"
    exit 1
fi
if [ "$(grep -c . <<<"$CANARY_HIT")" -ne 1 ]; then
    echo "FAIL (CANARY): the scan flagged more than the planted file:"; echo "$CANARY_HIT"
    exit 1
fi
echo "[layout-gate] DataLayout reachable from exactly: $FOUND_ALLOWED"
echo "[layout-gate] canary 'planted out-of-bounds TU': caught by the same scan"

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

# ── 1. the baseline: a program with no structs of its own ────────────────────
cat >"$TMPD/base.logos" <<'EOF'
package layout_gate_base;
fn main() -> i64 { return 0; }
EOF
census "$TMPD/base.logos"
BASE_TYPES=$N_TYPES; BASE_FIELDS=$N_FIELDS; BASE_DEFS=$N_DEFS
echo "[layout-gate] baseline: $BASE_TYPES struct types, $BASE_FIELDS fields, $BASE_DEFS defs"
floor() {   # floor <what> <got> <want>
    if [ "$2" -lt "$3" ]; then
        echo "FAIL: $1 — observed $2, floor $3 (MEASURED 2026-07-31 at 62835ad3)."
        echo "       A floor here is the value this gate actually saw, not a"
        echo "       fraction of it. If the drop is deliberate, edit the floor and"
        echo "       put its ground in the commit message."
        exit 1
    fi
}
floor "baseline struct types the verifier walked" "$BASE_TYPES"  "$MIN_BASELINE_TYPES"
floor "baseline fields compared (B vs C)"         "$BASE_FIELDS" "$MIN_BASELINE_FIELDS"
floor "baseline defs compared (A vs C)"           "$BASE_DEFS"   "$MIN_BASELINE_DEFS"

# ── 1b. THE FOUR-ENGINE CANARY, on that same baseline program ────────────────
# Each engine in turn is told to answer ONE BYTE wrong. The census must come
# back nonzero and name it. This is the same compile, the same census line, the
# same `N_BAD` field and the same test that judged the run above — inverted.
for eng in layout_of mlir_abi_size sema_abi_layout mono_abi_layout; do
    canary "$TMPD/base.logos" "$eng"
done

# ── 2. the lattice ───────────────────────────────────────────────────────────
# Generated here, from the axes, so "which shapes are covered" is a loop and not
# a list somebody maintains. Each type is CONSTRUCTED and read back, so it is
# reachable code and really gets registered.
python3 "$HERE/layout_lattice_gen.py" lattice "$TMPD/lattice.logos" 2>"$TMPD/lat.count"
grep -v '^LATTICE_TYPES=' "$TMPD/lat.count" || true
GEN_TYPES=$(sed -nE 's/^LATTICE_TYPES=([0-9]+)$/\1/p' "$TMPD/lat.count")
if [ -z "$GEN_TYPES" ]; then
    echo "FAIL: the generator did not report its type count."; exit 1
fi
# TWO checks, because they fail on different things. The generator's count going
# down means a SHAPE was deleted (it is the emitting loop's own tally); the
# verifier's delta going down means the shapes stopped REACHING the registry.
# The old gate compared the delta against the generator's count, so deleting a
# shape lowered both sides and nothing moved.
floor "shapes the lattice generator emits"        "$GEN_TYPES" "$MIN_GENERATED_TYPES"

census "$TMPD/lattice.logos"
LAT_TYPES=$N_TYPES
DELTA=$(( LAT_TYPES - BASE_TYPES ))
echo "[layout-gate] lattice: $LAT_TYPES struct types ($DELTA more than the baseline), $N_FIELDS fields"
echo "[layout-gate] early engines checked against llvm::DataLayout: sema $N_SEMA, mono $N_MONO"
floor "struct types the lattice added to the verifier's view" "$DELTA"   "$MIN_LATTICE_DELTA"
floor "sema_abi_layout answers checked"                       "$N_SEMA"  "$MIN_SEMA_CHECKED"
floor "mono_abi_layout answers checked"                       "$N_MONO"  "$MIN_MONO_CHECKED"
if [ "$DELTA" -lt "$GEN_TYPES" ]; then
    echo "FAIL: the lattice added only $DELTA struct types to the verifier's view"
    echo "       while its generator emitted $GEN_TYPES. Its structs did not reach"
    echo "       the check, so 'no disagreements' is about the stdlib alone."
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

link_run_rc() {   # link_run_rc <obj> <name>; sets RUN_RC. Decides nothing.
    if ! cc "$1" -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group \
            -lpthread -lm -lstdc++ -Wl,--gc-sections \
            -Wl,--allow-multiple-definition -o "$TMPD/$2" 2>"$TMPD/link"; then
        echo "FAIL: link of $2:"; cat "$TMPD/link"; exit 1
    fi
    set +e; "$TMPD/$2" >"$TMPD/$2.out" 2>&1; RUN_RC=$?; set -e
}

link_and_run() {  # the REAL path: must exit 0
    link_run_rc "$1" "$2"
    if [ "$RUN_RC" -ne 0 ]; then
        echo "FAIL: $2 exited $RUN_RC"
        cat "$TMPD/$2.out"
        exit 1
    fi
}

python3 "$HERE/layout_lattice_gen.py" oracle "$TMPD/oracle.logos" 2>"$TMPD/or.count"
grep -v '^ORACLE_' "$TMPD/or.count" || true
OR_PREFIXES=$(sed -nE 's/^ORACLE_PREFIXES=([0-9]+)$/\1/p' "$TMPD/or.count")
OR_OFFSETS=$(sed -nE 's/^ORACLE_OFFSETS=([0-9]+)$/\1/p'   "$TMPD/or.count")
OR_CODES=$(sed -nE 's/^ORACLE_CODES=([0-9]+)$/\1/p'       "$TMPD/or.count")
if [ -z "$OR_PREFIXES" ] || [ -z "$OR_OFFSETS" ] || [ -z "$OR_CODES" ]; then
    echo "FAIL: the oracle generator did not report its population."; exit 1
fi
# These three were PRINTED and never asserted, so the oracle could shrink to one
# probe and the gate would still say "every measured offset matched".
floor "DST prefix shapes the oracle measures"  "$OR_PREFIXES" "$MIN_ORACLE_PREFIXES"
floor "offset_of shapes the oracle measures"   "$OR_OFFSETS"  "$MIN_ORACLE_OFFSETS"
floor "distinct failure codes the oracle can return" "$OR_CODES" "$MIN_ORACLE_CODES"
census "$TMPD/oracle.logos"
link_and_run "$TMPD/x.o" oracle
echo "[layout-gate] run oracle: exit 0 — every measured tail offset and every"
echo "              measured field offset matched the compiler's claim"

# ⚠ CANARY. The same generator emits the same program with the FIRST probe's
# comparison inverted: it returns 1 exactly when the compiler is right. It goes
# through the same `census`, the same link, the same run and the same `RUN_RC`
# read. An oracle whose probes never executed — dead code, an early return, a
# `main` that returns 0 before the block — produces a canary that exits 0, and
# THAT is what this catches. "exit 0" from the real oracle then means "the
# probes ran and agreed", not "the probes were not there".
python3 "$HERE/layout_lattice_gen.py" oracle-canary "$TMPD/oracle_canary.logos" 2>"$TMPD/orc.count"
census "$TMPD/oracle_canary.logos"
link_run_rc "$TMPD/x.o" oracle_canary
if [ "$RUN_RC" -eq 0 ]; then
    echo "FAIL (CANARY 'run oracle' NOT CAUGHT): the oracle was generated with its"
    echo "      first probe INVERTED — it should return 1 on a correct compiler —"
    echo "      and it exited 0. The probe did not run, or its verdict does not"
    echo "      become an exit code, so the real oracle's 'exit 0' is empty."
    echo "      THE GATE IS BROKEN, not the tree."
    cat "$TMPD/oracle_canary.out"
    exit 1
fi
echo "[layout-gate] canary 'run oracle first probe inverted': caught — exit $RUN_RC"

# ── 4. the authored fixtures compile, run, and exit 0 ────────────────────────
NFIX=0
for f in layout_adjacent_narrow_fields layout_zero_size_enum_payload \
         layout_dst_prefix_and_offset_of; do
    src="$HERE/pass/$f.logos"
    [ -f "$src" ] || { echo "FAIL: missing fixture $src"; exit 1; }
    census "$src"
    link_and_run "$TMPD/x.o" "$f"
    NFIX=$((NFIX + 1))
    echo "[layout-gate] $f: exit 0, $N_TYPES struct types verified"
done
floor "authored layout fixtures run" "$NFIX" 3

echo "[layout-gate] OK — baseline $BASE_TYPES/$BASE_FIELDS/$BASE_DEFS, lattice +$DELTA"
echo "              (generator emitted $GEN_TYPES shapes), on the lattice: sema"
echo "              $LAT_SEMA / mono $LAT_MONO early-engine answers checked against"
echo "              llvm::DataLayout, 0 disagreements — and SIX canaries caught:"
echo "              layout_of, mlir_abi_size, sema_abi_layout, mono_abi_layout,"
echo "              the planted DataLayout TU, the inverted run-oracle probe."
