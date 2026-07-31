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
# above is silence about nothing. Measured when the floor was written: 16 dumps,
# 12 emitted fns, 424 `use` lines.
set -euo pipefail

MIN_DUMPS=8
MIN_FNS=6
MIN_USES=200

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

if grep -F "duplicate 'use" "$TMPD/err" > "$TMPD/hits"; then
    echo "FAIL: generated modules carry duplicate imports —"
    echo "      the synth USES dedup in logos_emit_item_blob_subst regressed."
    sort < "$TMPD/hits" | uniq -c | sort -rn
    exit 1
fi
echo "OK: no duplicate imports across ${#DUMPS[@]} generated modules ($n_uses import lines, $n_fns emitted fns)"
exit 0
