#!/usr/bin/env bash
# slice_scan_codegen_gate.sh LOGOSC FIXTURE GOLDEN
#
# ADR 0025 S1's SLICE GATE: "slice-source codegen byte-comparable OR measured
# equal". This is the byte-comparable half, and it exists because S1 could not
# run it.
#
# WHAT WENT WRONG WITHOUT IT. S1 collapsed the emitter's scan branches for
# STREAMED sources onto §1's one shape and left the SLICE arm alone, with a
# ground (a heap slice has no packet producer; minting one is `Drain`/`Buffer`,
# which is S2). The evidence offered for "the arm is untouched" was a `git diff`
# showing 96 insertions and 0 deletions in `rexpr_walk.logos`. That is an
# observation about ONE commit. Nothing in the tree could have told the next
# commit apart from that one: every slice fixture asserts the query's ANSWER,
# and the answer is identical under any scan that visits the same rows. So the
# emitted slice loop could have changed shape, grown a materialization, or lost
# its filter placement, and 147 green fixtures would have said nothing.
#
# WHAT IT ASSERTS. The `*_run` function emitted for `pass/wql_slice_scan_shape`
# — the whole function, byte for byte, against a checked-in golden. Not a regex
# over it: a regex pins the tokens somebody thought of, and the defect class here
# is "something else moved".
#
# ⚠ AND THE GOLDEN IS CHECKED FOR BEING AN ASSERTION AT ALL. A golden that is
# empty, truncated, or that lost the loop matches a compiler that emits nothing
# — the same defect `run_test.sh` refuses for an empty `.expected` and
# `corpus_registration_gate.sh` refuses for a fixture nothing runs. So the
# golden must be non-trivial (a floor on its size), must contain the source loop
# and the `where` predicate, and — the S1 state, written down rather than
# assumed — must carry NONE of §1's batch vocabulary.
#
# ⚠ THE LAST CLAUSE IS WHAT S2 HAS TO ARGUE WITH, WHICH IS THE POINT. When the
# slice arm rides §1's shape, this gate goes red twice: the bytes differ and the
# batch vocabulary appears. Neither is a failure of the gate. The ADR's "OR
# measured equal" arm is then the deliberate move — re-golden and record the
# measurement that the emitted code is equivalent — and it costs a sentence in
# the census instead of happening silently. Deleting the clause without that
# measurement is the thing this refuses to let be quiet.
#
# EXIT: 0 clean · 1 a claim failed (the message names it) · 2 the gate could not
# look (missing input, compile failure) — never reported as clean.
set -euo pipefail

LOGOSC="${1:?logosc path}"
FIXTURE="${2:?fixture .logos}"
GOLDEN="${3:?golden path}"

FN=slice_scan_run          # the emitted fn this gate is about

for f in "$LOGOSC" "$FIXTURE" "$GOLDEN"; do
    if [ ! -e "$f" ]; then
        echo "FAIL(2): missing input: $f"
        exit 2
    fi
done

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if ! "$LOGOSC" "$FIXTURE" --gen-dir "$TMPD/gen" -o "$TMPD/out.o" > "$TMPD/log" 2>&1; then
    echo "FAIL(2): logosc failed on $FIXTURE:"
    cat "$TMPD/log"
    exit 2
fi

# ⚠ `nullglob` + an array, NOT `ls <glob> | wc -l`: with no match `ls` exits 2
# and under `set -euo pipefail` the command substitution takes the gate down
# having printed nothing — the one case it exists to report.
shopt -s nullglob
DUMPS=("$TMPD"/gen/*.gen.logos)
if [ "${#DUMPS[@]}" -lt 1 ]; then
    echo "FAIL(2): --gen-dir produced no dump; there is nothing to compare."
    ls -la "$TMPD/gen" 2>&1 || true
    exit 2
fi
cat "${DUMPS[@]}" > "$TMPD/all.txt"

# Extract the function: from its definition line to the first column-0 `}`.
# The emitter writes one definition per name, and the assertion below checks
# that — two definitions would make "the emitted scan" ambiguous.
DEFS=$(grep -c "^pub fn ${FN}(" "$TMPD/all.txt" || true)
if [ "$DEFS" != 1 ]; then
    echo "FAIL(1): expected exactly one definition of ${FN}, found ${DEFS}."
    echo "         dumps: ${DUMPS[*]}"
    exit 1
fi
awk -v fn="^pub fn ${FN}\\\\(" '$0 ~ fn {f=1} f {print} f && /^}$/ {exit}' \
    "$TMPD/all.txt" > "$TMPD/actual"

# ── the golden is an assertion, not a placeholder ───────────────────────────
GLINES=$(wc -l < "$GOLDEN")
if [ "$GLINES" -lt 8 ]; then
    echo "FAIL(1): $GOLDEN has $GLINES lines — too small to be the emitted scan."
    echo "         A truncated golden matches a compiler that emits a stub."
    exit 1
fi
# The two facts that make it the SLICE scan. `grep -q` is used on a FILE, never
# at the tail of a pipe: `cmd | grep -q` exits at the first match, `cmd` dies of
# SIGPIPE, and `pipefail` hands the pipeline that status.
if ! grep -q "while (__i < (rows).len())" "$GOLDEN"; then
    echo "FAIL(1): $GOLDEN does not contain the indexed slice loop"
    echo "         \`while (__i < (rows).len())\` — it is not pinning a slice scan."
    exit 1
fi
if ! grep -q "r.k >= lo" "$GOLDEN"; then
    echo "FAIL(1): $GOLDEN does not contain the query's \`where\` predicate;"
    echo "         a scan that lost its filter would compare equal to it."
    exit 1
fi
# The S1 state: this arm carries none of §1's batch vocabulary. See the header —
# S2 changes this clause deliberately, with the ADR's measured-equality arm.
for tok in 'next_batch(' '_at(__b' '__bn0' '__brow'; do
    if grep -qF -- "$tok" "$GOLDEN"; then
        echo "FAIL(1): $GOLDEN carries the batch-scan token '$tok'."
        echo "         Either the slice arm now rides ADR 0025 §1's shape — in which"
        echo "         case this clause and the golden are updated TOGETHER, with the"
        echo "         'measured equal' arm recorded in the census — or the golden was"
        echo "         regenerated from the wrong function."
        exit 1
    fi
done

# ── the comparison ──────────────────────────────────────────────────────────
if ! diff -u "$GOLDEN" "$TMPD/actual" > "$TMPD/diff"; then
    echo "FAIL(1): emitted ${FN} differs from $GOLDEN"
    echo "         (ADR 0025 S1 gate: slice-source codegen byte-comparable)"
    cat "$TMPD/diff"
    echo
    echo "If the change is intended, ADR 0025 S1 offers exactly two moves:"
    echo "  * byte-comparable — it should not have changed; fix the emitter; or"
    echo "  * measured equal  — re-golden AND record the measurement showing the"
    echo "                      new code is equivalent, in"
    echo "                      docs/deem-interpreter-deletion-census.md."
    exit 1
fi

echo "OK: emitted ${FN} is byte-identical to the golden ($GLINES lines,"
echo "    md5 $(md5sum < "$GOLDEN" | cut -d' ' -f1)); slice arm carries no batch shape."
exit 0
