#!/usr/bin/env bash
# drain_read_once_pair_gate.sh LOGOSC PASS_DIR
#
# THE READ-ONCE DECISION, PINNED IN BOTH DIRECTIONS — ADR 0025 S3f.
#
# ── WHY A PAIR AND NOT AN ASSERTION ─────────────────────────────────────────
#
# "The plan reads this source once" is a REFUSAL when it is withdrawn: the rel
# is drained into a landing first. A gate that only checked the refusal is green
# on an emitter that drains EVERYTHING — the pessimal compiler passes every
# refusal test ever written. A gate that only checked the admission is green on
# an emitter that never drains, which is the miscompile the withdrawal exists to
# prevent (the second index build reads an iterator the first one spent, and the
# answer is silently empty). Neither half is an oracle alone, so both are here,
# read off ONE fixture where the queries differ in exactly one thing: how many
# times the query names the source.
#
#   ADMIT   `deem_join_step_reread::q_dup`      — `d` named ONCE.
#           Emitted: `let mut __rel_d: DupIter = dup_rows(d);` — the PRODUCER is
#           the binding, consumed where it stands inside the index build. No
#           landing, no `__it_`, no `Buffer<`.
#           Trace: `d -> stream`, `d -> no materialization`.
#
#   REFUSE  `deem_join_step_reread::q_selfstep` — `d` named TWICE in one chain.
#           Emitted: `let mut __it_d: DupIter = …` AND
#           `let mut __rdb_d: Buffer<(i64, i64)> = Buffer::<(i64, i64)>::new();`,
#           filled by `push`, read downstream through `as_slice()`.
#           Trace: `d -> materialize`, `d -> drain on drained: second use`.
#
# ⚠ THE LANDING'S SPELLING CHANGED IN ADR 0025 R-F AND IS RECORDED HERE BECAUSE
# THIS GATE ASKED FOR THAT — `__rel_d` -> `__rdb_d` (Drain) / `__rsb_<r>` (Sort),
# composed by `push_land_name` in `stdlib/mem/wql/access_plan.logos`. The
# ADMITTED half is untouched: `let mut __rel_d: DupIter` is `rel_ssrc`, the
# STREAMED producer's own binding, a different string from the landing that
# happened to read the same. The REFUSE assertions below check the landing BY
# TYPE (`: Buffer<T> = Buffer::<T>::new();`) and so did not move; only the
# failure-path diagnostic was re-aimed. The rename is pinned corpus-wide by
# `plan_ground_census_gate.sh` FACT N (drain 7 == `__rdb_` 7, sort 5 ==
# `__rsb_` 5, Buffer-typed `__rel_` == 0).
#
# The two functions come from the SAME source declaration in the SAME file, so a
# pass is evidence about the read-once decision and not about two unrelated
# queries that happen to differ.
#
# ── WHY THE BEHAVIOURAL FIXTURES CANNOT REPLACE THIS ────────────────────────
#
# `deem_join_step_reread` and `deem_drain_buffer_empty` both count the SOURCE's
# own `next()` calls, which is a strong oracle for "the source was read once" —
# but it cannot see WHERE the rows went. An emitter that kept a private `Vec`
# somewhere else, or that drained the admitted query too, produces identical
# pull counts and identical answers. The landing is an artifact fact, so it is
# read off the artifact.
#
# ⚠ MEASURED, NOT ARGUED: `deem_drain_buffer_empty` with its source named ONCE
# instead of twice — the admitted shape, no landing anywhere — was compiled and
# run, and reports the SAME `TICKS` (1 empty / 4 populated) as the drained
# version. Every runtime assertion in that fixture is green under an emitter
# that stopped draining. This gate is the only thing in the tree that is not.
#
# ── THE THIRD WITNESS, AND WHY IT IS NOT A THIRD COPY ───────────────────────
#
# `deem_drain_buffer_empty` is the behavioural fixture for an EMPTY landing (the
# drain arm's `Buffer::<R>::new()` built and never pushed to). Its whole claim
# rests on that query actually draining — a fact its own runtime assertions
# cannot establish, because 0 rows in / 0 rows out is what a drain-free emitter
# also produces. That fixture's `TICKS == 1` says the source was consulted once;
# THIS gate says the rows had a landing to go to. Together they are the pair the
# empty case needs.
#
# ── WHAT REDS AND WHAT THAT MEANS ───────────────────────────────────────────
#
# PROVED TO BITE — the probe pair below was run, ONE perturbation at a time, on
# the fixture (no stdlib rebuild involved), each restored to a byte-identical
# source (md5 b34aa8a0f611dd2236ce62f501a71bad) with a GREEN checkpoint between
# and after:
#
#   P1  `q_dup` given a SECOND naming of `d` (admit -> refuse shape):
#       7 failures — all three ADMIT absence clauses (`Buffer<`, `__it_d`,
#       `as_slice`), BOTH admit positives (the producer binding and the in-place
#       `next()` pull), and both admit trace grounds.
#   P2  `q_selfstep` reduced to ONE naming (refuse -> admit shape):
#       4 failures — `__it_`, the `Buffer<(i64, i64)>` landing, `as_slice`, and
#       the `drain on drained: second use` ground.
#
# ⚠ AND ONE CLAUSE DID NOT FIRE IN P2, WHICH IS RECORDED RATHER THAN HIDDEN: the
# `.push(` check survives the admit shape, because a streamed join pushes rows
# into its hash bucket too. It is kept — it still catches a landing that is built
# and never filled — but it is NOT part of what separates refuse from admit, and
# a reader must not count it as a fourth independent clause.
#
# The `Buffer<` spelling itself is a pin over S3b and could not pass on any tree
# before it: `Buffer<` first enters the corpus AT S3b, whose entire corpus-wide
# delta is those 10 sites / +60 bytes. Pre-S3b the same landings spell `Vec<`.
#
# The ADMIT half is checked POSITIVELY as well as negatively (the producer call
# and the in-place `next()` loop must be there), so a function that stopped being
# emitted at all cannot pass by having no `Buffer<` in it — P1's two positive
# failures are that clause firing.
#
# ⚠ IT RETIRES WITH ITS SUBJECT. If the Drain node's landing stops being a
# `Buffer` — S3d makes a slice a one-packet stream, S4/S5 may move the landing
# again — FACT R2 goes red and the instruction is to RE-AIM the gate at the new
# landing spelling WITH that change, recorded in the census, not to weaken the
# assert.
#
# EXIT: 0 clean · 1 a claim failed · 2 the gate could not look (never clean).
set -uo pipefail

LOGOSC=${1:?usage: drain_read_once_pair_gate.sh <logosc> <pass dir>}
PASSD=${2:?usage: drain_read_once_pair_gate.sh <logosc> <pass dir>}

export LC_ALL=C
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fails=0
note() { echo "FAIL: $*"; fails=$((fails+1)); }

# ── compile both fixtures, in parallel, one output file set per probe ────────
FIX=(deem_join_step_reread deem_drain_buffer_empty)
probe() {
    local b="$1" D="$2" LOGOSC="$3" PASSD="$4"
    LOGOS_TRACE_PLAN=1 "$LOGOSC" "$PASSD/$b.logos" --gen-dir "$D/$b.gen" \
        -o "$D/$b.o" > "$D/$b.out" 2> "$D/$b.err"
    echo "$?" > "$D/$b.rc"
}
export -f probe
printf '%s\0' "${FIX[@]}" \
  | xargs -0 -P "$(nproc)" -I{} bash -c 'probe "$@"' _ {} "$TMPD" "$LOGOSC" "$PASSD"

shopt -s nullglob
RCS=("$TMPD"/*.rc)
if [ "${#RCS[@]}" -ne "${#FIX[@]}" ]; then
    echo "FAIL(2): ${#RCS[@]} rc files for ${#FIX[@]} probes — probes were lost,"
    echo "         so a green would be an absence of evidence."
    exit 2
fi
for b in "${FIX[@]}"; do
    r=$(cat "$TMPD/$b.rc" 2>/dev/null || echo missing)
    if [ "$r" != "0" ]; then
        echo "FAIL(2): $b does not compile (rc=$r):"
        grep -m2 -i 'error' "$TMPD/$b.out" "$TMPD/$b.err" 2>/dev/null | head -2
        exit 2
    fi
    DUMPS=("$TMPD/$b.gen"/*.gen.logos)
    if [ "${#DUMPS[@]}" -lt 1 ]; then
        echo "FAIL(2): no --gen-dir dump for $b; there is nothing to read."
        exit 2
    fi
    cat "${DUMPS[@]}" > "$TMPD/$b.txt"
done

# Extract one emitted fn: definition line to the first column-0 `}`.
extract() {                     # extract <dumpfile> <fnname> <outfile>
    local src="$1" fn="$2" out="$3"
    local n
    n=$(grep -c "^pub fn ${fn}(" "$src")
    if [ "$n" != "1" ]; then
        note "expected exactly ONE definition of ${fn} in the dump, found ${n}.
      With two, \"the emitted query\" is ambiguous and every clause below is
      reading an arbitrary one of them."
        : > "$out"
        return 1
    fi
    awk -v fn="^pub fn ${fn}\\\\(" '$0 ~ fn {f=1} f {print} f && /^}$/ {exit}' \
        "$src" > "$out"
}

RR=$TMPD/deem_join_step_reread.txt
EM=$TMPD/deem_drain_buffer_empty.txt
extract "$RR" q_dup_run       "$TMPD/admit"    || true
extract "$RR" q_selfstep_run  "$TMPD/refuse"   || true
extract "$EM" selfstep_run    "$TMPD/refuse2"  || true

# ── FACT A1 — THE ADMITTED QUERY HAS NO LANDING ─────────────────────────────
if [ -s "$TMPD/admit" ]; then
    # ⚠ ADR 0025 R-H (b′) — `Buffer<` LEFT THIS LIST AND WAS REPLACED, NOT
    # DROPPED. (b′) made the QUERY-OUTPUT landing a Buffer: `q_dup_run` opens
    # `let mut __out: Buffer<(i64, i64)>` and returns `__out.into_vec()`, so a
    # bare `Buffer<` is now present in EVERY emitted query and says nothing
    # about a drain. The clause is about the DRAIN landing, which is minted
    # `let mut __rdb_<rel>: Buffer<…>` (FACT K, plan_ground_census_gate.sh), so
    # that is the token now — same direction, same floor, narrower subject.
    # The FACT R1/R2 halves below still read the full `: Buffer<ty> =` spelling.
    for tok in 'let mut __rdb_' '__it_d' '.as_slice()'; do
        if grep -qF -- "$tok" "$TMPD/admit"; then
            note "q_dup_run — the ADMITTED half — carries '$tok'. The source is
      named ONCE, so the plan proved it read-once and the emitter must consume
      the producer where it stands. A landing here is the pessimal direction:
      correct answers, a materialization nobody asked for, and every refusal
      test in the tree still green."
        fi
    done
    # POSITIVE: the admitted shape is present, so "no Buffer" cannot be green by
    # the function having disappeared.
    grep -qF 'let mut __rel_d: DupIter = dup_rows(d);' "$TMPD/admit" \
      || note "q_dup_run does not bind the PRODUCER itself
      (\`let mut __rel_d: DupIter = dup_rows(d);\`). Without this line the
      absence checks above assert nothing — an empty function passes them all."
    grep -qF '(__rel_d).next()' "$TMPD/admit" \
      || note "q_dup_run has no in-place \`(__rel_d).next()\` pull — the streamed
      scan is what 'read once, consumed where it stands' MEANS in the artifact."
else
    note "q_dup_run was not extracted; the ADMIT half asked nothing."
fi

# ── FACT R1/R2 — THE REFUSED QUERIES LAND IN A `Buffer` ─────────────────────
check_refuse() {                # check_refuse <file> <label> <rowty>
    local f="$1" lab="$2" ty="$3"
    if [ ! -s "$f" ]; then
        note "$lab was not extracted; a REFUSE half asked nothing."
        return
    fi
    grep -qF "let mut __it_" "$f" \
      || note "$lab has no \`__it_\` producer binding — the drain arm emits the
      producer and the landing as TWO bindings; one of them is missing."
    # ⚠ ADR 0025 R-H (b′) — THE MATCH IS RESTRICTED TO `__rdb_` LANDINGS FIRST.
    # (b′) made the query-output landing a Buffer of the SAME row type, so the
    # bare `: Buffer<ty> = Buffer::<ty>::new();` text now also matches
    # `let mut __out: Buffer<(i64, i64)> = …` and this clause would pass
    # VACUOUSLY on a tree where the drain landing disappeared. Narrowed to the
    # drain landing's own name; the spelling asserted after the narrowing is
    # byte-for-byte the one this clause always asserted.
    # (NO PIPE INTO `grep -q`: the reader dies at the first match, the writer
    # takes SIGPIPE and `pipefail` hands the pipeline that status — the recorded
    # gate-lie form. The narrowing is a variable, the test is a `case`.)
    local rdbl; rdbl=$(grep -oE 'let mut __rdb_[a-z_0-9]+: Buffer<.*' "$f" 2>/dev/null || true)
    case "$rdbl" in
      *": Buffer<${ty}> = Buffer::<${ty}>::new();"*) ;;
      *) note "$lab does not land in \`Buffer<${ty}>\`. Either the read-once
      withdrawal stopped happening for a source named twice in one chain (the
      miscompile: the second index build reads a spent iterator and the answer
      goes silently empty), or the Drain node's landing changed spelling — in
      which case RE-AIM this gate WITH that change and record it in the census.
      Emitted landings found: $(grep -oE 'let mut __(rdb|rsb|rel)_[a-z_0-9]+: [A-Za-z]+<' "$f" | tr '\n' ' ')" ;;
    esac
    grep -qF '.push(' "$f" \
      || note "$lab never \`push\`es into the landing — the ACCUMULATOR spelling
      is what S3b measured (13.8x cheaper in emitted text than wrapping a Vec);
      a landing nothing fills is an empty relation."
    grep -qF '.as_slice()' "$f" \
      || note "$lab never reads the landing back through \`as_slice()\`."
}
check_refuse "$TMPD/refuse"  "q_selfstep_run (deem_join_step_reread)" '(i64, i64)'
check_refuse "$TMPD/refuse2" "selfstep_run (deem_drain_buffer_empty)" '(i64, i64)'

# ── FACT T — THE TRACE SAYS THE SAME THING THE ARTIFACT DOES ────────────────
#
# The artifact halves above could both be true while the PLAN's own account of
# why is missing or says something else — that is the "a materialization named
# and not explained" defect the ground census exists for, asked here per rel.
E1=$TMPD/deem_join_step_reread.err
E2=$TMPD/deem_drain_buffer_empty.err
grep -qF '[plan] d -> stream' "$E1" \
  || note "no \`[plan] d -> stream\` for the admitted query — the artifact
      consumes the producer in place but the plan never said it proved read-once."
grep -qF '[plan] d -> no materialization' "$E1" \
  || note "no \`[plan] d -> no materialization\` ground for the admitted query."
grep -qF '[plan] d -> drain on drained: second use' "$E1" \
  || note "no \`[plan] d -> drain on drained: second use\` for q_selfstep — the
      landing exists in the artifact with no ground naming why."
grep -qF '[plan] s -> drain on drained: second use' "$E2" \
  || note "no \`[plan] s -> drain on drained: second use\` for
      deem_drain_buffer_empty — that fixture's empty-landing claim is about a
      drain, and this is the only thing that says it drains."

if [ "$fails" -gt 0 ]; then
    echo "drain_read_once_pair_gate: $fails failure(s)."
    exit 1
fi
echo "drain_read_once_pair_gate: OK — admit (no landing, producer consumed in place)"
echo "    and refuse (Buffer landing, pushed and read back) on one source, x2 witnesses,"
echo "    each with the plan ground that explains it."
exit 0
