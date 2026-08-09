#!/usr/bin/env bash
# abi-check — the minor-bump gate. Five checks:
#
#   0. BUILD AGE   — the stdlib archives must not predate logosc. A spec
#                    regenerated from archives older than the compiler describes
#                    the PREVIOUS surface, and every check below would then be
#                    answering about the wrong program.
#   1. FRESHNESS   — regenerate the spec from the built stdlib and require the
#                    committed abi/logos.abi to equal it. Checked against the
#                    BUILD, not against git: `git diff` on the spec only asks
#                    "did you edit the file", which is trivially clean for
#                    anyone who forgot to regenerate — the gate then passed
#                    without ever looking at the change (observed repeatedly,
#                    and the reason this script regenerates for you now).
#   2. VERDICT     — qualify the change vs a base ref as ABI-preserving/breaking
#                    (logosc --abi-diff; removals are BREAKING).
#   3. BUMP GATE   — if the ABI broke, the version (CMakeLists project VERSION)
#                    must have a higher major or minor than the base; else fail.
#   4. CLOSURE     — checks 1-3 all read the spec and can only speak about what
#                    is IN it; none can notice a type it never recorded. Require
#                    the recorded set to be CLOSED: a type named by a recorded
#                    field list or enum payload must itself have a record, or an
#                    exemption stating the reason the emitter derives.
#
#   scripts/abi-check.sh [<base-ref>]
#     base-ref   what to compare against (default: origin/main)
#
#   env: LOGOSC       path to logosc      (default build/bin/logosc, else PATH)
#        LOGOS_LIB_DIR stdlib archive dir (default <logosc dir>/../lib/logos)
#
#   exit 0 = OK (preserved, or broke-with-bump), 1 = gate failure, 2 = IO error.
#
# ⚠⚠ AND EVERY CHECK CARRIES A CANARY, IN THE SAME RUN, THROUGH THE SAME TOOL.
#
# Measured hole this closes: with an EMPTY base spec, `--abi-diff` reported
# "ADDED: 12844 record(s) / VERDICT: ABI-PRESERVING" and exited 0. Nothing was
# removed because nothing was there — the gate answered a question about a blob,
# not about the previous ABI, and said "preserved". A floor on the base spec's
# record count closes that particular hole; a canary closes the CLASS, because a
# differ that has stopped comparing at all cannot be enumerated into.
#
#   CHECK      CANARY                                RIDES / DOES NOT RIDE
#   ────────── ───────────────────────────────────── ──────────────────────────
#   build age  a file stamped in the past, compared  the same `-nt` test. Not
#              with the same `-nt`                   the archive list itself.
#   freshness  the fresh spec with one record moved  the same `diff -q`. Not
#              must compare UNEQUAL to the spec      `--emit-abi` itself; the
#                                                    record floor covers that.
#   closure    a fabricated record naming a type    the same `--abi-closure`.
#              with no record must come back a       Plus: an exemption that
#              VIOLATION; and an exemption whose     matches nothing FAILS, so a
#              reason is mutated must come back      walk that stopped reaching
#              EXEMPTION-REASON-CHANGED              goes RED, not green.
#   verdict    (a) base = spec + one real-shaped     the same `--abi-diff`
#              record the build does not have  ⇒     invocation and the same
#              MUST come back 1 / BREAKING           `$verdict` reading that
#              (b) base = the spec itself      ⇒     judges the real base. Not
#              MUST come back 0 / preserved          the bump arithmetic.
#
# (a) and (b) together pin the differ from both sides: (a) alone passes on a
# tool stuck at "breaking", (b) alone passes on a tool stuck at "preserved" —
# which is exactly what the empty-base reading looked like.
#
# BLINDING MUTATION, RE-RUN: a base ref whose `abi/logos.abi` is an empty blob.
# Was GREEN ("ADDED: 12844 record(s) / VERDICT: ABI-PRESERVING / OK"). Now RED:
#   "::error:: the base spec at '<ref>' holds sym=0 type=0 vtable=0 schema=0,
#    under the floors sym>=12368 type>=359 vtable>=115 schema>=2."
set -uo pipefail

SPEC=abi/logos.abi
EXEMPT_FILE=abi/logos.abi-closure-exempt   # check 4: ABI-closure exemptions
BASE="${1:-origin/main}"

# ── FLOORS: MEASURED VALUES, WITH THE MEASUREMENT ────────────────────────────
# Read off `abi/logos.abi` at `62835ad3` on 2026-07-31 (12844 records total):
#   sym 12368, type 359, vtable 115, schema 2
# PER CATEGORY, not a total: `sym` is 96% of the file and would carry a total on
# its own while `type` or `vtable` went silent. These are floors on BOTH the
# freshly emitted spec and the BASE spec — a base that cannot meet them is not a
# previous ABI, it is a blob, and no verdict about it is a verdict about the ABI.
# ⚠ MIN_SYM LOWERED 12368 -> 12061 on 2026-08-09, DELIBERATELY, AND THIS IS THE
# "say why" the paragraph above demands. P5 deleted the Deem interpreter and the
# DBSP incremental engine — seven stdlib files, 7199 lines
# (`stdlib/mem/deem/{check,exec,query,incr,incr_rec,mapping_state}.logos`,
# `stdlib/lcm/deem/facthistory.logos`) — so the emitted spec legitimately lost
# 307 `sym` records. The floor is a "this is not a blob" bound, not a coverage
# claim: it exists so that a spec emitted from a broken or empty build cannot
# produce a verdict. It is set to the value MEASURED on the post-cut tree, not
# rounded down, so it still refuses anything thinner. `type` went the other way
# (361, above the unchanged 359 floor) and needed no edit — which is exactly why
# these are PER CATEGORY: one category shrinking by design does not license the
# others to go silent.
MIN_SYM=12061
MIN_TYPE=359
MIN_VTABLE=115
MIN_SCHEMA=2

LOGOSC="${LOGOSC:-}"
if [ -z "$LOGOSC" ]; then
    if [ -x "build/bin/logosc" ]; then LOGOSC="build/bin/logosc"; else LOGOSC="logosc"; fi
fi
LIB_DIR="${LOGOS_LIB_DIR:-$(dirname "$LOGOSC")/../lib/logos}"

if [ ! -f "$SPEC" ]; then echo "abi-check: $SPEC missing"; exit 2; fi
if [ ! -d "$LIB_DIR" ]; then
    echo "abi-check: stdlib archive dir '$LIB_DIR' not found — set LOGOS_LIB_DIR"; exit 2
fi

workdir="$(mktemp -d)"
fresh_spec="$workdir/fresh.abi"; base_spec="$workdir/base.abi"
trap 'rm -rf "$workdir"' EXIT

# One spelling of "how many records of kind K", used for the fresh spec, the
# base spec and the canaries alike.
records_of() { grep -c "^$2	" "$1" 2>/dev/null || true; }
spec_floor() {   # spec_floor <file> <what-it-is>
    local f=$1 what=$2 s t v c
    s=$(records_of "$f" sym); t=$(records_of "$f" type)
    v=$(records_of "$f" vtable); c=$(records_of "$f" schema)
    if [ "$s" -lt "$MIN_SYM" ] || [ "$t" -lt "$MIN_TYPE" ] || \
       [ "$v" -lt "$MIN_VTABLE" ] || [ "$c" -lt "$MIN_SCHEMA" ]; then
        echo "::error:: $what holds sym=$s type=$t vtable=$v schema=$c, under the"
        echo "          floors sym>=$MIN_SYM type>=$MIN_TYPE vtable>=$MIN_VTABLE"
        echo "          schema>=$MIN_SCHEMA (MEASURED at 62835ad3, 2026-07-31)."
        echo "          A spec that thin is not an ABI surface, and a verdict"
        echo "          computed against it is not a verdict about the ABI."
        echo "          If the shrink is deliberate, edit the floor and say why."
        return 1
    fi
    echo "abi-check: $what — sym $s, type $t, vtable $v, schema $c"
    return 0
}

# ── 0. build age ──────────────────────────────────────────────────────────────
# An archive older than logosc means the build did not catch up with the
# compiler; the spec emitted from it is the old surface wearing a new name.
for a in "$LIB_DIR"/liblogos-lang.a "$LIB_DIR"/liblogos-mem.a \
         "$LIB_DIR"/liblogos-lcm.a  "$LIB_DIR"/liblogos-std.a; do
    [ -f "$a" ] || { echo "abi-check: missing archive $a — build first"; exit 2; }
    if [ "$LOGOSC" -nt "$a" ]; then
        echo "::error:: $a is older than $LOGOSC — the spec would describe the"
        echo "          previous surface. Rebuild (ninja / cmake --build) first."
        # ⚠ AND IF THAT SAYS "no work to do", THE REMEDY ABOVE IS A NO-OP.
        # This test is mtime; ninja's is its own dep log, and the two can
        # disagree — anything that touches these files from outside ninja (a
        # revert/restore, a manual copy, a parallel agent in the same build
        # dir) skews the mtimes without dirtying the dep log. MEASURED: a red
        # gate here, `cmake --build` answering "no work to do" in 0.07 s, and
        # the gate still red; only deleting the archives cleared it. So say the
        # command that actually works rather than leaving the reader to loop.
        echo "          If that reports 'no work to do' and this error persists,"
        echo "          ninja's dep log disagrees with this mtime test. Force it:"
        echo "              rm -f $LIB_DIR/liblogos-*.a && cmake --build <builddir>"
        exit 1
    fi
done
# ⚠ CANARY. The loop above passing means "no archive is older than logosc" — and
# it means exactly the same thing if `-nt` were `-ot`, or if the paths were
# wrong and the test degenerated. A file deliberately stamped in 2000 must come
# back OLDER under the SAME operator.
touch -d '2000-01-01 00:00:00' "$workdir/older_than_anything"
if [ ! "$LOGOSC" -nt "$workdir/older_than_anything" ]; then
    echo "::error:: CANARY 'build age' NOT CAUGHT — a file stamped 2000-01-01 did"
    echo "          not compare older than $LOGOSC under the same test the archive"
    echo "          loop uses. That loop's silence is not evidence. GATE BROKEN."
    exit 1
fi

# ── 1. freshness, against the BUILD ───────────────────────────────────────────
if ! "$LOGOSC" --emit-abi -L "$LIB_DIR" -o "$fresh_spec" 2>/dev/null; then
    echo "abi-check: --emit-abi failed"; exit 2
fi
spec_floor "$fresh_spec" "the spec emitted from the built stdlib" || exit 1
# ⚠ CANARY. `diff -q` reporting "same" is the whole freshness check; a `diff`
# that has stopped looking says the same thing. One record moved out of the
# fresh spec must come back DIFFERENT under the same comparison.
sed '7d' "$fresh_spec" > "$workdir/fresh_minus_one.abi"
if diff -q "$workdir/fresh_minus_one.abi" "$fresh_spec" >/dev/null 2>&1; then
    echo "::error:: CANARY 'freshness' NOT CAUGHT — a copy of the emitted spec with"
    echo "          one record deleted compared EQUAL to it. The comparison below"
    echo "          cannot see a stale spec either. GATE BROKEN."
    exit 1
fi
if ! diff -q "$fresh_spec" "$SPEC" >/dev/null 2>&1; then
    echo "::error:: $SPEC does not match the built stdlib — regenerate and commit:"
    echo "          cmake --build build --target logos-abi"
    diff -u "$SPEC" "$fresh_spec" | sed -n '1,40p'
    exit 1
fi
# Fresh, but is it COMMITTED? CI compares the committed spec against the base.
if ! git diff --quiet -- "$SPEC"; then  # lint:git-ok — the BUILD was asked above; only git knows whether the regenerated spec is COMMITTED
    echo "::error:: $SPEC is regenerated but uncommitted — commit it with the change."
    git --no-pager diff --stat -- "$SPEC"
    exit 1
fi

# ── 2. verdict vs base ────────────────────────────────────────────────────────
# ⚠ A GATE THAT COULD NOT LOOK IS NOT A GATE THAT SAW NOTHING WRONG. This branch
# used to print "NOTHING was compared" and exit 0 — it announced its own blindness
# in prose and reported success in the exit code, which is the only channel a CI
# job or a commit script reads. No base spec means the verdict below and the bump
# gate after it are both unanswerable, so the answer is red with the reason.
if ! git show "${BASE}:${SPEC}" > "$base_spec" 2>/dev/null; then  # lint:git-ok — the base spec exists ONLY in history; there is no build of it to ask
    echo "::error:: no $SPEC at '${BASE}', so NOTHING could be compared and no ABI"
    echo "          verdict exists. Pass a base ref that has one (e.g."
    echo "          scripts/abi-check.sh HEAD~1), or fetch the default base."
    exit 1
fi
# ⚠ AND A BASE THAT IS NOT AN ABI SURFACE IS NOT A BASE. MEASURED: with an empty
# base blob this script printed "ADDED: 12844 record(s) — ABI-PRESERVING" and
# exited 0. Nothing had been removed because nothing was there to remove.
spec_floor "$base_spec" "the base spec at '$BASE'" || exit 1

# ⚠⚠ CANARY, BOTH DIRECTIONS, THROUGH THE SAME `--abi-diff` AND THE SAME
# `$verdict` READING that judges the real base below.
#
# (a) A REMOVAL MUST BE SEEN. The canary base is the committed spec plus ONE
#     record of the real shape — a real `sym` line with its key prefixed — so
#     the current build is missing it and the differ must say BREAKING. This is
#     the same computation that would notice a genuinely deleted export; if it
#     comes back 0, the "preserved" verdict below is the differ not looking.
awk 'BEGIN{FS=OFS="\t"} {print}
     /^sym\t/ && !done {n=$0; sub(/^sym\t/, "sym\t__abi_canary_removed_", n); print n; done=1}' \
    "$SPEC" > "$workdir/canary_removal.abi"
if [ "$(wc -l < "$workdir/canary_removal.abi")" -le "$(wc -l < "$SPEC")" ]; then
    echo "::error:: CANARY 'removal' could not be BUILT — no sym record to derive it"
    echo "          from. The gate cannot prove its differ live. GATE BROKEN."
    exit 1
fi
"$LOGOSC" --abi-diff "$workdir/canary_removal.abi" "$SPEC" > "$workdir/canary_removal.out" 2>&1
canary_removal=$?
if [ "$canary_removal" != 1 ] || ! grep -q 'BREAKING' "$workdir/canary_removal.out"; then
    echo "::error:: CANARY 'removal' NOT CAUGHT — a base carrying one export this"
    echo "          build does not have came back verdict=$canary_removal:"
    sed -n '1,10p' "$workdir/canary_removal.out"
    echo "          --abi-diff cannot see a removal, so 'ABI preserved' below is"
    echo "          a statement about nothing. GATE BROKEN, not the tree."
    exit 1
fi
# (b) AND AN IDENTICAL SURFACE MUST COME BACK PRESERVED. Without this, a differ
#     stuck at "breaking" would satisfy (a) and every real change would be a
#     false red — the mirror image of the empty-base hole.
"$LOGOSC" --abi-diff "$SPEC" "$SPEC" > "$workdir/canary_same.out" 2>&1
canary_same=$?
if [ "$canary_same" != 0 ]; then
    echo "::error:: CANARY 'identity' NOT CAUGHT — the spec compared against ITSELF"
    echo "          came back verdict=$canary_same:"
    sed -n '1,10p' "$workdir/canary_same.out"
    echo "          --abi-diff answers the same thing regardless of its inputs."
    echo "          GATE BROKEN, not the tree."
    exit 1
fi
echo "abi-check: canaries caught — a planted removal reads BREAKING(1), an"
echo "           identical surface reads PRESERVING(0); the differ is live."

"$LOGOSC" --abi-diff "$base_spec" "$SPEC"
verdict=$?
[ "$verdict" = 2 ] && { echo "abi-check: --abi-diff error"; exit 2; }
# ⚠ AND ONLY 0 AND 1 ARE VERDICTS. Every other status is the differ failing to
# reach one — a crash (13x), a removed flag (64), a missing binary (127) — and
# the fallthrough below printed "ABI preserved" for all of them. "The tool did
# not answer" is not the answer "nothing changed".
if [ "$verdict" != 0 ] && [ "$verdict" != 1 ]; then
    echo "::error:: --abi-diff exited $verdict, which is not a verdict (0 = preserved,"
    echo "          1 = breaking, 2 = internal error). No comparison was made, so this"
    echo "          gate has no answer about the ABI."
    exit 1
fi

# ── 3. bump gate ──────────────────────────────────────────────────────────────
ver_of() {  # "MAJOR MINOR" from CMakeLists project(VERSION) at a ref ($1=ref, ""=worktree)
    local src
    if [ -z "$1" ]; then src="$(cat CMakeLists.txt)"; else src="$(git show "$1:CMakeLists.txt")"; fi  # lint:git-ok — the BASE revision's declared version, which only history holds
    echo "$src" | grep -m1 -oE 'project\(logos VERSION [0-9]+\.[0-9]+' | grep -oE '[0-9]+\.[0-9]+$'
}
base_ver="$(ver_of "$BASE")"; cur_ver="$(ver_of "")"
base_maj="${base_ver%.*}"; base_min="${base_ver#*.}"
cur_maj="${cur_ver%.*}";  cur_min="${cur_ver#*.}"

if [ "$verdict" = 1 ]; then
    if [ "${cur_maj:-0}" -gt "${base_maj:-0}" ] || \
       { [ "${cur_maj:-0}" = "${base_maj:-0}" ] && [ "${cur_min:-0}" -gt "${base_min:-0}" ]; }; then
        echo "abi-check: ABI broke AND version bumped (${base_ver} -> ${cur_ver}) — OK (intentional break)."
        exit 0
    fi
    echo "::error:: ABI-BREAKING change but version not bumped (still ${base_ver}). Bump the minor in CMakeLists (project VERSION) — or revert the breaking change."
    exit 1
fi
echo "abi-check: ABI preserved (additive or unchanged) — OK."

# ── 4. closure ────────────────────────────────────────────────────────────────
# ⚠ CHECKS 1-3 ARE ALL ABOUT RECORDS THAT EXIST. Every one of them — freshness,
# verdict, bump — reads abi/logos.abi and can only ever speak about what is IN
# it. None can notice a type the spec never recorded, and that is not a corner:
# `QEnv` was recorded carrying `f_ptrs:[fn(&[RtVal]) -> RtVal; 8]` while `RtVal`
# — a `pub enum`, the whole UDF/UDA registration surface — had no record at all.
# MEASURED on this tree, before the fix: an added `F32(f32)` arm on the real
# RtVal produced a BYTE-IDENTICAL 12881-line spec, and checks 1-3 above all
# passed, with check 2 printing "VERDICT: ABI-PRESERVING". The gate was blind by
# construction, and the blindness was in the SHAPE of the spec, not in the
# differ. Closure is the check that can see it: every type named by a recorded
# field list or enum payload must itself have a record, or an exemption stating
# the reason the emitter derives. Same probe after the fix: ABI-BREAKING.
if [ -x tests/logos/abi_closure_gate.sh ] && [ -f "$EXEMPT_FILE" ]; then
    if ! tests/logos/abi_closure_gate.sh "$LOGOSC" "$LIB_DIR" "$EXEMPT_FILE"; then
        echo "::error:: the ABI spec is not CLOSED — see above. A recorded type names a"
        echo "          type with no record, so a change to that type is invisible to"
        echo "          checks 1-3."
        exit 1
    fi
else
    # A skipped check must not read as a passed one.
    echo "::error:: closure check MISSING (tests/logos/abi_closure_gate.sh or"
    echo "          $EXEMPT_FILE absent) — checks 1-3 cannot see an unrecorded type,"
    echo "          so this run has no answer about the spec's closure."
    exit 1
fi
exit 0
