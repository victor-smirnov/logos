#!/usr/bin/env bash
# drain_import_pair_gate.sh LOGOSC PASS_DIR WQL_LOGOS REXPR_WALK_LOGOS
#
# THE IMPORT PAIR THAT MAKES THE DRAIN NODE'S `Buffer` RESOLVE — ADR 0025 S3a.
#
# ── WHY A GATE AT ALL: AN UNCHECKED EXEMPTION IS WORSE THAN NO GATE ──────────
#
# Landing `Drain -> Buffer` (S3b) needed TWO source lines that look like one
# duplicated line, and a reader who deletes "the redundant one" gets a green
# stdlib and a corpus that stops compiling:
#
#   HALF 1  `use logos.mem.stream;` in `stdlib/mem/wql/wql.logos` — the TRIGGER
#           module's import. It names nothing in that file. Its job is to LOAD
#           the package into the trigger module's import closure.
#   HALF 2  `use logos.mem.stream;` written LITERALLY inside BOTH `quote_item!`s
#           of `rexpr_walk::emit_fn_quote_blob`. Its job is to make the NAME
#           visible inside the spliced item, which inherits none of the trigger
#           module's imports.
#
# THE MECHANISM THAT MAKES BOTH NECESSARY: a spliced item's own `use P;` makes
# P's names VISIBLE but does NOT LOAD P. If P is not already in the trigger
# module's closure, the use decl is present in the emitted dump and the types
# still do not resolve — the failure mode that costs the most time to read,
# because the fix appears to already be in the artifact.
#
# ── THE ABUSE-DIRECTION MEASUREMENT (each half removed ALONE, ONE AT A TIME,
#    full stdlib rebuild for each, restored to a byte-identical source with a
#    GREEN 6/6 checkpoint between and after) ────────────────────────────────
#
#   BOTH HALVES        6 / 6 drain fixtures compile.
#   HALF 1 REMOVED     1 pass / 5 FAIL — `unknown generic type 'Buffer'` in
#     (quote literal      `q_selfstep_run`, `q_loop_run`, `w_rel_block_run`,
#      alone)             `tail_count_run`, `iter_step_run`. The survivor is
#                         `deem_batch_scan_drain`, and it survives for a reason
#                         that is NOT the import working: that fixture loads
#                         `logos.mem.stream` transitively through Memoria. This
#                         asymmetry is the whole hazard — a reader who probes
#                         with the one convenient batch fixture measures GREEN
#                         and concludes half 1 is dead weight.
#   HALF 2 REMOVED     0 pass / 6 FAIL — the same error, and `use
#     (trigger import     logos.mem.stream;` is PRESENT in the emitted dump of
#      alone)             every one of them. Visible is not loaded.
#
# ── WHAT THIS GATE CAN AND CANNOT CHECK, SAID PLAINLY ───────────────────────
#
# It CANNOT re-run the measurement above: removing either half requires a full
# stdlib rebuild, which is not a thing a test may do. So this gate is a
# PRESENCE pin over the two lines, TIED TO THE ARTIFACT so it is not merely a
# text lint over prose:
#
#   FACT 1  both halves are present in the two source files (half 2 in BOTH
#           `quote_item!`s — the priv and the pub arm; one arm alone silently
#           covers only half the corpus);
#   FACT 2  the artifact actually needs them: a drain fixture's `--gen-dir` dump
#           carries BOTH the spliced `use logos.mem.stream;` AND a `Buffer<`
#           landing. If S3b's drain arm ever stops spelling `Buffer`, FACT 2
#           goes red and this gate retires WITH its subject instead of standing
#           as a lint over a fact nobody needs any more.
#   FACT 3  ALL drain fixtures compile — the state the measurement above calls
#           the green checkpoint. Six when that measurement was taken, seven
#           since S3f added the empty-landing fixture; the list is derived from
#           the corpus by `grep -l 'Buffer<'` and is re-derived when one lands.
#
# The numbers in the measurement block are the falsifiable part: re-run it by
# deleting one line and rebuilding, and it must reproduce 5-fail / 6-fail, not
# 6-fail / 6-fail (which would mean the halves are redundant after all).
set -uo pipefail

LOGOSC=${1:?usage: drain_import_pair_gate.sh <logosc> <pass dir> <wql.logos> <rexpr_walk.logos>}
PASSD=${2:?usage: drain_import_pair_gate.sh <logosc> <pass dir> <wql.logos> <rexpr_walk.logos>}
WQL=${3:?usage: drain_import_pair_gate.sh <logosc> <pass dir> <wql.logos> <rexpr_walk.logos>}
RW=${4:?usage: drain_import_pair_gate.sh <logosc> <pass dir> <wql.logos> <rexpr_walk.logos>}

export LC_ALL=C
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fails=0
note() { echo "FAIL: $*"; fails=$((fails+1)); }

# EVERY fixture in the corpus that emits a `Buffer` landing — the plans that
# contain a Drain (or Sort) node over a native source. Measured off the corpus
# snapshot, not guessed: `grep -l 'Buffer<'` over all 161 dumps.
#
# ⚠ THIS LIST GROWS WITH THE CORPUS AND MUST. Six at S3b — exactly the files
# whose dump moved when `Vec` -> `Buffer` (10 sites, +60 bytes), which is the set
# the abuse-direction measurement above was taken over, and the 1/5 and 0/6
# figures there stay stated in those terms because that is what was run. S3f
# added the seventh (`deem_drain_buffer_empty`, the empty landing), so FACT 3 is
# 7/7 today. A new drain fixture that is NOT added here is not a hole in the pin
# (the halves are still checked, and the census gate counts drains corpus-wide)
# but it is a fixture this gate's compile clause does not cover, so the list is
# re-derived with the same one-line grep whenever a drain fixture lands.
FIX=(deem_batch_scan_drain deem_drain_buffer_empty deem_join_step_reread
     deem_join_step_streams deem_mat_ground_witness deem_pushdown_all_shapes
     wql_deferred_plan_e2e)

# ── FACT 1 — both halves present, half 2 in BOTH quote arms ─────────────────
n1=$(grep -cE '^use logos\.mem\.stream;' "$WQL")
[ "$n1" -ge 1 ] || note "HALF 1 is gone: \`use logos.mem.stream;\` is not a
      top-level import of $WQL. Without it the package is never LOADED and the
      quote literal resolves nothing — measured: 5 of 6 drain fixtures fail
      with \`unknown generic type 'Buffer'\`."

n2=$(grep -cE '^[[:space:]]+use logos\.mem\.stream;' "$RW")
[ "$n2" -eq 2 ] || note "HALF 2 is $n2 of the required 2: \`use
      logos.mem.stream;\` must appear in BOTH \`quote_item!\`s of
      emit_fn_quote_blob (the priv arm and the pub arm). One arm alone covers
      only the queries whose entry fn has that visibility. Measured with the
      literal absent from both: 6 of 6 drain fixtures fail."

# ── FACT 3 (run first — it produces the dumps FACT 2 reads) ─────────────────
np=$(nproc)
probe() {
    local b="$1" D="$2" LOGOSC="$3" PASSD="$4"
    "$LOGOSC" "$PASSD/$b.logos" --gen-dir "$D/$b.gen" -o "$D/$b.o" \
        > "$D/$b.out" 2> "$D/$b.err"
    echo "$?" > "$D/$b.rc"
}
export -f probe
printf '%s\0' "${FIX[@]}" \
  | xargs -0 -P "$np" -I{} bash -c 'probe "$@"' _ {} "$TMPD" "$LOGOSC" "$PASSD"

shopt -s nullglob
RCS=("$TMPD"/*.rc)
if [ "${#RCS[@]}" -ne "${#FIX[@]}" ]; then
    note "${#RCS[@]} rc files for ${#FIX[@]} probes — probes were LOST, so a
      green here would be an absence of evidence. (rc comes from a redirect per
      probe, never from a pipeline's tail.)"
fi

ok=0
for b in "${FIX[@]}"; do
    r=$(cat "$TMPD/$b.rc" 2>/dev/null || echo missing)
    if [ "$r" = "0" ]; then
        ok=$((ok+1))
    else
        note "drain fixture $b does not compile (rc=$r):
      $(grep -m1 -i 'error' "$TMPD/$b.out" "$TMPD/$b.err" 2>/dev/null | head -1)
      This is the exact failure the import pair exists to prevent."
    fi
done
[ "$ok" -eq "${#FIX[@]}" ] || note "only $ok of ${#FIX[@]} drain fixtures compile."

# ── FACT 2 — the artifact needs the pair ────────────────────────────────────
#
# Read on the fixture that does NOT get the package transitively through
# Memoria, so a passing FACT 2 is evidence about the IMPORT PAIR and not about
# Memoria's dependency graph. (`deem_batch_scan_drain` would pass FACT 2 even
# with half 1 deleted — that is precisely the trap recorded above.)
WIT=deem_join_step_streams
shopt -s nullglob
DUMPS=("$TMPD/$WIT.gen"/*.gen.logos)
if [ "${#DUMPS[@]}" -lt 1 ]; then
    note "no --gen-dir dump for $WIT, so FACT 2 asked nothing. A gate whose
      oracle produced no text is not green, it is blind."
else
    cat "${DUMPS[@]}" > "$TMPD/wit.txt"
    u=$(grep -c '^use logos\.mem\.stream;' "$TMPD/wit.txt")
    b=$(grep -c 'Buffer<' "$TMPD/wit.txt")
    [ "$u" -ge 1 ] || note "$WIT's emitted item carries NO \`use
      logos.mem.stream;\` — half 2 stopped reaching the splice."
    [ "$b" -ge 1 ] || note "$WIT's emitted item spells no \`Buffer<\`. Either
      the Drain node's landing stopped being a Buffer (S3b reverted, in which
      case RETIRE THIS GATE WITH ITS SUBJECT rather than deleting the assert),
      or this fixture stopped having a drain — in which case pick another
      witness from the six and say which."
fi

if [ "$fails" -gt 0 ]; then
    echo "drain_import_pair_gate: $fails failure(s)."
    exit 1
fi
echo "drain_import_pair_gate: OK — pair present (1 + 2), ${#FIX[@]}/${#FIX[@]} drain fixtures compile, artifact carries use+Buffer."
exit 0
