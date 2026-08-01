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
#   the ENUM rows of the      `LOGOS_LAYOUT_CANARY=mono_…`    the enum arm of
#   comparison (new)          on the lattice MUST report a    `truth`, the enum
#                             row naming an `EB_*`/`GB_*`     ledger keys, the
#                             C-like enum BY NAME             per-shape bounded
#                                                             report
#   the rule-selector scan    a planted TU that names         the same grep, the
#   (new)                     `lay::Uni` and chooses between  same allowlist
#                             `niche_enum`/`tagged_enum`
#   the DECLINE count (new)   `LOGOS_LAYOUT_CANARY=declined`  `declines()`, the
#                             pushes ONE synthetic decline    `n_declined` count,
#                             through `record_declined`       the `bad` rows, the
#                                                             census + JSON field,
#                                                             this file's
#                                                             `N_DECLINED != 0`
#
# ⚠⚠ AND WHY `declined` EXISTS AT ALL — THE HOLE A DISAGREEMENT CANNOT SHOW.
#
# Every arm above compares TWO ANSWERS. It is silent, by construction, about a
# type an engine never answered for: no row, no cell, no disagreement. That is
# not hypothetical. `layout_of`'s Struct case met a missing definition with
# `Layout r{8, 8}` and returned WITHOUT calling `struct_def_layout`, so nothing
# entered the ledger — and mono did not make the instance because a type
# mentioned only in `sizeof::<T>()` was not treated as an instantiation demand.
# MEASURED: `sizeof::<W<i128>>()` = 8 for a 32-byte struct, `offset_of!(W<i128>,
# n)` = 16 in the SAME compile, `malloc(sizeof(..))` short by 24 bytes — and
# THIS GATE, on that program, reported 0 disagreements. Truthfully. It had never
# been offered the type.
#
# So a decline is now a recorded fact with its own field, floored at exactly 0,
# and the canary above proves the field can move. The engine asked at CODEGEN
# additionally dies at the decline site, since no later phase can supply what it
# was missing.
#
# ⚠⚠ AND WHY THERE IS NOW A MATRIX. The previous form floored a per-engine
# TOTAL. A total says the engine was checked; it does not say WHICH BRANCHES
# were, and a missing branch is exactly a branch nothing exercised. mono shipped
# with NO C-LIKE ENUM BRANCH behind a green total of 2114, because the only
# C-like enum in the corpus was `enum EC { X, Y, Z }` — the DEFAULT i32 backing,
# the one width at which having the branch and not having it give the same four
# bytes. One value of an axis is not an axis.
#
# So the law now NAMES the branch it took (`layout::Shape`), that name rides
# every ledger entry, and the compiler prints an ENGINE × SHAPE matrix:
#
#   layout-matrix: mono_abi_layout product=… union=… transparent=… c-like=…
#                                  tagged=… niche=…
#
# Every one of the 18 cells is floored SEPARATELY at a measured value. A cell at
# ZERO is an engine that never took that branch on the whole lattice — which is
# either a missing branch (the bug) or a shape the corpus stopped having (the
# same blindness one level up). Both are red, and both are named.
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
# MUTATION PROOFS RUN 2026-08-01, on THIS form of the gate:
#
#   * mono loses its C-LIKE BACKING branch (`enum B : u64` sized {4,4}) — THE
#     DEFECT THIS ROUND FIXED. 80 disagreements, named by engine AND shape:
#       "[c-like] layout_gate_lattice.EB_i16: size — mono_abi_layout says 4,
#        llvm::DataLayout says 2"
#       "[product] layout_gate_lattice.C_eb_i64_post: size — layout_of says 24,
#        mono_abi_layout says 16"
#     ⚠ Against the PREVIOUS corpus this mutation was GREEN: `EC`'s backing type
#     is the default i32, so mono's missing branch and mono's correct branch
#     agreed. The `BACKINGS` axis is what makes this red.
#   * `clone_enum_def` drops BACKING_TYPE on instances (`GB_u64<i32>` weighs 4):
#       "[c-like] layout_gate_lattice.GB_i16__i32: size — mono_abi_layout says 2,
#        llvm::DataLayout says 4"  — 48 rows.
#   * `clone_struct_def` drops REPR_TRANSPARENT on instances:
#       "[product] logos.lang.cell.UnsafeCell$G1$i32: SHAPE — layout_of applied
#        'product', mono_abi_layout applied 'transparent'"
#     ⚠ CAUGHT BY THE SHAPE COLUMN ALONE. Every byte count agrees — a one-member
#     product and a transparent wrapper weigh the same — so a size comparison is
#     blind to it, and the `layout_of × transparent` cell drops to 0. This is the
#     row that shows why the matrix reports the BRANCH and not just the number.
#   * `mono_dst_prefix_field`'s projection offset moves by ONE BYTE, and nothing
#     else: the compile-time verifier stays FULLY GREEN — all 18 cells at their
#     floors, 0 disagreements — and the RUN ORACLE catches it, "FAIL: oracle
#     exited 225" (the u8-backed DstRef row: the same field written through the
#     sized and through the `dyn` instantiation landed on different bytes). That
#     offset is not any registry's struct size, so the verifier CANNOT see it.
#     The two nets are complementary and both are required.
#
# Measured by an earlier round, on the same verifier:
#   * restoring `dl.getTypeSize` at the array-literal element memcpy → red,
#     naming `{i56,i8,i64}`-shaped types, "mlir_abi_size says 16,
#     llvm::DataLayout says 24".
#   * restoring `pb = payload_bytes ? payload_bytes : 1` → red naming
#     `OptionIter$G1$ConvertError`, "layout_of says 4, llvm::DataLayout says 8".
#
# ⚠⚠⚠ AND WHY NOTHING HERE READS PROSE ANY MORE (2026-08-01).
#
# The canaries answered "is the instrument DEAD?". They cannot answer "is the
# instrument ALIVE AND READING THE WRONG THING?", and two arms of this gate were
# doing exactly that:
#
#   * THE FLOORS WERE PARSED WITH `sed`, WHICH ON A NON-MATCH RETURNS ITS INPUT.
#     `N_DEFS=$(sed -E 's/.*, ([0-9]+) defs.*/\1/' <<<"$LINE")` gives the WHOLE
#     CENSUS LINE when the wording moves by one token. `[ "<a sentence>" -lt
#     3676 ]` then writes "integer expression expected" and exits 2 — and `if`
#     reads a non-zero status as FALSE, so the floor NEVER FIRES. MEASURED: a
#     wrapper rewriting only `, N defs,` to `, defs=N,` on logosc's stderr left
#     this gate at EXIT 0, printing its OK line with the census line embedded in
#     it where the number should be. Rewriting all three floored fields the same
#     way passed all three floors and then died at an arithmetic expansion
#     saying "layout: unbound variable", naming nothing. The canaries could not
#     cover it: they read a differently-anchored field that still matched.
#     ⚠ THE SAME FUNCTION already used the safe `;t;s/.*/0/` idiom for
#     `N_SEMA`/`N_MONO` and the safe `sed -n …p` for the generator counts. The
#     idiom was known and applied to three of six parses. That is the argument
#     against idiom discipline as a fix.
#
#   * THE DataLayout ARM ASSERTED A PROPERTY OF #include LINES AND STATED IT AS A
#     PROPERTY OF THE PROGRAM: "a TU that does not include the header has no
#     declaration to call, under any spelling, through any alias, behind any
#     macro". FALSE — `llvm/IR/Module.h` includes `llvm/IR/DataLayout.h`, and the
#     compiler's own dependency record says NINE logosc TUs hold a live
#     `llvm::DataLayout`, not two. A text scan of a source file cannot see what
#     the preprocessor does.
#
# So: THE SUBJECT EMITS A STRUCTURED VERDICT AND A STRICT PARSER READS IT.
# `layout-verify-json:` / `lattice-gen-json:` / `oracle-gen-json:` are JSON
# objects with named, typed fields; `tests/logos/verdict.py` reads them, and a
# missing field, a RENAMED field, a non-integer value or a malformed document is
# a FATAL ERROR (exit 3) that names what it could not parse — never an empty
# string that floors to true. And `tests/logos/dl_reach.py` puts the DataLayout
# question to the TOOLCHAIN: `ninja -t deps` for who can NAME one (the
# preprocessed TU, not the include lines) and `nm -uC` for who ASKS one (the
# undefined references the linker will resolve — which also sees a new reader
# appearing INSIDE an allowed TU, the include check's own stated blind spot).
#
# ⚠ AND THE PARSER'S OWN MEDIUM IS CHECKED THE WAY THE CANARIES ARE. Every run
# starts with `verdict.py --selftest`, which pushes its whole table of
# malformed and false inputs — the count is the selftest's own to print, not a
# number restated here where nothing pins it —
# through the SAME read_verdict/resolve_int/assertions this gate then uses and
# requires each to be rejected — plus the well-formed one accepted, so a parser
# that rejects everything proves nothing. It exits 4 if it accepts one.
set -euo pipefail

LOGOSC="${1:?logosc path}"
LIB_DIR="${2:?lib dir}"
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# ── FLOORS: MEASURED VALUES, WITH THE MEASUREMENT ────────────────────────────
# All read off this gate on 2026-08-01 at `af6cdd93`+fix, x86_64-linux, from the
# lines this script prints. Written as `>=` because the stdlib grows; a DROP is
# a real event and must be looked at, not absorbed.
#
#   [layout-gate] baseline: 3676 struct types, 9810 fields, 3676 defs
#   [layout-gate] lattice: 4336 struct types (660 more than the baseline), 11506 fields
#   [layout-gate] early engines … : sema 343, mono 2235
#   [layout-gate] run oracle generated: 59 DST prefix shapes, 54 offset_of shapes,
#                 12 DstRef-projection widths, 272 diagnostic codes
#   oracle-run-json: {"probes":272,"failures":0,"first":0}
#   [layout-gate] all 18 cells at or above their measured floors, 91 enum types
MIN_BASELINE_TYPES=3676
MIN_BASELINE_FIELDS=9810
# `defs` is the A-vs-C arm's own population — the types on which `layout_of` was
# actually asked. It was parsed and never asserted, so the gate was green with
# it at 0: the whole A arm could go silent behind B's number.
MIN_BASELINE_DEFS=3676
# ⚠ AND THE ARM THOSE THREE WERE MEASURED ON IMPORTS NOTHING (see §1). The
# STDLIB baseline — every `package` declared under `stdlib/`, DERIVED from the
# tree at run time — is the population that actually has types in it. MEASURED
# 2026-08-01 by this gate:
#   [layout-gate] stdlib baseline over 188 DERIVED packages: 6314 struct types,
#                 17906 fields, 6300 defs
# The empty program's 3676/9810/3676 held on the same build on which four
# disagreements were sitting one `use logos.lang.writ.container;` away.
MIN_STDLIB_TYPES=6314
MIN_STDLIB_FIELDS=17906
MIN_STDLIB_DEFS=6300
# The DERIVATION's own floor: how many `package` declarations the stdlib tree
# yields. MEASURED 2026-08-01: 188. An empty or truncated list gives back the
# empty program, whose clean report reads identically.
MIN_STDLIB_PKGS=188
# The lattice's contribution, MEASURED. The generator's own count (202) is a
# cross-check below, not the floor: it shrinks when a shape is deleted, so using
# it as the floor is exactly the "half the measured value" hole — two of the six
# composition shapes could be removed and this stayed green at 560 >= 202.
MIN_LATTICE_DELTA=660
MIN_GENERATED_TYPES=252
# PER-ENGINE, on the lattice. There is deliberately NO total: a total lets one
# engine hide behind another's number.
MIN_SEMA_CHECKED=343
MIN_MONO_CHECKED=2235
# The RUN oracle's own population, read back from the generator.
MIN_ORACLE_PREFIXES=59
MIN_ORACLE_OFFSETS=54
MIN_ORACLE_CODES=272
# Backing widths whose DstRef PROJECTION the oracle measures by writing the same
# field through the sized and the `dyn` instantiation and scanning for it. This
# is the ONLY net for `mono_dst_prefix_field`: that offset is not any registry's
# struct size, so the compile-time verifier cannot see it.
MIN_ORACLE_DSTREF=12
# ── THE ENGINE × SHAPE MATRIX ────────────────────────────────────────────────
# Read off `layout-matrix:` on the lattice, 2026-08-01, x86_64-linux:
#   layout_of       product=2162 union=3 transparent=2 c-like=31 tagged=55 niche=5
#   mono_abi_layout product=2163 union=3 transparent=2 c-like=27 tagged=36 niche=4
#   sema_abi_layout product=321  union=3 transparent=2 c-like=15 tagged=1  niche=1
# ⚠ sema's TAGGED and NICHE cells are small because sema names a generic enum
# BEFORE mono renames it (`Option$G1$i64` vs `Option__i64`), so only NON-GENERIC
# enums of sema's share a key with the other two. That is why the lattice now
# carries `E2` and `ORef` — an authored tagged enum and an authored niche enum
# with no type parameters. Their cells are 1 each and a 1 is a floor.
MIN_LO_PRODUCT=2162 ; MIN_LO_UNION=3 ; MIN_LO_TRANSPARENT=2
MIN_LO_CLIKE=31     ; MIN_LO_TAGGED=55 ; MIN_LO_NICHE=5
MIN_MO_PRODUCT=2163 ; MIN_MO_UNION=3 ; MIN_MO_TRANSPARENT=2
MIN_MO_CLIKE=27     ; MIN_MO_TAGGED=36 ; MIN_MO_NICHE=4
MIN_SE_PRODUCT=321  ; MIN_SE_UNION=3 ; MIN_SE_TRANSPARENT=2
MIN_SE_CLIKE=15     ; MIN_SE_TAGGED=1  ; MIN_SE_NICHE=1
MIN_ENUM_TYPES=91
# The verdict's exact field set. A field this gate does not floor is still a
# field whose disappearance means the verdict's shape moved, and a gate reading
# a shape it was not written against is guessing.
VERDICT_KEYS='struct_types,fields,defs,enum_types,unmatched,declined,disagreements,engines,matrix'
# ── THE CANARY TALLY IS DERIVED, AND ITS FLOOR IS MEASURED ───────────────────
# ⚠ This gate's closing line said "NINE canaries caught" and listed nine names.
# MEASURED 2026-08-01: TWELVE fire. `declined` — added the day before, in the
# commit that made an engine's silence a failure — is caught on every run and
# was in no list. That is the SIXTH recorded kind of lying gate, a measured
# claim that nothing pins, inside the artifact written to stop gates from lying;
# and unlike a stale number in a commit message it is re-read by every reader of
# this file as a statement about the present.
#
# So the count and the names are an accumulator. `caught` is the only way to
# announce one, the summary prints what it accumulated, and the floor below is
# the ONE hand-maintained number — which cannot drift silently, because a canary
# that stops firing already fails the gate at its own site, and one that is
# DELETED drops the tally under this floor.
# MEASURED 2026-08-01: dl-reach 4, engines 4, rule-selector 1, declined 1, enum
# rows 1, inverted run-oracle probe 1 = 12.
MIN_DLREACH_CANARIES=4
MIN_CANARIES=12
N_CANARIES=0
CANARY_NAMES=()
caught() {   # caught <name> <detail…> — THE ONLY WAY TO ANNOUNCE A CAUGHT CANARY
    N_CANARIES=$((N_CANARIES + 1))
    CANARY_NAMES+=("$1")
    echo "[layout-gate] canary '$1': caught — ${*:2}"
}
caught_n() { # caught_n <label> <n> — a sub-tool that counted its own, already announced
    N_CANARIES=$((N_CANARIES + $2))
    CANARY_NAMES+=("$1×$2")
}

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

export LOGOS_LIB_DIR="$LIB_DIR"

# ── THE STRICT PARSER, AND ITS OWN MEDIUM CHECKED FIRST ──────────────────────
# Nothing below this line reads prose. `verdict.py` is the only reader, and it
# proves itself before it is trusted: every malformed and false input in its
# table goes through the same
# functions this run uses, each of which must be REJECTED, plus one well-formed
# input that must be ACCEPTED. Exit 4 means the parser is broken and no verdict
# it would have pronounced means anything.
VERDICT="$HERE/verdict.py"
[ -f "$VERDICT" ] || { echo "FAIL: $VERDICT is missing — this gate has no reader."; exit 1; }
if ! python3 "$VERDICT" --selftest; then
    echo "FAIL: the verdict parser did not pass its own selftest. Every number"
    echo "      this gate is about to read would be read by it. Stop here."
    exit 1
fi

# `read_verdict <file> <prefix> <label> <exact-keys> <bind…>` — resolves every
# binding or DIES. On success it writes $TMPD/bind.sh with one NAME=value line
# per binding and this shell sources it under `set -u`, so a field that moved is
# a missing shell variable at the point of use and not an empty string that
# floors to true. Floors and equalities are passed as `--floor path:N`.
read_verdict() {   # read_verdict <file> <prefix> <label> [verdict.py args…]
    local file=$1 prefix=$2 label=$3 rc=0; shift 3
    rm -f "$TMPD/bind.sh"
    set +e
    python3 "$VERDICT" --file "$file" --prefix "$prefix" --label "$label" \
            --export "$TMPD/bind.sh" --quiet "$@"
    rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then
        if [ "$rc" -eq 3 ]; then
            echo "       ⚠ EXIT 3 IS THE GATE REPORTING THAT IT COULD NOT LOOK."
            echo "       Nothing above this line is evidence about the tree."
        fi
        exit 1
    fi
    # shellcheck disable=SC1090
    . "$TMPD/bind.sh"
}

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
census_raw() {   # census_raw <src> [canary-engine]; sets RC and, when the verdict
                 # is present, N_TYPES / N_FIELDS / N_DEFS / N_BAD / N_SEMA /
                 # N_MONO / N_ENUMS through the strict parser.
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
    # ⚠ NO `sed` ON THE PROSE LINE. The compiler emits the SAME census as a JSON
    # object; a field that was renamed is a MISSING field here and verdict.py
    # exits 3 naming it, where the old `sed` returned the whole line and the
    # floor silently passed. `engines.*` are ABSENT when an engine recorded
    # nothing — absent must read as 0 and never as "not measured" — so those two
    # are defaulted by `have_engine` below rather than bound blindly.
    N_TYPES=0; N_FIELDS=0; N_DEFS=0; N_BAD=-1; N_SEMA=0; N_MONO=0; N_ENUMS=0
    N_DECLINED=-1
    HAVE_VERDICT=0
    grep -q '^layout-verify-json:' "$TMPD/err" || return 0
    HAVE_VERDICT=1
    # `engines.<name>` is ABSENT when that engine recorded nothing, and absent
    # must read as 0 — `--bind-opt` is the ONE documented absence. A missing
    # `engines` OBJECT is still fatal, which is the difference between "this
    # engine said nothing" and "the verdict's shape moved".
    read_verdict "$TMPD/err" 'layout-verify-json:' "the layout verifier on $src" \
        --exact-keys "$VERDICT_KEYS" \
        --bind N_TYPES=struct_types --bind N_FIELDS=fields \
        --bind N_DEFS=defs --bind N_ENUMS=enum_types \
        --bind N_BAD=disagreements --bind N_DECLINED=declined \
        --bind-opt N_SEMA=engines.sema_abi_layout:0 \
        --bind-opt N_MONO=engines.mono_abi_layout:0
}

census() {   # the REAL path: compiles, verdict present, ZERO disagreements
    census_raw "$1"
    if [ "$RC" -ne 0 ]; then
        echo "FAIL: logosc failed on $1 (exit $RC):"; cat "$TMPD/err"; exit 1
    fi
    if [ "$HAVE_VERDICT" -eq 0 ]; then
        echo "FAIL: no 'layout-verify-json:' verdict from $1 — the check did NOT run."
        echo "       A gate that could not look must not report that nothing is wrong."
        exit 1
    fi
    if [ "$N_BAD" -ne 0 ]; then
        echo "FAIL: $N_BAD layout disagreements on $1"; cat "$TMPD/err"; exit 1
    fi
    # ── AND THE ANSWERS NOBODY GAVE ─────────────────────────────────────────
    # `disagreements` is about types that were SIZED TWICE and differ. It is
    # silent about a type an engine DECLINED to size — which is how
    # `sizeof::<W<i128>>() == 8` lived behind this gate reporting 0: mono never
    # made the instance, `layout_of` found no definition, took `{8,8}` without
    # calling `struct_def_layout`, and so recorded NOTHING. No row, no cell, no
    # disagreement. A declined computation is now a recorded fact and its floor
    # is EXACTLY ZERO, read as its own field so it cannot be confused with a
    # byte-count mismatch.
    if [ "$N_DECLINED" -ne 0 ]; then
        echo "FAIL: $N_DECLINED layout computation(s) DECLINED on $1 — an engine"
        echo "       could not size a type and would have answered {8,8}. That is"
        echo "       not a disagreement, it is a type the comparison never saw."
        cat "$TMPD/err"; exit 1
    fi
}

canary() {   # the SAME path with one engine moved by one byte: the census MUST
             # come back with a nonzero disagreement count NAMING that engine.
    local src=$1 engine=$2
    census_raw "$src" "$engine"
    if [ "$HAVE_VERDICT" -eq 0 ]; then
        echo "FAIL (CANARY '$engine'): no 'layout-verify-json:' verdict at all."
        echo "       The instrument this gate reads is not producing its verdict, so"
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
        grep -m1 '^layout-verify-json:' "$TMPD/err" || true
        exit 1
    fi
    if ! grep -q -- "$engine says" "$TMPD/err"; then
        echo "FAIL (CANARY '$engine'): $N_BAD disagreements reported but none names"
        echo "       '$engine' — the canary was caught by SOMETHING ELSE, which"
        echo "       proves nothing about the engine under test."
        sed -n '1,20p' "$TMPD/err"
        exit 1
    fi
    caught "$engine" "$N_BAD disagreement(s), e.g. $(grep -m1 -- "$engine says" "$TMPD/err" | sed 's/^ *//')"
}

# ── 0. WHO CAN REACH AN llvm::DataLayout — ASKED OF THE BUILD ────────────────
# `mlir::DataLayout` accumulates a struct's members at their STORE size while
# `llvm::StructLayout` — the layout the object is emitted with — accumulates
# ALLOC sizes. For `{i56,i8,i64}` that is 16 against 24, and no `dlti.dl_spec`
# can reconcile it: the divergence is in the ACCUMULATION RULE, not the leaf
# alignments. `8ba3c764` moved three engines onto one leaf table and stamped the
# spec on the module, and a fourth reader still disagreed. So exactly one place
# may ask `llvm::DataLayout` what a type weighs: the verifier that IS the
# independent answer.
#
# ⚠ THIS WAS A TEXT SCAN AND THE TEXT SCAN WAS WRONG. It grepped `#include`
# lines over src/compiler and said "two TUs", on the stated ground that "a TU
# that does not include the header has no declaration to call, under any
# spelling, through any alias, behind any macro". `llvm/IR/Module.h` includes
# `llvm/IR/DataLayout.h`. NINE logosc TUs hold a live one — including
# `compile_pipeline.cpp`, which the scan called clean while its object carries
# three undefined `llvm::DataLayout` references. A text scan of a source file
# cannot see what the preprocessor does; the question has to be put to the
# toolchain, and it has an answer there.
#
#   Q1  WHO CAN NAME ONE — `ninja -t deps`, the compiler's own -MD record: the
#       preprocessed TU, not the first ten lines of the source.
#   Q2  WHO ASKS ONE — `nm -uC` on the built objects: the undefined references
#       the linker will resolve. This is the question the include check meant to
#       ask, and it ALSO sees a new reader appearing INSIDE an allowed TU, which
#       the include check named as its own blind spot and could not cover.
#
# Both answers are compared, in BOTH directions, against a recorded one with its
# ground per TU (`datalayout_reach.expected.json`) — so a shrink (the scan went
# blind) is as red as a growth. Two canaries ride the same nm scan: a fifth
# engine COMPILED HERE from source with this build's own flags, which reaches
# `llvm::DataLayout` through `<llvm/IR/Module.h>` and never names the header the
# old scan looked for; and a planted copy of a real oracle object. What Q2
# cannot see — a size query on a scalar type only, which is fully header-inline
# and leaves no symbol — is written down in that file, and is covered by the
# four-engine comparison the engine canaries prove live.
SRC_ROOT=$(cd "$HERE/../../src/compiler" && pwd)
BUILD_ROOT=$(cd "$LIB_DIR/../.." && pwd)
mkdir -p "$TMPD/dlreach"
set +e
python3 "$HERE/dl_reach.py" --build "$BUILD_ROOT" --src "$SRC_ROOT" \
        --expected "$HERE/datalayout_reach.expected.json" \
        --tmpd "$TMPD/dlreach" >"$TMPD/dlreach.json" 2>"$TMPD/dlreach.err"
DL_RC=$?
set -e
grep -v '^dl-reach-json:' "$TMPD/dlreach.err" >&2 || true
case "$DL_RC" in
  0) ;;
  3) echo "       ⚠ EXIT 3: THE BUILD COULD NOT BE ASKED. Not a clean tree — an"
     echo "       unanswered question. Nothing above is evidence."; exit 1 ;;
  4) echo "       ⚠ EXIT 4: a canary was not caught. THE SCAN IS BROKEN, not the"
     echo "       tree."; exit 1 ;;
  *) exit 1 ;;
esac
# …and its canaries join THIS gate's tally as a number, not as a clause.
read_verdict "$TMPD/dlreach.err" 'dl-reach-json:' "the DataLayout-reach scan" \
    --exact-keys canaries --floor canaries "$MIN_DLREACH_CANARIES" \
    --bind DL_CANARIES=canaries
caught_n "dl-reach" "$DL_CANARIES"

# ⚠ THE TWO TEXT SCANS THAT USED TO STAND HERE ARE GONE, and what replaced them
# is in dl_reach.py above:
#   * `grep -qE DataLayout layout_law.hpp` asked about ONE FILE'S TEXT when the
#     property is "the law has no second answer IN SCOPE" — its whole
#     preprocessed closure. dl_reach.py compiles a TU whose entire content is
#     `#include "layout_law.hpp"`, reads the compiler's own -MD list (338
#     headers) and asserts neither DataLayout header is in it. Its canary is the
#     same probe with the header added, which must come back flagged.
#   * `grep -rn 'mlir::DataLayout'` guessed at a spelling. MEASURED: an
#     `mlir::DataLayout` size query leaves `mlir::DataLayout::getTypeSize` /
#     `getTypeABIAlignment` as UNDEFINED SYMBOLS, so the linker answers this one
#     too — the Q2 symbol map covers both families, and the compiled canary asks
#     both and must be flagged for both.

# ── 0b. NO ENGINE DECIDES ITS OWN AGGREGATE SHAPE ────────────────────────────
# The previous round put the aggregate RULE in one header and left each engine
# to decide WHICH RULE APPLIED — a three-way if/else on `repr_transparent` /
# `is_union` written out once per engine. A law with five askers can still be
# asked incompletely: mono's enum decision read as a complete thought and was
# missing the C-like branch, and nothing in mono pointed at the question it
# never asked.
#
# So the discriminator is now obtained ONLY from `layout::agg_shape(transparent,
# union, n)` — three positional, undefaulted arguments — and `aggregate_layout`
# switches on it. This arm asserts that on the SOURCE: in the engine TUs, every
# line that reads one of those two properties must be the `agg_shape` call
# itself. A hand-rolled branch is then a gate failure, not a latent asymmetry.
# Asserted by NAME, not by call spelling of the property reads: an engine reads
# `is_union()` for plenty of non-layout reasons (a union field write needs
# `unsafe`, a union literal takes one initialiser, the emitted LLVM type is the
# field list in sequence). What it may NOT do is SELECT A RULE. The three
# selectors are `Uni` (the union rule), `niche_enum(` and `tagged_enum(`; if no
# TU outside the law can name them, no TU outside the law can choose between
# them, under any spelling, through any alias. `tagged_enum_payload_offset(`,
# `resolve_tagged_enum(` and `register_tagged_enum(` are different identifiers
# and are not matched (the `[^_]` guard).
SEL_RE='(^|[^_[:alnum:]])(niche_enum|tagged_enum)\(|(lay|layout)::Uni[^a-zA-Z_]'
shape_scan() {   # shape_scan <root>...; prints offending "file:line: text"
    { grep -rnE "$SEL_RE" "$@" --include=*.cpp --include=*.hpp 2>/dev/null || true; } \
    | while IFS= read -r hit; do
        [ -n "$hit" ] || continue
        b=$(basename "${hit%%:*}")
        [ "$b" != "layout_law.hpp" ] || continue   # the law itself
        # A comment quoting a rule name is prose, not a call.
        case "$hit" in
            *:[0-9]*:*"//"*) continue ;;
            *:[0-9]*:*" * "*) continue ;;
        esac
        printf '%s\n' "$hit"
    done
}
BAD_SHAPE=$(shape_scan "$SRC_ROOT")
if [ -n "$BAD_SHAPE" ]; then
    echo "FAIL: a TU outside the law SELECTS a layout rule for itself:"
    echo "$BAD_SHAPE"
    echo "       The rule selectors (Uni / niche_enum / tagged_enum) belong to"
    echo "       layout_law.hpp. An engine supplies FACTS —"
    echo "       agg_shape(transparent, union, n), enum_layout(has_payload,"
    echo "       packed, payload, backing) — and the law takes the branch."
    echo "       A branch written here is a branch that can be MISSING here, and"
    echo "       a missing branch reads as a complete thought from the inside."
    exit 1
fi
# ⚠ CANARY: a planted TU with exactly the hand-rolled branch this arm forbids,
# scanned by the SAME function, must come back flagged.
mkdir -p "$TMPD/shapecanary"
cat >"$TMPD/shapecanary/canary_engine_shape.cpp" <<'EOF'
// canary — a fifth engine selecting the rule itself; the gate's own scan must
// flag both selectors below.
Layout bad_engine(StructView sv, bool packed, L payload) {
    lay::Uni u;
    for (auto f : sv.fields()) u.push(member(f));
    return packed ? niche_enum(payload) : tagged_enum(payload);
}
EOF
SHAPE_HIT=$(shape_scan "$SRC_ROOT" "$TMPD/shapecanary")
if ! grep -q 'canary_engine_shape\.cpp' <<<"$SHAPE_HIT"; then
    echo "FAIL (CANARY 'rule-selector scan' NOT CAUGHT): a planted TU that names"
    echo "      lay::Uni and chooses between niche_enum and tagged_enum was NOT"
    echo "      reported by the same scan that just said the tree is clean."
    echo "      THE GATE IS BROKEN, not the tree. scan returned: '${SHAPE_HIT}'"
    exit 1
fi
if [ "$(grep -c 'canary_engine_shape' <<<"$SHAPE_HIT")" -ne "$(grep -c . <<<"$SHAPE_HIT")" ]; then
    echo "FAIL (CANARY): the rule-selector scan flagged something other than the"
    echo "      planted file:"; echo "$SHAPE_HIT"; exit 1
fi
echo "[layout-gate] the rule selectors (Uni / niche_enum / tagged_enum) are named"
echo "              ONLY by layout_law.hpp"
caught "rule-selector scan" "the planted TU naming lay::Uni and both enum rules"

# ── 1. THE BASELINE, AND ITS POPULATION IS DERIVED FROM THE STDLIB TREE ──────
#
# ⚠⚠ THE PREVIOUS BASELINE IMPORTED NOTHING, AND ITS ZERO SAID SO.
#
# It was `package layout_gate_base; fn main() -> i64 { return 0; }` — two lines
# whose whole content is that they have no types. It reported "0 declined, 0
# disagreements" BECAUSE THERE WAS ALMOST NOTHING TO DISAGREE ABOUT: the walk
# reached only what a bare program links, and every stdlib type that no such
# program mentions was outside the comparison, indistinguishable from a type
# that agreed. MEASURED 2026-08-01: adding ONE `use logos.lang.writ.container;`
# to that same file took it from 0 disagreements to 4, and the compile aborted
# with "the compiler's layout engines disagree" — `Rc<dyn Resident>` was 8 bytes
# to sema and 16 to llvm::DataLayout, and `HeldAny` 16 against 24.
#
# So the import list is not a list any more. It is EVERY `package` declared
# under `stdlib/`, read off the tree at run time: a new stdlib module is in this
# gate's population the moment it exists, and a module that stops existing takes
# its own count down with it. Nobody adds a `use` line here, and nobody can
# forget to.
#
# MEASURED, same day, same build: 182 packages; the walk goes 3676 → 6314 struct
# types, 9810 → 17906 fields, 64 → 430 sema answers, 58 → 151 enum types.
STDLIB_DIR="$HERE/../../stdlib"
if [ ! -d "$STDLIB_DIR" ]; then
    echo "FAIL: no stdlib tree at $STDLIB_DIR — this gate's baseline population is"
    echo "       DERIVED from it, and a missing tree would silently give the empty"
    echo "       program whose zero is what this arm exists to stop being."
    exit 1
fi
# `grep -h` over the files, first match per file, comments stripped: a `package`
# named only inside a comment is not a package. The list is sorted -u because
# one package is declared by several files.
: >"$TMPD/pkgs.txt"
while IFS= read -r f; do
    sed 's://.*::' "$f" \
      | grep -m1 -oE '^[[:space:]]*package[[:space:]]+[A-Za-z0-9_.]+' \
      | grep -oE '[A-Za-z0-9_.]+$' >>"$TMPD/pkgs.txt" || true
done < <(find "$STDLIB_DIR" -name '*.logos' -type f | sort)
sort -u "$TMPD/pkgs.txt" -o "$TMPD/pkgs.txt"
N_PKGS=$(grep -c . "$TMPD/pkgs.txt" || true)
# ⚠ A FLOOR ON THE DERIVATION ITSELF. An empty or truncated package list gives
# back exactly the old two-line program, and its clean report would read the
# same. MEASURED 2026-08-01: 182 packages under stdlib/.
if [ "$N_PKGS" -lt "$MIN_STDLIB_PKGS" ]; then
    echo "FAIL: derived $N_PKGS stdlib packages, floor $MIN_STDLIB_PKGS (MEASURED"
    echo "       2026-08-01). The baseline's population comes from this list; a"
    echo "       short list is a smaller comparison reporting the same zero."
    exit 1
fi
{
    echo "package layout_gate_stdbase;"
    sed 's/^/use /; s/$/;/' "$TMPD/pkgs.txt"
    echo 'fn main() -> i64 { return 0; }'
} >"$TMPD/stdbase.logos"
census "$TMPD/stdbase.logos"
STD_TYPES=$N_TYPES; STD_FIELDS=$N_FIELDS; STD_DEFS=$N_DEFS
echo "[layout-gate] stdlib baseline over $N_PKGS DERIVED packages: $STD_TYPES struct types, $STD_FIELDS fields, $STD_DEFS defs"

# The EMPTY program stays, and it is not a second baseline — it is the REFERENCE
# the lattice's delta is measured against (§2 below). Both are compiled; the
# derived one is what makes the comparison big, the empty one is what makes the
# lattice's contribution a difference of two numbers taken the same way.
cat >"$TMPD/base.logos" <<'EOF'
package layout_gate_base;
fn main() -> i64 { return 0; }
EOF
census "$TMPD/base.logos"
BASE_TYPES=$N_TYPES; BASE_FIELDS=$N_FIELDS; BASE_DEFS=$N_DEFS
echo "[layout-gate] empty-program reference: $BASE_TYPES struct types, $BASE_FIELDS fields, $BASE_DEFS defs"
floor() {   # floor <what> <got> <want>
    # ⚠ THE NON-NUMBER IS CHECKED FIRST, EXPLICITLY. `[ "$got" -lt N ]` on
    # anything that is not an integer exits 2 with "integer expression expected"
    # and `if` reads a non-zero status as FALSE — the floor then never fires.
    # That is exactly how this gate stayed green with `defs` unparsed. Every
    # value reaching here now comes from verdict.py and IS an integer; this
    # guard is so that a future arithmetic path cannot re-open the hole.
    case "$2" in
        ''|*[!0-9-]*)
            echo "FAIL: $1 — the measured value is '$2', which is NOT A NUMBER."
            echo "       A floor cannot be applied to it, and a floor that cannot"
            echo "       be applied must never read as a floor that held."
            exit 1 ;;
    esac
    if [ "$2" -lt "$3" ]; then
        echo "FAIL: $1 — observed $2, floor $3 (MEASURED 2026-08-01)."
        echo "       A floor here is the value this gate actually saw, not a"
        echo "       fraction of it. If the drop is deliberate, edit the floor and"
        echo "       put its ground in the commit message."
        exit 1
    fi
}
floor "baseline struct types the verifier walked" "$BASE_TYPES"  "$MIN_BASELINE_TYPES"
floor "baseline fields compared (B vs C)"         "$BASE_FIELDS" "$MIN_BASELINE_FIELDS"
floor "baseline defs compared (A vs C)"           "$BASE_DEFS"   "$MIN_BASELINE_DEFS"
# The DERIVED arm's own floors. These are the ones that move when the stdlib
# grows a type, and they are ~1.7x the empty program's on every axis.
floor "stdlib-baseline struct types the verifier walked" "$STD_TYPES"  "$MIN_STDLIB_TYPES"
floor "stdlib-baseline fields compared (B vs C)"         "$STD_FIELDS" "$MIN_STDLIB_FIELDS"
floor "stdlib-baseline defs compared (A vs C)"           "$STD_DEFS"   "$MIN_STDLIB_DEFS"
# ⚠ AND THE DERIVED POPULATION MUST BE STRICTLY BIGGER THAN THE EMPTY ONE. If a
# future edit breaks the import generation — a bad `sed`, a moved stdlib, a
# `package` spelling this scan stops matching — the generated program degrades
# gracefully into the EMPTY program, whose census is a valid, clean verdict.
# That is the failure this whole section is about, one level up, so it is tested
# and not assumed.
if [ "$STD_TYPES" -le "$BASE_TYPES" ]; then
    echo "FAIL: the DERIVED stdlib baseline walked $STD_TYPES struct types and the"
    echo "       EMPTY program walked $BASE_TYPES. The import generation produced a"
    echo "       program no larger than importing nothing, so this arm is reporting"
    echo "       about the empty program under another name."
    exit 1
fi

# ── 1b. THE FOUR-ENGINE CANARY, on that same baseline program ────────────────
# Each engine in turn is told to answer ONE BYTE wrong. The census must come
# back nonzero and name it. This is the same compile, the same census line, the
# same `N_BAD` field and the same test that judged the run above — inverted.
for eng in layout_of mlir_abi_size sema_abi_layout mono_abi_layout; do
    canary "$TMPD/base.logos" "$eng"
done

# ── 1c. THE CANARY FOR `declined` ────────────────────────────────────────────
# The four canaries above ride the DISAGREEMENT path: two engines answered and
# differ. `declined` is the other half — an engine that did not answer at all —
# and it reads 0 on every program here. A field that is always 0 and has never
# been shown able to be anything else is exactly the failure this gate exists to
# remove: it is indistinguishable from a field nobody computes. So one synthetic
# decline is injected and BOTH numbers must move.
canary_declined() {
    local src=$1
    census_raw "$src" "declined"
    if [ "$HAVE_VERDICT" -eq 0 ]; then
        echo "FAIL (CANARY 'declined'): no 'layout-verify-json:' verdict at all."
        sed -n '1,20p' "$TMPD/err"; exit 1
    fi
    if [ "$N_DECLINED" -lt 1 ]; then
        echo "FAIL (CANARY 'declined' NOT CAUGHT): a decline was injected and the"
        echo "       verifier still reported declined=$N_DECLINED. The zero this"
        echo "       gate floors on every real program is therefore a statement"
        echo "       about nothing. THE GATE IS BROKEN, not the tree."
        grep -m1 '^layout-verify-json:' "$TMPD/err" || true
        exit 1
    fi
    if [ "$N_BAD" -lt 1 ]; then
        echo "FAIL (CANARY 'declined'): declined=$N_DECLINED was counted but"
        echo "       disagreements=$N_BAD — a decline is not reaching the rows"
        echo "       that make the compile fail, so it would be a footnote."
        exit 1
    fi
    if ! grep -q -- 'DECLINED —' "$TMPD/err"; then
        echo "FAIL (CANARY 'declined'): counted but no 'DECLINED —' row names it,"
        echo "       so a real decline would be a number with nothing to act on."
        sed -n '1,20p' "$TMPD/err"; exit 1
    fi
    caught "declined" "declined=$N_DECLINED, and it reached the failure rows" \
           "(disagreements=$N_BAD)"
}
canary_declined "$TMPD/base.logos"

# ── 2. the lattice ───────────────────────────────────────────────────────────
# Generated here, from the axes, so "which shapes are covered" is a loop and not
# a list somebody maintains. Each type is CONSTRUCTED and read back, so it is
# reachable code and really gets registered.
python3 "$HERE/layout_lattice_gen.py" lattice "$TMPD/lattice.logos" 2>"$TMPD/lat.count"
grep -v '^lattice-gen-json:' "$TMPD/lat.count" || true
# The generator emits its own tally as a structured verdict; a rename of `types`
# is exit 3 here, not an empty string that a `-z` test happens to catch and a
# floor happens not to.
read_verdict "$TMPD/lat.count" 'lattice-gen-json:' "the lattice generator" \
    --exact-keys types --bind GEN_TYPES=types
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

# ── 2b. THE ENGINE × SHAPE MATRIX, CELL BY CELL ──────────────────────────────
# EVERY cell of EVERY engine, floored at the value MEASURED on this lattice.
# There is no aggregate here on purpose: an aggregate lets a branch stop being
# reached while another cell grows over it, which is precisely the shape of the
# defect that started this arc.
echo "[layout-gate] ENGINE × SHAPE — answers of each shape checked against"
echo "              llvm::DataLayout and against the other engines:"
# ⚠ ALL 18 CELLS IN ONE STRICT READ. The old form scraped each cell with
# `sed -nE "s/^layout-matrix: $eng .*$shape=([0-9]+).*/\\1/p"` and guarded the
# empty case by hand. Here the whole matrix is a nested object in the verdict:
# `matrix.<engine>.<shape>` either resolves to an integer or the parser exits 3
# naming the path AND listing the keys that ARE at that level — so a renamed
# engine, a renamed shape, a dropped row and a dropped column all say what they
# are, in one message, instead of eighteen "cell is empty" guesses.
grep -E '^layout-matrix: ' "$TMPD/err" | sed 's/^/              /' || true
read_verdict "$TMPD/err" 'layout-verify-json:' "the ENGINE × SHAPE matrix" \
    --exact-keys "$VERDICT_KEYS" \
    --floor matrix.layout_of.product          "$MIN_LO_PRODUCT" \
    --floor matrix.layout_of.union            "$MIN_LO_UNION" \
    --floor matrix.layout_of.transparent      "$MIN_LO_TRANSPARENT" \
    --floor matrix.layout_of.c-like           "$MIN_LO_CLIKE" \
    --floor matrix.layout_of.tagged           "$MIN_LO_TAGGED" \
    --floor matrix.layout_of.niche            "$MIN_LO_NICHE" \
    --floor matrix.mono_abi_layout.product    "$MIN_MO_PRODUCT" \
    --floor matrix.mono_abi_layout.union      "$MIN_MO_UNION" \
    --floor matrix.mono_abi_layout.transparent "$MIN_MO_TRANSPARENT" \
    --floor matrix.mono_abi_layout.c-like     "$MIN_MO_CLIKE" \
    --floor matrix.mono_abi_layout.tagged     "$MIN_MO_TAGGED" \
    --floor matrix.mono_abi_layout.niche      "$MIN_MO_NICHE" \
    --floor matrix.sema_abi_layout.product    "$MIN_SE_PRODUCT" \
    --floor matrix.sema_abi_layout.union      "$MIN_SE_UNION" \
    --floor matrix.sema_abi_layout.transparent "$MIN_SE_TRANSPARENT" \
    --floor matrix.sema_abi_layout.c-like     "$MIN_SE_CLIKE" \
    --floor matrix.sema_abi_layout.tagged     "$MIN_SE_TAGGED" \
    --floor matrix.sema_abi_layout.niche      "$MIN_SE_NICHE" \
    --floor enum_types                        "$MIN_ENUM_TYPES" \
    --bind N_TYPES=struct_types --bind N_FIELDS=fields \
    --bind N_DEFS=defs --bind N_ENUMS=enum_types \
    --bind N_BAD=disagreements \
    --bind-opt N_SEMA=engines.sema_abi_layout:0 \
    --bind-opt N_MONO=engines.mono_abi_layout:0
# ⚠ A ZERO CELL IS A MISSING BRANCH OR A MISSING SHAPE IN THE CORPUS: the engine
# never took that branch on the whole lattice — either it does not have it (the
# bug this matrix exists to name) or nothing in the corpus has that shape any
# more (the same blindness one level up). Both are red; the floors above are the
# MEASURED values and none of them is 0.
echo "[layout-gate] all 18 cells at or above their measured floors,"
echo "              $N_ENUMS enum types sized by llvm::DataLayout"

# ⚠ CANARY FOR THE ENUM ARM SPECIFICALLY. The four-engine canary above proves
# the comparison is live for an engine; it does not prove the ENUM rows reach
# it, and enum rows are new — `truth` used to be built from `struct_types_`
# alone, so an enum entered the comparison only when some struct happened to
# have a field of it. Corrupt mono and demand that a reported row names a
# C-LIKE ENUM of the lattice by name.
census_raw "$TMPD/lattice.logos" mono_abi_layout
if [ "$N_BAD" -lt 1 ]; then
    echo "FAIL (CANARY 'enum arm'): mono was told to answer one byte wrong and the"
    echo "      verifier reported $N_BAD disagreements."
    exit 1
fi
if ! grep -qE 'layout_gate_lattice\.(EB_|GB_)[a-z0-9]+:' "$TMPD/err"; then
    echo "FAIL (CANARY 'enum arm' NOT CAUGHT): $N_BAD disagreements were reported"
    echo "      and NOT ONE of them names a C-like enum of the lattice"
    echo "      (layout_gate_lattice.EB_* / GB_*). The enum rows are not reaching"
    echo "      the comparison, so the 'c-like' cells above are counting answers"
    echo "      that nothing checks. THE GATE IS BROKEN, not the tree."
    grep -m5 'says' "$TMPD/err" || true
    exit 1
fi
caught "enum rows reach the comparison" "e.g. $(grep -m1 -E 'layout_gate_lattice\.(EB_|GB_)' "$TMPD/err" | sed 's/^ *//')"

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
grep -v '^oracle-gen-json:' "$TMPD/or.count" || true
# These four were PRINTED and never asserted, so the oracle could shrink to one
# probe and the gate would still say "every measured offset matched". Read and
# floored in ONE strict pass; `canary: 0` is asserted too, so the REAL oracle can
# never be silently replaced by the inverted one.
read_verdict "$TMPD/or.count" 'oracle-gen-json:' "the run-oracle generator" \
    --exact-keys prefixes,offsets,dstref_widths,codes,canary \
    --floor dstref_widths "$MIN_ORACLE_DSTREF" \
    --floor prefixes      "$MIN_ORACLE_PREFIXES" \
    --floor offsets       "$MIN_ORACLE_OFFSETS" \
    --floor codes         "$MIN_ORACLE_CODES" \
    --eq    canary 0 \
    --bind OR_PREFIXES=prefixes --bind OR_OFFSETS=offsets \
    --bind OR_CODES=codes --bind OR_DSTREF=dstref_widths
census "$TMPD/oracle.logos"
link_run_rc "$TMPD/x.o" oracle
# ⚠ THE EXIT STATUS IS A BOOLEAN AND THE DIAGNOSIS IS THE REPORT. A process exit
# status is EIGHT BITS; this oracle allocates 272 distinct codes. MEASURED
# 2026-08-01 on the previous form of this gate, which read the code off `$?`:
# an oracle forced to fail at code 255 exited 255 → RED; forced at code 256 it
# exited 0 → GREEN, 256 & 0xFF == 0. Codes 257…272 aliased onto probes 1…16, so
# a failure in the u56/i56/u64/i64 DstRef rows — the exact cell of the generic
# backing-width miscompile — named a DST prefix shape instead. The class was
# diagnosed and fixed here on 07-19 across all 33 conuco tests; this gate,
# written a week later to stop gates from lying, reproduced it.
#
# So `main` returns 0 or 1 and writes `oracle-run-json:`. The code lives in
# `first`, where it has no ceiling, and `probes` is a RUN-TIME census: asserting
# it equals the number the generator EMITTED is what makes "exit 0" mean "272
# assertions executed and none disagreed" rather than "nothing ran".
run_boolean() {   # run_boolean <name> <want-rc>; the status may only be 0 or 1
    if [ "$RUN_RC" -ne 0 ] && [ "$RUN_RC" -ne 1 ]; then
        echo "FAIL: $1 exited $RUN_RC. Its exit status carries ONE BIT — 0 or 1 —"
        echo "      so any other value is a crash or a signal, not a verdict, and"
        echo "      no report below it can be trusted."
        cat "$TMPD/$1.out"; exit 1
    fi
    if [ "$RUN_RC" -ne "$2" ]; then
        echo "FAIL: $1 exited $RUN_RC, want $2."; cat "$TMPD/$1.out"; exit 1
    fi
}
run_boolean oracle 0
read_verdict "$TMPD/oracle.out" 'oracle-run-json:' "the run oracle" \
    --exact-keys probes,failures,first \
    --eq failures 0 --eq first 0 --eq probes "$OR_CODES" \
    --bind ORUN_PROBES=probes
echo "[layout-gate] run oracle: exit 0, $ORUN_PROBES assertions EXECUTED (= the"
echo "              $OR_CODES the generator emitted) — every measured tail offset"
echo "              and every measured field offset matched the compiler's claim"

# ⚠ CANARY. The same generator emits the same program with the FIRST probe's
# comparison inverted: it returns 1 exactly when the compiler is right. It goes
# through the same `census`, the same link, the same run and the same `RUN_RC`
# read. An oracle whose probes never executed — dead code, an early return, a
# `main` that returns 0 before the block — produces a canary that exits 0, and
# THAT is what this catches. "exit 0" from the real oracle then means "the
# probes ran and agreed", not "the probes were not there".
python3 "$HERE/layout_lattice_gen.py" oracle-canary "$TMPD/oracle_canary.logos" 2>"$TMPD/orc.count"
# ⚠ AND THE CANARY GENERATOR MUST SAY IT INVERTED SOMETHING. `oracle-canary` and
# `oracle` differ by one flipped comparison; if the mode argument stopped being
# honoured, the "canary" would be the real oracle and its exit 0 would be read
# as "the probes did not run". `canary: 1` is that difference, in the verdict.
read_verdict "$TMPD/orc.count" 'oracle-gen-json:' "the run-oracle CANARY generator" \
    --exact-keys prefixes,offsets,dstref_widths,codes,canary \
    --eq canary 1 \
    --floor prefixes "$MIN_ORACLE_PREFIXES" --floor offsets "$MIN_ORACLE_OFFSETS"
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
run_boolean oracle_canary 1
# …and the canary must NAME what it caught. `failures: 1, first: 1` is the
# inverted probe and nothing else: a canary that came back with `first: 7` would
# mean the run is failing for an unrelated reason and the inversion proves
# nothing. `probes` is asserted here too, so "the canary fired" cannot be an
# oracle that aborted after one assertion.
read_verdict "$TMPD/oracle_canary.out" 'oracle-run-json:' "the run-oracle CANARY" \
    --exact-keys probes,failures,first \
    --eq failures 1 --eq first 1 --eq probes "$OR_CODES"
caught "run oracle first probe inverted" \
       "exit 1, failures=1, first=1, $OR_CODES assertions still executed"

# ── 4. the authored fixtures compile, run, and exit 0 ────────────────────────
NFIX=0
for f in layout_adjacent_narrow_fields layout_zero_size_enum_payload \
         layout_dst_prefix_and_offset_of layout_clike_enum_backing; do
    src="$HERE/pass/$f.logos"
    [ -f "$src" ] || { echo "FAIL: missing fixture $src"; exit 1; }
    census "$src"
    link_and_run "$TMPD/x.o" "$f"
    NFIX=$((NFIX + 1))
    echo "[layout-gate] $f: exit 0, $N_TYPES struct types verified"
done
floor "authored layout fixtures run" "$NFIX" 4

echo "[layout-gate] OK — stdlib baseline $STD_TYPES/$STD_FIELDS/$STD_DEFS over"
echo "              $N_PKGS packages DERIVED from the stdlib tree (nobody maintains"
echo "              that import list); empty-program reference"
echo "              $BASE_TYPES/$BASE_FIELDS/$BASE_DEFS, lattice +$DELTA"
echo "              (generator emitted $GEN_TYPES shapes), on the lattice: sema"
echo "              $LAT_SEMA / mono $LAT_MONO early-engine answers checked against"
echo "              llvm::DataLayout, 0 disagreements, all 18 ENGINE × SHAPE"
echo "              cells at or above their measured floors. EVERY NUMBER ABOVE"
echo "              WAS READ BY verdict.py OUT OF A JSON VERDICT — no sed on"
echo "              prose, no grep -c as a measurement — and the parser passed"
echo "              its own selftest first — whose case count it PRINTS, so no"
echo "              number here restates a measurement nothing pins."
# ⚠ AND NEITHER DOES THIS ONE. Both the count and the list are what `caught`
# accumulated during THIS run; the floor is the only maintained number, and a
# canary that was deleted rather than fixed lands under it.
floor "canaries caught" "$N_CANARIES" "$MIN_CANARIES"
echo "[layout-gate] $N_CANARIES canaries caught, every one of them announced by the"
echo "              site that caught it: ${CANARY_NAMES[*]}"
