#!/usr/bin/env bash
# no_dup_use_gate.sh LOGOSC TEST_LOGOS
#
# A synth module's import list is a SET. It is assembled from three sources
# that cannot see each other — the quote's own `use` decls, the enclosing
# handler module's imports baked into the blob at lowering time, and the
# originating user module's imports merged in afterwards — so the same package
# arrives more than once whenever two of them name it. sema_collect then warns
# `duplicate 'use …;' in module` once per duplicate per emitted item: a
# diagnostic written for hand-written copy-paste, fired at generated code the
# user cannot edit.
#
# Deem emits one quote per generated fn and every quote carries the same fixed
# import prelude, so this scales with the number of queries in the program: it
# measured 819 warnings across a full build before the dedup sweep in
# logos_emit_item_blob_subst, and 6 after (those 6 are real duplicates in
# hand-written stdlib source, which is what the warning is FOR).
#
# ⚠ The dumps cannot gate this. `render_module_source_for_dump` prints each
# package once, so --gen-dir / --dump-metaprog output looks clean either way.
# The only observable is the compiler's own stderr.
#
# ⚠⚠ BUT THE ABSENCE OF A WARNING IS ONLY EVIDENCE IF SOMETHING WAS GENERATED.
# This gate is a pure negative — "no line matched" — and a pure negative is green
# on a fixture that emits no synth module at all, on a compiler that stopped
# emitting the diagnostic, and on one that stopped generating code. So the dumps
# ARE read here, not for the duplicates they cannot show but as the FLOOR: this
# fixture must still produce generated modules carrying imports, or the silence
# above is silence about nothing.
#
# ⚠⚠⚠ AND THE FLOOR IS NOT ENOUGH EITHER, because the OTHER half of the
# instrument is the compiler's diagnostic and this gate's `grep` for it. A
# compiler that stopped emitting `duplicate 'use …'` at all, or a `grep` looking
# for a string the diagnostic no longer spells, is green with everything
# generated and every floor met. So the gate compiles a program with a DUPLICATE
# IMPORT WRITTEN IN IT BY HAND and requires that same grep to find the warning.
#
# ⚠⚠⚠⚠ AND THE CANARY MUST RIDE THE SAME INVOCATION IT CERTIFIES (2026-08-01).
# It used to be a THREE-LINE program compiled WITHOUT `--gen-dir`, while the
# verdict is the silence of a compile WITH `--gen-dir`. Those are two different
# invocations of two different inputs, so anything that suppresses the
# diagnostic on the metaprogramming path alone is invisible: MEASURED with a
# wrapper that filtered `duplicate 'use` from stderr ONLY when `--gen-dir` was
# present — the gate exited 0 and printed "the detector is proven live", which
# was FALSE of the compile whose silence it had just read.
#
# The canary is now THE FIXTURE ITSELF, copied, with one `use` line duplicated
# in place, compiled with the IDENTICAL argv shape. RIDES: the same source, the
# same `--gen-dir` flag, the same metaprog and synth-module path, the same
# diagnostic channel, the same `grep -F` pattern. DOES NOT RIDE: the injection
# of a duplicate BY THE SYNTH PATH — a source file can only carry a hand-written
# one; what this proves is that the detector the verdict depends on is alive on
# the very invocation the verdict is about.
#
# FLOORS ARE MEASURED VALUES, read off this gate on 2026-07-31 at `62835ad3`:
# 16 dumps, 12 emitted fns, 424 `use` lines. They were 8 / 6 / 200 — half of
# each, so half of the generated surface could disappear unremarked.
set -euo pipefail

MIN_DUMPS=16
MIN_FNS=12
MIN_USES=424
# The diagnostic the whole gate is a negative of. One spelling, used by the
# canary and by the verdict.
DUP_PAT="duplicate 'use"

LOGOSC="${1:?logosc path}"
TEST_LOGOS="${2:?fixture path}"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

# NOT `logosc … | grep`: read the whole stream to a file first, then match it.
# `grep -q` closes the pipe on its first hit and the writer dies of SIGPIPE,
# which `set -o pipefail` reports as a compiler failure — intermittently, under
# load only. Same reason run_gendir_test.sh splits its objdump read from its
# match.
if ! "$LOGOSC" "$TEST_LOGOS" --gen-dir "$TMPD/gen" -o "$TMPD/t.o" 2>"$TMPD/err"; then
    echo "FAIL: logosc failed:"; cat "$TMPD/err"; exit 1
fi

# ── THE FLOOR: this fixture still generates modules, and they carry imports ──
shopt -s nullglob
DUMPS=("$TMPD"/gen/*.gen.logos)
n_fns=0; n_uses=0
if [ "${#DUMPS[@]}" -gt 0 ]; then
    n_fns=$(cat "${DUMPS[@]}"  | grep -cE '^(pub )?fn ' || true)
    n_uses=$(cat "${DUMPS[@]}" | grep -c '^use '        || true)
fi
if [ "${#DUMPS[@]}" -lt "$MIN_DUMPS" ] || [ "$n_fns" -lt "$MIN_FNS" ] \
   || [ "$n_uses" -lt "$MIN_USES" ]; then
    echo "FAIL: ${#DUMPS[@]} generated dumps (floor $MIN_DUMPS), $n_fns emitted fns"
    echo "      (floor $MIN_FNS), $n_uses import lines (floor $MIN_USES)."
    echo "      This gate asserts that NO duplicate-import warning was emitted; with"
    echo "      nothing generated that assertion is about nothing, and 'the compiler"
    echo "      emitted no synth module' would read exactly like 'the dedup holds'."
    exit 1
fi

# ── THE CANARY: THE DETECTOR IS ALIVE, IN THIS RUN ──────────────────────────
# A hand-written duplicate import. The SAME compiler and the SAME `grep -F
# "$DUP_PAT"` that pronounce the verdict below must find it here.
# The fixture, with its FIRST `use` line emitted twice. Built with awk so the
# duplicated package is whatever the fixture actually imports, and so a fixture
# that stopped importing anything is caught here rather than passing silently.
awk 'BEGIN{done=0} { print } /^use /{ if (!done) { print; done=1 } }
     END{ if (!done) { print "NO_USE_LINE_IN_FIXTURE" > "/dev/stderr"; exit 9 } }' \
    "$TEST_LOGOS" > "$TMPD/canary.logos" || {
        echo "FAIL (CANARY unavailable): $TEST_LOGOS has no \`use\` line to"
        echo "      duplicate, so the detector cannot be proven live on it."
        exit 1; }
# ⚠ THE SAME ARGV SHAPE AS THE REAL COMPILE — `--gen-dir` included. A diagnostic
# suppressed on the metaprogramming path alone is exactly what the old canary
# (a three-line program, no `--gen-dir`) could not see.
if ! "$LOGOSC" "$TMPD/canary.logos" --gen-dir "$TMPD/cgen" -o "$TMPD/canary.o" \
        2>"$TMPD/canary.err"; then
    echo "FAIL (CANARY): the duplicate-import canary did not compile:"
    cat "$TMPD/canary.err"; exit 1
fi
if ! grep -Fq "$DUP_PAT" "$TMPD/canary.err"; then
    echo "FAIL (CANARY 'duplicate import' NOT CAUGHT): a module with the same"
    echo "      \`use\` written twice produced no /$DUP_PAT/ on stderr. Either the"
    echo "      compiler stopped emitting that diagnostic, or it no longer spells"
    echo "      it this way, or it is SUPPRESSED ON THIS VERY INVOCATION — and this"
    echo "      gate's whole verdict is the ABSENCE of that string on exactly such"
    echo "      an invocation. Its silence below is not evidence. GATE BROKEN, not"
    echo "      the tree."
    sed -n '1,10p' "$TMPD/canary.err"
    exit 1
fi
n_canary=$(grep -Fc "$DUP_PAT" "$TMPD/canary.err" || true)

if grep -F "$DUP_PAT" "$TMPD/err" > "$TMPD/hits"; then
    echo "FAIL: generated modules carry duplicate imports —"
    echo "      the synth USES dedup in logos_emit_item_blob_subst regressed."
    sort < "$TMPD/hits" | uniq -c | sort -rn
    exit 1
fi
echo "OK: no duplicate imports across ${#DUMPS[@]} generated modules ($n_uses import lines,"
echo "    $n_fns emitted fns) — and the detector is proven live ON THIS SHAPE OF"
echo "    INVOCATION: the same fixture with one \`use\` duplicated, compiled with"
echo "    the same --gen-dir argv, raised $n_canary warning(s) through the same grep."
exit 0
