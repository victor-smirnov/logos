#!/usr/bin/env bash
# plan_nodes_gate.sh LOGOSC REREAD_FIXTURE BATCH_FIXTURE HASHMAP_FIXTURE JOIN_FIXTURE
#
# EVERY MATERIALIZATION IS A NAMED NODE WITH A GROUND — ADR 0025 S2, §4.
#
# Before the node layer, a materialization in the emitted plane was a CONSEQUENCE
# of a boolean. A per-rel single-read proof made `stream=false` made
# `emit_prelude_oneshot` write a `Vec` and a drain loop, and the sentence
# explaining it hung off the boolean rather than off the thing that was built.
# (The boolean is gone as of S2j — `plan_insert_drains` inserts the node and
# `access_plan_decide_mode` recovers "reads it once" as the ABSENCE of one — so
# the lines this gate counts are now the only representation there is.) Two facts could not be stated at all,
# and this gate exists because both are now stated and neither has any other
# witness in the tree:
#
#   • A BUFFER BUILT WHERE THE PLAN STREAMS. A hash join reads its build side
#     exactly once — no drain node, `stream=true`, the trace says "streamed: the
#     producer is an iterator and the plan reads it once" — and then materializes
#     every one of those rows into a `HashMap`. The old flag says the opposite of
#     what the artifact does, and no test in this tree could see the difference:
#     `deem_join_base_streams` and `deem_join_step_streams` assert the ANSWER and
#     the SCAN shape, and the answer is identical whether the build side is a map,
#     a sorted vector, or a rescan.
#
#   • HOW MANY. A chain that names one source twice builds TWO indexes over it.
#     A per-rel flag reports one. The node list is a LIST for that reason, and
#     the count is asserted against the artifact below (this is not hypothetical:
#     the per-rel form was written first and `deem_join_step_reread`'s emitted
#     dump — `__hm1` AND `__hm2` over one drained `__rel_d` — is what refuted it).
#
# ⚠ THE ORACLE IS THE EMITTED ARTIFACT, NOT A SECOND READING OF THE PLAN. Every
# claim below is checked on BOTH channels and the two are COMPARED, not each
# asserted against a literal:
#
#   THE PLAN'S ACCOUNT   `[plan] <rel> -> drain|sort|arrange on <ground>  (why)`
#                        on stderr under LOGOS_TRACE_PLAN=1, plus the POSITIVE
#                        absence line `no materialization` — a plan that spoke
#                        only when it built something would leave "no line"
#                        meaning both "nothing was built" and "nobody looked".
#
#   THE ARTIFACT        the `--gen-dir` dump. A prelude buffer is `let mut __it_<r>`
#                        (the drain arm's iterator binding, emitted for a drain and
#                        for a sort alike — a sort drains and then permutes); a
#                        build side is `let mut __hm<k>` / `__hs<k>` / `__bt<k>`.
#
# and the comparison is COUNT EQUALITY per fixture: #(drain+sort nodes) ==
# #(`__it_` bindings), #(arrange nodes) == #(index declarations). A node layer
# that over-reports invents a materialization that never runs; one that
# under-reports is the silence this whole ADR is about. The literal numbers are
# asserted TOO, as floors that prove the channels spoke at all — but they are not
# the property.
#
# ⚠ SCOPE, STATED SO THE GREEN CANNOT BE READ WIDER THAN IT IS. The node layer
# covers the PER-REL plane: the prelude buffer per source and the join build side.
# It does NOT yet name (a) an aggregate's group-state vectors and their group-row
# permutation, which is S4's subject — measured here: `deem_batch_scan_drain`
# emits THREE `__ix0` permutations against TWO `sort` nodes, and the third is the
# aggregate's group-row one; (b) a derived rel's or an SCC member's total, which
# is not a source's drain. The count equalities above are over (a)'s `__it_`
# bindings, which the aggregate DOES share, so the aggregate's source drain is
# covered; its group state is not, and is not claimed to be.
#
# ⚠ AND THE EMITTED TEXT DID NOT MOVE. S2's node layer is derived from facts the
# planner already had and feeds nothing into the emitter — the byte-pin
# `logos_09_slice_scan_codegen` and the whole corpus are what hold that, not this
# gate.
#
# ⚠⚠ WHAT S2b DID TO THIS GATE'S INDEPENDENCE, WRITTEN DOWN RATHER THAN LEFT FOR
# THE NEXT READER TO ASSUME. S2b made `emit_prelude_oneshot` choose its arm from
# `prm.rel_node` — the node the plan wrote down — instead of from
# `rel_stream`/`rel_iter`. The prelude buffer and the drain/sort node therefore
# no longer have INDEPENDENT provenance, and the equality
#
#     #(drain+sort nodes) == #(`__it_` bindings)
#
# is, for the PER-REL prelude, now true by construction: both sides read one
# lookup. It is kept because it still fails on the ways the two can part —
# a node dropped past the list's capacity, an arrange node miscounted as a
# prelude one, a rel whose prelude arm is the container arm — but it is no longer
# an oracle for "the plan's account matches what is emitted" on that clause.
#
# THE TWO CLAUSES THAT STAYED INDEPENDENT, and which is which matters:
#   • #(arrange nodes) == #(index declarations) — the build phase is emitted by
#     `build_phase_frag` off the STRATEGY and reads no node; unchanged.
#   • the `__ix0` count vs the sort nodes — the permutation is still emitted by
#     `sort_perm_frag` and reads no node (the Sort node is S2 stage 3, not
#     landed). Unchanged, and it is what measures the aggregate's unnamed
#     group-row permutation above.
# The independent oracle for the prelude clause is now the CONTROL, not this
# gate: forcing the node in each direction and reading the artifact
# (`AD_NONE` on a drained rel ⇒ 6 and 2 `__it_` bindings vanish and the emitted
# code stops compiling; `MAT_DRAIN` on a streamed rel ⇒ 0 → 4 appear), restored
# to a byte-identical corpus between the two.
set -euo pipefail

LOGOSC="$1"
REREAD="$2"
BATCH="$3"
HASHMAP="$4"
JOIN="$5"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
fail=0

# Compile one fixture with the plan trace on and the emitted source kept.
compile() {
    local tag="$1" src="$2"
    if ! LOGOS_TRACE_PLAN=1 "$LOGOSC" "$src" --gen-dir "$TMPD/$tag" \
            -o "$TMPD/$tag.o" 2>"$TMPD/$tag.err"; then
        echo "FAIL: logosc failed on $src:"; cat "$TMPD/$tag.err"; exit 1
    fi
}

# The emitted user code — the query's own module dumps. `logos.gen.*` holds the
# family DEFINITIONS and must not be searched: a producer found there says
# nothing about what this query builds.
dumps() {
    shopt -s nullglob
    local d=("$TMPD/$1"/test.*.gen.logos)
    if [ "${#d[@]}" -lt 1 ]; then
        echo "FAIL [$1]: no test.*.gen.logos dump — the artifact side was not asserted" >&2
        return 1
    fi
    printf '%s\n' "${d[@]}"
}

# ⚠ NO PIPE INTO `wc`. Under `set -o pipefail` a legitimately-zero `grep` fails
# the whole command, and zero is an EXPECTED answer here more than once.
count_err() {  # tag ERE -> count of matching trace lines
    grep -Ec "$2" "$TMPD/$1.err" || true
}
count_gen() {  # tag ERE -> count of matching emitted lines
    local files
    files=$(dumps "$1") || { echo "-1"; return; }
    grep -Eh "$2" $files > "$TMPD/$1.hits" 2>/dev/null || true
    wc -l < "$TMPD/$1.hits"
}

eq() {  # tag what got want
    if [ "$2" != "$3" ]; then
        echo "FAIL [$1]: $4"
        echo "       got $2, want $3"
        fail=1
    fi
}

IT_RE='let mut __it_[a-z_0-9]+:'
IX_RE='let mut __(hm|hs|bt)[0-9]+:'
NODE_RE='^\[plan\] [a-z_0-9]+ -> (drain|sort|arrange) on '

# THE CROSS-CHANNEL COMPARISON, applied to every fixture: the plan's node count
# against the artifact's. This is the assertion; the per-fixture literals below
# are the floors that prove the channel was alive.
crosscheck() {  # tag
    local tag="$1" nbuf nit narr nix
    nbuf=$(( $(count_err "$tag" '^\[plan\] [a-z_0-9]+ -> (drain|sort) on ') ))
    nit=$(count_gen "$tag" "$IT_RE")
    narr=$(count_err "$tag" '^\[plan\] [a-z_0-9]+ -> arrange on ')
    nix=$(count_gen "$tag" "$IX_RE")
    eq "$tag" "$nbuf" "$nit" \
       "the plan's drain/sort nodes disagree with the prelude buffers the artifact builds"
    eq "$tag" "$narr" "$nix" \
       "the plan's arrange nodes disagree with the keyed indexes the artifact builds"
}

# NO SILENCE WHERE A MATERIALIZATION HAPPENS. Per plan-compile (the blocks are
# delimited by the `prepared plan` line), every rel that reports the
# `materialize` MODE must also carry a node line or the "already a buffer"
# ground. A mode line with neither is a materialization the plan performed and
# did not name — the exact defect S2 closes.
no_silence() {  # tag
    awk -v tag="$1" '
        /^\[plan\] [a-z_0-9]+ -> / {
            rel=$2; verb=$4;
            if (verb == "materialize") mat[rel]=1;
            else if (verb == "drain" || verb == "sort" || verb == "arrange") named[rel]=1;
            else if (verb == "no") { if ($0 ~ /already a buffer/) named[rel]=1; }
        }
        /-> prepared plan/ { flushblk(); }
        function flushblk(   r) {
            for (r in mat) if (!(r in named)) {
                printf("FAIL [%s]: rel %s reports `materialize` and names no node — a materialization with no ground\n", tag, r);
                bad=1;
            }
            delete mat; delete named;
        }
        # Two literal exits rather than one computed status: a status a reader
        # (or a lint) has to evaluate is the 8-bit ceiling waiting to happen.
        END { flushblk(); if (bad) { exit 1; } exit 0; }
    ' "$TMPD/$1.err" || fail=1
}

# ── 1. THE PAIR, ON ONE REL, IN ONE FIXTURE ────────────────────────────────
#
# `deem_join_step_reread` compiles two queries over the same source `d`:
#   • one names it ONCE — the plan STREAMS it and still ARRANGES it. The node
#     exists where the boolean says "read once, nothing materialized".
#   • one names it TWICE — the plan DRAINS it (`drained: second use`, ADR §4's
#     first ground verbatim) and then arranges it TWICE, once per step.
# Measured artifact: `__rel_d: DupIter` + one `__hm1` in the first, `__it_d` +
# `__rel_d: Vec` + `__hm1` + `__hm2` in the second.
compile reread "$REREAD"
eq reread "$(count_err reread '^\[plan\] d -> arrange on join build side')" 3 \
   "the three keyed indexes built over d are not three arrange nodes"
eq reread "$(count_err reread '^\[plan\] d -> drain on drained: second use')" 1 \
   "the twice-named source was not drained with ADR §4's first ground"
eq reread "$(count_err reread '^\[plan\] d -> sort on ')" 0 \
   "a sort node appeared in a query with no order by"
# THE ORTHOGONALITY, stated as an inequality no boolean can satisfy: more
# arrangements than materialize verdicts is only possible if a rel the plan
# STREAMS was built into an index.
n_arr=$(count_err reread '^\[plan\] d -> arrange on ')
n_mat=$(count_err reread '^\[plan\] d -> materialize')
n_str=$(count_err reread '^\[plan\] d -> stream')
if [ "$n_arr" -le "$n_mat" ] || [ "$n_str" -lt 1 ]; then
    echo "FAIL [reread]: the streamed-AND-arranged case is not witnessed"
    echo "       arrange $n_arr, materialize $n_mat, stream $n_str"
    echo "       (want arrange > materialize and at least one streamed read)"
    fail=1
fi
crosscheck reread
no_silence reread

# ── 2. THE SAME REL, NODE AND NO NODE ──────────────────────────────────────
#
# `deem_batch_scan_drain` runs four queries over one container `m`. One is read
# once end to end and builds NOTHING; one carries `order by` over a column the
# source is NOT sorted by and builds a SORT; one carries `order by … desc` over
# the column it IS sorted by and builds NOTHING, backwards; one AGGREGATES and
# — since ADR 0025 S5-D5 — also builds NOTHING, folding out of the batch pull.
# Same source, same producer, so a node layer that answered from the SOURCE
# rather than from the plan would show here, and "no materialization" is a
# POSITIVE line rather than a missing one.
#
# ⚠ THE FOUR VERDICTS ARE NOW THREE, AND THE LOST DISTINCTION IS REPLACED, NOT
# DROPPED. Before S5-D5 the four queries gave four different answers and the
# aggregate's was the DRAIN; now two of them answer "read once, nothing built"
# and the counting clause alone can no longer tell which query is which. The
# distinction moves to the ARTIFACT clause below, which names the emitted fn
# (`parity_sums_run`) and asserts the shape of ITS scan — a per-query oracle
# where the trace only has a per-rel one.
#
# ⚠ THE SORT COUNT WAS 2 AND IS NOW 1, MOVED BY ADR 0025 S3-desc AND RE-PINNED
# RATHER THAN RELAXED. `tail_desc` orders by `key` descending, and `key` is the
# column the family DECLARES its rows arrive sorted by, so its Sort node is
# gone — replaced by a backward leaf walk. The two clauses below therefore
# PARTITION what used to be one count: one Sort node and one reversed elision,
# summing to the two ordered queries that have always been there. Asserting only
# the reduced sort count would have left the other half unwitnessed, and a
# second elision appearing (or the sort silently returning) would then have been
# invisible here.
compile batch "$BATCH"
eq batch "$(count_err batch '^\[plan\] m -> sort on order by')" 1 \
   "the wrong-column ordered query did not name a Sort node with ADR §4's second ground"
eq batch "$(count_err batch '^\[plan\] m -> no materialization on ordered source, reversed')" 1 \
   "the descending query over the declared ordered column did not name the reversed-elision ground — an absence with no ground is the silence this gate exists to close"
# ⚠ THE AGGREGATE'S DRAIN COUNT WAS 1 AND IS NOW 0, MOVED BY ADR 0025 S5-D5 AND
# RE-AUTHORED RATHER THAN RELAXED. `parity_sums` is the PURE class
# (`aggr_group_frame_pure`) over a producer that hands out BATCHES, and
# `emit_aggregate` now routes its base loop through `batch_scan_frag` — so the
# fold reads `m` once, in place, and the Drain that stood in for the indexed
# walk is gone. The old clause asserted "the aggregate's re-read was NAMED";
# there is no re-read to name, and asserting a ground the compiler correctly no
# longer emits would have been a test pinning a defect.
#
# THE REPLACEMENT IS NOT THE SAME CLAUSE WITH A SMALLER NUMBER. Three assertions
# take its place and each closes a way the change could have been wrong:
#   (a) the drain is ZERO here — checked as an equality, so a drain RETURNING is
#       a red exactly as its disappearance was;
#   (b) the read-once ground is 2 — the new verdict is a POSITIVE line, which is
#       the property this whole gate is about (silence and "nothing built" must
#       stay distinguishable), and the count is what says the aggregate joined
#       that class rather than going quiet;
#   (c) THE ARTIFACT, which is the independent channel: `parity_sums_run` must
#       PULL BATCHES and must build NO Buffer. (b) alone is a second reading of
#       the plan; (c) is what the plan is a claim ABOUT. Had the plan stopped
#       draining while the emitter kept the indexed walk — the exact one-sided
#       change S4 measured and refused (`'…LeafWalk' has no method 'len'`) —
#       (b) would be green and (c) red.
#
# ⚠ AND `MG_REGROUP` DOES NOT GO UNWITNESSED, which the census gate checks in
# both directions corpus-wide: it survives on `deem_pushdown_all_shapes`
# (representative class) and on the PURE-over-a-ROW-producer arm S5-D5 adds. The
# second has NO CORPUS FIXTURE — the corpus's only two rel-registered aggregate
# sources are this one and that one — and was therefore measured out of tree
# rather than assumed: a pure aggregate over `MapSource::rel entry` (a row
# iterator) keeps its drain, states the ROWS-not-batches ground, compiles, and
# runs its 6-pull / 2-group oracle green. Recorded in the census, not pinned
# here, because pinning it means a new corpus fixture and this stage does not
# add one.
eq batch "$(count_err batch '^\[plan\] m -> drain on regrouped: aggregate')" 0 \
   "the pure-class aggregate over a batch producer drained its source — S5-D5 routes that base loop through batch_scan_frag, so a Drain here means the plan and the emitter have parted again"
eq batch "$(count_err batch '^\[plan\] m -> no materialization   \(no node: the plan reads this source once')" 2 \
   "the queries that materialize nothing do not SAY so — silence and 'nothing built' are again indistinguishable"
# (c) THE ARTIFACT SIDE of the same decision, on the emitted fn by name.
awk '/^pub fn parity_sums_run\(/ {f=1} f {print} f && /^}$/ {exit}' \
    $(dumps batch) > "$TMPD/batch.agg" || true
if [ ! -s "$TMPD/batch.agg" ]; then
    echo "FAIL [batch]: no emitted \`parity_sums_run\` in the dump — the artifact side of the aggregate's scan was not asserted at all"
    fail=1
else
    if ! grep -q "next_batch()" "$TMPD/batch.agg"; then
        echo "FAIL [batch]: emitted \`parity_sums_run\` does not pull batches — the plan says this source is read once in place, and the artifact still walks it some other way"
        fail=1
    fi
    if grep -q "Buffer<" "$TMPD/batch.agg"; then
        echo "FAIL [batch]: emitted \`parity_sums_run\` builds a Buffer — a materialization the plan no longer names"
        fail=1
    fi
fi
crosscheck batch
no_silence batch
# THE GROUND VOCABULARY IS NEVER EMPTY. A node whose ground is blank is the
# defect in its purest form: the materialization is visible, the justification is
# not. Asserted over every node line in this gate's four fixtures.
for tag in reread batch; do
    grep -E "$NODE_RE\$" "$TMPD/$tag.err" > "$TMPD/$tag.blank" 2>/dev/null || true
    if [ -s "$TMPD/$tag.blank" ]; then
        echo "FAIL [$tag]: a node line carries an EMPTY ground:"; cat "$TMPD/$tag.blank"
        fail=1
    fi
done

# ── 3. THE GROUND THAT DIES (ADR §4) ───────────────────────────────────────
#
# A producer returning a CONTAINER hands back a buffer already; the prelude's
# `Vec` binding is that value under a name, not a materialization the plan
# performed. ADR §4 retires this ground — and it retires by being SAID, not by
# going quiet, which is the whole difference between a decision and an omission.
# `HashMap` is the hand-written source that has it.
compile hashmap "$HASHMAP"
if [ "$(count_err hashmap '^\[plan\] m -> no materialization on already a buffer')" -lt 1 ]; then
    echo "FAIL [hashmap]: the container producer's 'no node' ground is not stated"
    fail=1
fi
eq hashmap "$(count_err hashmap "$NODE_RE")" 0 \
   "a node was inserted over a producer that hands back a container"
eq hashmap "$(count_gen hashmap "$IT_RE")" 0 \
   "the artifact drains a container producer — the 'already a buffer' ground is false here"
no_silence hashmap

# ── 4. A CONTAINER ARRANGED, BESIDE A STREAM THAT IS NOT ───────────────────
#
# The cross-domain join reads a Memoria family (`c`, streamed, no node) and a
# `HashMap` (`h`, a container — no prelude node — built into the join's index).
# Both absences and the one presence in ONE plan: this is where a node layer that
# derived nodes from `stream` alone would report a drain for `h` and nothing for
# the index it actually builds.
compile join "$JOIN"
eq join "$(count_err join '^\[plan\] h -> arrange on join build side')" 1 \
   "the join's build side over the hand-written container is not an Arrange node"
eq join "$(count_err join '^\[plan\] c -> no materialization   \(no node: the plan reads this source once')" 1 \
   "the streamed family source does not state that it builds nothing"
eq join "$(count_err join '^\[plan\] [a-z_0-9]+ -> (drain|sort) on ')" 0 \
   "a prelude buffer was claimed in a plan whose artifact builds none"
crosscheck join
no_silence join

if [ "$fail" -ne 0 ]; then
    echo "---- traces ----"
    for tag in reread batch hashmap join; do
        echo "== $tag"
        grep -E '^\[plan\] [a-z_0-9]+ -> (drain|sort|arrange|no materialization|materialize|stream)' \
            "$TMPD/$tag.err" || true
    done
    exit 1
fi
echo "OK: plan nodes — every materialization in the per-rel plane is a named node"
echo "    with a ground, the counts agree with the emitted artifact on all four"
echo "    fixtures, a rel the plan STREAMS is still recorded as arranged, and the"
echo "    two 'nothing was built' sentences are said rather than omitted."
exit 0
