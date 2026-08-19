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

# ⚠ A GATE THAT CANNOT MEASURE MUST SAY SO, NOT SCORE ZERO. `${n:?}` above only
# catches an argument that is ABSENT; an argument that is present and does not
# RESOLVE (a moved source file, a renamed pass dir) left every `grep -c` reading
# 0 and the arithmetic comparisons printing `[: : integer expression expected`
# while the gate went on to a verdict. Observed during the S5-direct round.
# A missing subject is a could-not-measure, which is a different failure from a
# pin that moved — and the difference is exactly what makes a green trustworthy.
for _subj in "$LOGOSC" "$WQL" "$RW"; do
    [ -e "$_subj" ] || { echo "FAIL(2): subject does not resolve: $_subj"; exit 2; }
done
[ -d "$PASSD" ] || { echo "FAIL(2): pass dir does not resolve: $PASSD"; exit 2; }

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

# ⚠ SCOPED PER EMITTING FUNCTION SINCE ADR 0025 §12 `direct` (2026-08-19).
# The count used to be a FILE-WIDE `grep -c` pinned at 2, and the 2 carried the
# claim "one per quote arm of `emit_fn_quote_blob`". `emit_stream_direct` now
# emits a THIRD quote — the §12 direct door's state struct + impls — which needs
# the same import for the same reason (`RowsBatch`, `BatchStream` and the
# `Buffer` sibling are named inside a spliced item that inherits nothing), so a
# file-wide count reads 3. Re-typing the literal to 3 would have thrown the
# claim away: 3 is also what "one arm of `emit_fn_quote_blob` plus two in the
# direct emitter" reads, which is the exact defect this gate exists to catch.
# So the count is taken PER FUNCTION instead — strictly stronger than the file
# total, and it stays true whatever the next emitter adds.
fn_body() {   # $1 = fn name; prints its body, up to the next top-level `fn `
    # ⚠ `[([:space:]]` — a bracket expression holding `(` AND the space class.
    # The first spelling here was `[(:space:]`, which is NOT that: it closes at
    # the final `]`, so awk reads it as the SEVEN-CHARACTER SET `( : s p a c e`.
    # That set contains `(`, which is why both names this gate asks for matched
    # and every count read correctly — the defect was LATENT, not live, and
    # saying so is the point: it made the anchor accept any name that merely
    # EXTENDS the wanted one with `s`, `p`, `a`, `c`, `e` or `:`, and `fn_body`
    # would then have printed a body that is not its own. Bite-proved on a copy
    # of `rexpr_walk.logos` carrying a decoy `fn emit_stream_directsam` with one
    # `use logos.mem.stream;` in it: the old spelling reads the DIRECT ARM as 2,
    # this one reads 1 and leaves the extra occurrence to the file-total clause,
    # which is where an unpinned quote belongs.
    # The second alternative (`"^fn " want "\\("`) is gone with it — it was what
    # made the malformed class survive, and one anchor that is right beats two
    # that overlap.
    awk -v want="$1" '
        /^fn [A-Za-z_0-9]+/ { infn = ($0 ~ "^fn " want "[([:space:]]") }
        infn { print }
    ' "$RW"
}
n2=$(fn_body emit_fn_quote_blob | grep -cE '^[[:space:]]+use logos\.mem\.stream;')
# ⚠ AND THE FILE-WIDE TOTAL IS PINNED TOO, BESIDE the per-function counts, not
# instead of them. The per-function counts are the stronger claim about the
# arms they name; they say NOTHING about an arm nobody named. A fourth emitter
# growing its own quote with the literal in it would leave both green — the
# total is what refuses to be silent about it. The two together are the
# both-directions rule: `2 + 1 == 3` must hold, so neither side can drift alone.
n2t=$(grep -cE '^[[:space:]]+use logos\.mem\.stream;' "$RW")
n2d=$(fn_body emit_stream_direct | grep -cE '^[[:space:]]+use logos\.mem\.stream;')
[ "$n2d" -eq 1 ] || note "HALF 2, §12 DIRECT ARM: \`use logos.mem.stream;\` occurs
      $n2d time(s) inside \`emit_stream_direct\`'s quote, want 1. The direct
      door's spliced item names \`RowsBatch\` and \`BatchStream\` and inherits
      none of the trigger module's imports, so it needs the literal exactly as
      the two \`emit_fn_quote_blob\` arms do."
[ "$n2" -eq 2 ] || note "HALF 2 is $n2 of the required 2: \`use
      logos.mem.stream;\` must appear in BOTH \`quote_item!\`s of
      emit_fn_quote_blob (the priv arm and the pub arm). One arm alone covers
      only the queries whose entry fn has that visibility. Measured with the
      literal absent from both: 6 of 6 drain fixtures fail."
[ "$n2t" -eq 3 ] || note "HALF 2, FILE TOTAL: \`use logos.mem.stream;\` occurs
      $n2t time(s) in rexpr_walk.logos, want 3 (= $n2 in emit_fn_quote_blob's
      two arms + $n2d in emit_stream_direct). A total that is not the sum of the
      arms this gate names is an emitting quote NOBODY here pins — name it and
      re-derive, do not re-type the 3."
[ $((n2 + n2d)) -eq "$n2t" ] || note "HALF 2, SEAM: the per-function counts
      ($n2 + $n2d) do not add to the file total ($n2t) — either \`fn_body\` has
      stopped bounding a body correctly or a quote outside both functions holds
      the literal. Either way one of the two sides is measuring nothing."

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
    # ⚠ ADR 0025 R-H CLOSING AUDIT, 2026-08-16 — NARROWED, SAME FLOOR, BECAUSE
    # THE BARE NEEDLE HAD GONE VACUOUS. `b` used to be `grep -c 'Buffer<'` with
    # the floor below, and the sentence it defends is about the DRAIN LANDING.
    # (b′) made the QUERY-OUTPUT landing a `Buffer` too (`let mut __out:
    # Buffer<E>` + `return Result::Ok(__out.into_vec())`, four emitter sites in
    # `rexpr_walk.logos`), so a bare `Buffer<` is now present in every emitted
    # query. MEASURED on this witness (`deem_join_step_streams`, this tree):
    # 5 `Buffer<` mentions — 1 drain landing (`let mut __rdb_s: Buffer<(i64,
    # i64)>`), 2 query-output landings, 2 `_stream` surface return types. With
    # the drain landing's type destroyed by hand the bare count still reads 4,
    # i.e. THE CLAUSE COULD NO LONGER FAIL FOR ITS OWN REASON — the permissive
    # shape a green corpus can never show you. Aimed at the drain landing BY
    # NAME (`__rdb_<rel>`, FACT K of `plan_ground_census_gate.sh`), floor
    # UNCHANGED at 1; the sibling `drain_read_once_pair_gate.sh` took the same
    # narrowing in the same round and this file was missed by its sweep.
    b=$(grep -cE 'let mut __rdb_[a-z_0-9]+: Buffer<' "$TMPD/wit.txt")
    [ "$u" -ge 1 ] || note "$WIT's emitted item carries NO \`use
      logos.mem.stream;\` — half 2 stopped reaching the splice."
    [ "$b" -ge 1 ] || note "$WIT's emitted item spells no \`let mut __rdb_<rel>:
      Buffer<…>\` drain landing. Either the Drain node's landing stopped being a
      Buffer (S3b reverted, in which case RETIRE THIS GATE WITH ITS SUBJECT
      rather than deleting the assert), or this fixture stopped having a drain —
      in which case pick another witness from the six and say which. Buffer
      mentions found: $(grep -oE 'let mut __[a-z_0-9]+: Buffer<' "$TMPD/wit.txt" | tr '\n' ' ')"
fi

if [ "$fails" -gt 0 ]; then
    echo "drain_import_pair_gate: $fails failure(s)."
    exit 1
fi
echo "drain_import_pair_gate: OK — pair present (1 + 2), ${#FIX[@]}/${#FIX[@]} drain fixtures compile, artifact carries use+Buffer."
exit 0
