#!/usr/bin/env bash
# ctr_access_path_gate.sh LOGOSC SIZE_FIXTURE HASHMAP_FIXTURE VECTOR_FIXTURE
#
# WHICH ROWS THE CONTAINER WAS ASKED FOR — the property every pushdown fixture in
# this tree was blind to.
#
# The narrowing itself is landed and was RE-MEASURED on 2026-08-10 (ADR 0024 S6):
# a generated ordered-map family declares `op entry.key ge = __ctr_from_… exact`
# and the query lands by `seek_key`, one descent; the positional family declares
# `op row.pos ge` and lands by `seek`; `HashMap`, hand-written and knowing no
# order, declares `op entry.key eq = hashmap_at exact` and nothing else. What was
# missing is a SENSOR. Every fixture over those paths asserted only the ANSWER —
# and the answer is IDENTICAL under a full scan, because a plan that fails to
# narrow keeps the query's own filter and returns exactly the same rows. So a
# pushdown could degrade to a scan, silently, and the whole corpus would stay
# green. That is what this gate removes.
#
# ⚠ AND IT CANNOT BE A TIMING TEST. This box is shared; a duration measured here
# is not evidence about anything. The property asserted is therefore the SHAPE of
# the demand — which producer was called, with which bound, and from which
# position — which is a fact about the emitted artifact, not about the machine.
# Two sides, because either alone can lie:
#
#   THE PLAN'S ACCOUNT   `[plan] <rel> -> <fn> [<cardinality>] on <col>  (<why>)`
#                        on stderr under LOGOS_TRACE_PLAN=1. It says what was
#                        DECIDED, including the grounds — and `op = -1` prints as
#                        a POSITIVE `scan` line with its own antecedent, so
#                        "declared no covering operation" is distinguishable from
#                        "the query had no filter to narrow to". A silent
#                        degradation has to write one of those two sentences, and
#                        both are asserted.
#
#   THE EMITTED CALL     the `--gen-dir` dump. It says what will actually RUN. A
#                        plan that recorded a narrowing while the emitter called
#                        `__ctr_rows_` would pass the first half; the absence of
#                        the full-scan producer for that rel is asserted too, and
#                        the bound LITERAL is asserted with it, because "narrowed"
#                        with the wrong landing is not narrowed.
#
# ⚠ THE RETIRED FILTER IS ASSERTED THE SAME WAY, and in BOTH directions. When the
# access is exact for the whole demand the planner drops the query's `where` — so
# the bound literal must appear EXACTLY ONCE in the emitted fn, inside the call.
# A second occurrence is a residual comparison, which means the planner did not
# believe its own exactness. And a DECLARED SCAN must KEEP its filter: `big` in
# the vector fixture narrows on nothing and its `> 10u64` must still be there.
#
# ⚠ NEUTRALITY (ADR 0024 S6 acceptance criterion). A declaration a FACTORY
# generates and one a HUMAN writes must be the same object. That is asserted
# here rather than asserted about: ONE line grammar is applied to all three
# fixtures — Canon's generated ordered-map family, Canon's generated positional
# family, and `HashMap`, whose `impl MapSource … { rel/op/size }` is written out
# by hand in the standard library. The planner cannot tell them apart, and the
# gate would notice if it could, because the same regex has to match all three.
# ── PROVED TO BITE, TWICE, WHERE THE THING LIVES (2026-08-10) ───────────────
#
# Not by breaking the gate — by deleting one declared operation from the source
# that declares it, rebuilding the stdlib, and measuring. Both controls were
# restored to a byte-empty `git diff` and the restore re-measured green before
# the next one, because a control stacked on an un-restored predecessor measures
# the accumulation.
#
#   CONTROL 1 — `op entry.key ge = #fromfn exact;` deleted from the
#   `OrderedMapSource` impl this repo's factory emits
#   (stdlib/lcm/canon/container_item.logos). PREDICTED: the plan falls to
#   `scan`, this gate exits 1, and every pre-existing test stays green.
#   MEASURED: exactly that — `[plan] m -> scan [every row] (the source declares
#   no operation covering that comparison on that column)`, the emitted fn
#   calling `__ctr_rows_Hs…(m)`, four assertions red here, and the hashmap and
#   vector halves untouched (the gate names the rel that lost the operation).
#
#   CONTROL 2 — `op entry.key eq = hashmap_at exact;` deleted from the
#   hand-written `impl MapSource` in
#   stdlib/mem/collections/hashmap/hashmap.logos. MEASURED: five assertions red,
#   `hashmap_rows(m)` emitted TWICE (want 1), and the scan's ground changed from
#   "no operation covering that comparison on that column" to "no access
#   operations" — a distinction this gate asserts and no other test reads.
#
# ⚠ HOW MUCH WORK THE BROKEN CODE HAD DONE BEFORE EACH CATCH: ALL OF IT. Under
# both controls the whole stdlib built (rc=0), the fixtures compiled, linked,
# RAN, and returned the right rows — `logos_02_semantic_core_pass_deem_source_size`,
# `logos_02_semantic_core_pass_container_item_e2e`,
# `logos_02_semantic_core_pass_deem_hashmap_source` and `logos_09_plan_size_asked`
# all PASSED with the pushdown deleted. That is the measurement, not a remark:
# before this gate, deleting a container's access declaration was invisible to
# the entire suite.
set -euo pipefail

LOGOSC="$1"
SIZE_FIXTURE="$2"
HASHMAP_FIXTURE="$3"
VECTOR_FIXTURE="$4"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fail=0

# Compile one fixture with the plan trace on and the emitted source kept.
# $1 = tag, $2 = fixture path. Leaves $TMPD/<tag>.err and $TMPD/<tag>/gen.
compile() {
    local tag="$1" src="$2"
    if ! LOGOS_TRACE_PLAN=1 "$LOGOSC" "$src" --gen-dir "$TMPD/$tag" \
            -o "$TMPD/$tag.o" 2>"$TMPD/$tag.err"; then
        echo "FAIL: logosc failed on $src:"; cat "$TMPD/$tag.err"; exit 1
    fi
}

# A trace line that must be present. $1 = tag, $2 = ERE, $3 = what it means.
want_plan() {
    if ! grep -Eq "$2" "$TMPD/$1.err"; then
        echo "FAIL [$1]: $3"
        echo "       no plan line matching: $2"
        fail=1
    fi
}

# The emitted user code — every `test.*.gen.logos` dump, which is the query's own
# module. The family's dumps are `logos.gen.*` and hold the DEFINITIONS of these
# producers, so they must not be searched: finding `__ctr_rows_` there proves
# nothing about what the query calls.
dumps() {
    shopt -s nullglob
    local d=("$TMPD/$1"/test.*.gen.logos)
    if [ "${#d[@]}" -lt 1 ]; then
        echo "FAIL [$1]: no test.*.gen.logos dump — the emitted side was not asserted" >&2
        return 1
    fi
    printf '%s\n' "${d[@]}"
}

# $1 = tag, $2 = ERE over emitted lines, $3 = expected count, $4 = what it means.
#
# ⚠ NO PIPE. `grep | wc` under `set -o pipefail` fails the whole command when the
# count is legitimately zero, and zero is an EXPECTED answer here (the full-scan
# producer must be absent). The hits go to a file and the file is counted.
want_emit() {
    local tag="$1" re="$2" want="$3" why="$4"
    local files n
    if ! files=$(dumps "$tag"); then fail=1; return; fi
    grep -EnH "$re" $files > "$TMPD/$tag.hits" || true
    n=$(wc -l < "$TMPD/$tag.hits")
    if [ "$n" -ne "$want" ]; then
        echo "FAIL [$tag]: $why"
        echo "       '$re' occurs $n times in the emitted fn (want $want):"
        cat "$TMPD/$tag.hits"
        fail=1
    fi
}

# ── 1. A FACTORY-GENERATED ORDERED-MAP FAMILY, narrowed on the KEY ──────────
#
# `from m e where e.key >= 5` — the family declares `op entry.key ge` exact, so
# the plan lands by one descent and the filter is retired.
compile size "$SIZE_FIXTURE"
want_plan size \
    '^\[plan\] m -> __ctr_from_[A-Za-z0-9_]+ \[a range\] on key   \(an operation EXACT for that comparison' \
    "the key range did not reach the container as a range access"
want_emit size '= __ctr_from_[A-Za-z0-9_]+\(m, 5u64\);' 1 \
    "the emitted fn does not land the walk on the declared bound"
want_emit size '__ctr_rows_[A-Za-z0-9_]+\(m\)' 0 \
    "the emitted fn still calls the FULL-SCAN producer for the narrowed rel"
# The retired filter: the bound appears once, in the call, and nowhere else.
want_emit size '5u64' 1 \
    "the bound occurs outside the access call — the exact access did not retire the filter"
# The hand-written source in the same fixture declares a rel and no operation,
# and its query has no filter at all. Its scan is a DIFFERENT sentence from a
# scan forced by a missing operation, and the difference is the point.
want_plan size \
    '^\[plan\] s -> scan \[every row\]   \(the query asks for every row — no filter to narrow to\)' \
    "the unfiltered rel's scan lost its antecedent"

# ── 2. A HAND-WRITTEN STANDARD-LIBRARY MAP: one probe, one declared scan ────
#
# `HashMap` answers `==` and has no order. Both queries in the fixture return
# their rows either way; only these lines can tell the two paths apart.
compile hashmap "$HASHMAP_FIXTURE"
want_plan hashmap \
    '^\[plan\] m -> hashmap_at \[one key\] on key   \(an operation EXACT for that comparison' \
    "the equality demand did not reach the hash map as a point probe"
want_plan hashmap \
    '^\[plan\] m -> scan \[every row\]   \(the source declares no operation covering that comparison on that column\)' \
    "the ordered demand a hash map cannot answer was not recorded as a declared scan"
want_emit hashmap '= hashmap_at\(m, 2i64\);' 1 \
    "the emitted fn does not probe the hash map at the declared key"
want_emit hashmap '= hashmap_rows\(m\);' 1 \
    "the full-scan producer is called for a number of rels other than the one that declared no covering operation"
want_emit hashmap '2i64' 1 \
    "the probed key occurs outside the probe — the exact access did not retire the filter"

# ── 3. A FACTORY-GENERATED POSITIONAL FAMILY, narrowed on the POSITION ──────
#
# The vector family's `pos` is a column of the hub relation, and the family
# declares `op row.pos ge`. The same fixture also queries a column NO operation
# covers, so both halves are measured over ONE container.
compile vector "$VECTOR_FIXTURE"
want_plan vector \
    '^\[plan\] v -> __ctr_from_[A-Za-z0-9_]+ \[a range\] on pos   \(an operation EXACT for that comparison' \
    "the position range did not reach the container as a range access"
want_plan vector \
    '^\[plan\] v -> scan \[every row\]   \(the source declares no operation covering that comparison on that column\)' \
    "the element filter, which no operation covers, was not recorded as a declared scan"
want_emit vector '= __ctr_from_[A-Za-z0-9_]+\(v, 2i64\);' 1 \
    "the emitted fn does not land the positional walk on the declared bound"
want_emit vector '= __ctr_rows_[A-Za-z0-9_]+\(v\);' 1 \
    "the full-scan producer is called for a number of rels other than the one that declared no covering operation"
# The other direction: a scan KEEPS the filter it could not push down. A plan
# that dropped it would be unsound, and this is the cheap place to see it.
want_emit vector '> 10u64' 1 \
    "the scanned query lost the filter it could not push down"

# ── 4. NEUTRALITY, MECHANIZED ───────────────────────────────────────────────
#
# One grammar, three provenances: two families Canon's factory emits and one
# `impl MapSource` a human wrote in `stdlib/mem/collections/hashmap/`. Each must
# contribute exactly one narrowed access, matched by the SAME expression. If the
# planner ever learned to treat a generated declaration differently from a
# written one, this is the assertion that would notice.
NEUTRAL='^\[plan\] [a-z_]+ -> [A-Za-z0-9_]+ \[(one key|a range)\] on [a-z_]+   \(an operation EXACT for that comparison'
for tag in size hashmap vector; do
    grep -E "$NEUTRAL" "$TMPD/$tag.err" > "$TMPD/$tag.neutral" || true
    n=$(wc -l < "$TMPD/$tag.neutral")
    if [ "$n" -ne 1 ]; then
        echo "FAIL [$tag]: neutrality — one narrowed access expected, the common grammar matched $n"
        grep -E '^\[plan\]' "$TMPD/$tag.err" || true
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "---- traces ----"
    for tag in size hashmap vector; do
        echo "== $tag"; grep '^\[plan\]' "$TMPD/$tag.err" || true
    done
    exit 1
fi
exit 0
