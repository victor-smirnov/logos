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
# ── WHERE S2/S3 LEFT IT, AND WHY THE GOLDEN STILL SHOWS THE OLD SHAPE ───────
#
# The golden is UNCHANGED through S2a-h, S3a/S3b/S3f and S2j, and every one of
# those stages passed through this pin by the byte-comparable arm — the empty-
# diff one — not by the measured-equal arm. That is a claim about text, so it is
# stated with what it was measured against:
#
#   S3a  put `use logos.mem.stream;` into every spliced item (2089 lines,
#        45,958 bytes, corpus-wide). NONE of them is inside `slice_scan_run`:
#        the use decls are file-level in the dump and this gate extracts one
#        function, definition line to the first column-0 `}`.
#   S3b  changed the DRAIN node's landing (`Vec<R>` -> `Buffer<R>`, 10 sites in
#        6 fixtures, +60 bytes). `slice_scan_run` has no landing at all — a
#        heap slice is already materialized, which is exactly the ground S1
#        gave for leaving this arm alone — so the change could not reach it.
#        Not assumed: the pinned function contains no `__it_`, no `__rel_`, no
#        `Buffer<`, and this fixture's whole corpus-snapshot delta is its 3
#        file-level use lines (3 emitted items x 22 bytes = 66) — it is not one
#        of the 6 fixtures that carry the +60, and the corpus-wide arithmetic
#        closes exactly: 2089 x 22 + 60 == 46,018 == the measured snapshot
#        growth, leaving no unexplained byte for this function to hide in.
#   S3f  added a fixture and two gates; no emitter change.
#   S2j  THE INVERSION (`plan_mark_single_pass` -> `plan_insert_drains`): the
#        planner now inserts the `Drain`/`Sort` node where it used to record a
#        proof. Corpus TEXT-PRESERVING — `diff -rq` empty over 161 dumps /
#        6,856,383 bytes — so this function is unchanged by construction and not
#        merely unchanged in the pinned window.
#
# ⚠ AND S3d — "THE SLICE ARM DIES" — IS STILL NOT LANDED, BUT IT IS NO LONGER
# "NOT STARTED": at S2j it was BUILT, MEASURED, AND REVERTED, and the measured-
# equal arm was REFUSED BY THE MEASUREMENT rather than left unexamined. The two
# findings, both re-measurable from the repro pair recorded in the census:
#
#   (1) THE COLLAPSED SHAPE IS NOT MEASURED-EQUAL. `slice_scan_run` emitted
#       through a one-packet `SliceStream<R>` and disassembled against today's
#       byte-pinned shape, same fixture, same flags:
#         BASE (this golden)  59 instructions ·  242 bytes of .text · frame 0x78
#         COLLAPSED          128 instructions ·  528 bytes of .text · frame 0x108
#       +69 instructions, +286 bytes, frame more than doubled — for a source that
#       is ALREADY materialized. The ADR's "OR measured equal" arm is a licence
#       to re-golden when the emitted code is equivalent; this is a licence NOT
#       taken, because the measurement says it is not.
#   (2) AND THE SHAPE DOES NOT COMPILE IN THE EMITTER'S OWN SCOPING. The emitted
#       join-order branch puts the stream in an ARM scope and the sort's key
#       vector at FUNCTION scope, and the packet-borrowed key is then refused —
#       `'__bb0' does not live long enough … borrowed by '__ks'` (E0597) — while
#       the rows really live in the caller's `&[Row]`, which outlives everything.
#       ONE VARIABLE AT A TIME, re-measured on today's compiler: one arm compiles
#       (lt2, rc 0), TWO SIBLING ARMS of the same shape are refused (lt4, rc 1),
#       and the same two arms with the PRE-COLLAPSE indexed loop compile (lt5,
#       rc 0). So the refusal is the borrow-provenance over-refusal class, not
#       the design — the collapse was blocked on a COMPILER fix.
#
# ⚠ (2) IS CLOSED AS OF 2026-08-14 (S5-D4), AND (1) IS NOT. The over-refusal was
# rooted to an 8-line program with no stream in it and fixed in
# `src/compiler/borrow_check.cpp`: a §B6 borrow SOURCE reached through a
# reference-typed local is that local's own PROVENANCE, not the local — its
# storage is the pointee's, and the pointee outlives the block the reference was
# declared in. lt2/lt4/lt5 and the whole m/q/r probe grid are rc 0 now, with the
# refusal PRESERVED and re-aimed: where the reference points at a DYING LOCAL
# the E0597 stands and names that local (the probe pair is recorded in the
# census). THIS GOLDEN STILL DOES NOT MOVE, and the surviving reason is (1)
# alone — +69 instructions and +286 bytes of `.text` for a source that is
# ALREADY materialized is not "measured equal", and a checker fix does not
# change a codegen number. Two blockers became one, and the one that is left is
# a MEASUREMENT: the transition needs a re-measurement, not a re-argument.
#
# When S3d does land, this gate goes red twice (bytes + batch vocabulary) and
# both arms move together with the measurement recorded in the census.
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
