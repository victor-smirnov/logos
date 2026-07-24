#!/usr/bin/env bash
# render_type_fidelity.sh LOGOSC TEST_LOGOS
#
# Content gate on the AST→source renderer (sema_render.cpp Stage 2). The
# --gen-dir round-trip gate (run_gendir_test.sh) only proves the dump
# REPARSES — and the failures this pins all reparse happily while meaning
# something else: `impl Fam<dyn Tag>` rendered as `impl Fam<Tag>`,
# `&mut dyn Tag` as `dyn Tag`, `Wrap<u64>` as `Wrap: u64`. So assert the
# rendered TEXT, line by line.
#
# Reads the --dump-metaprog output of the fixture; every expected line must
# appear verbatim (modulo leading indentation).
set -euo pipefail

LOGOSC="${1:?logosc path}"
TEST_LOGOS="${2:?fixture path}"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if ! "$LOGOSC" --dump-metaprog="$TMPD/dump" -c "$TEST_LOGOS" -o "$TMPD/t.o" \
        2>"$TMPD/err"; then
    echo "FAIL: logosc failed:"; cat "$TMPD/err"; exit 1
fi

DUMP=$(find "$TMPD/dump" -name post_quote.logos | head -1)
if [ -z "$DUMP" ]; then
    echo "FAIL: no post_quote.logos under $TMPD/dump"
    find "$TMPD/dump" -type f
    exit 1
fi

# Type ARGUMENTS on an impl header — the slot the parameter renderer used to
# misread field-by-field.
EXPECT=(
    'impl Fam<dyn Tag> for CDyn {'
    'impl Fam<Wrap<u64>> for CGen {'
    'impl Fam<*mut u8> for CPtr {'
    'impl Fam<(u64, i32)> for CTup {'
    'impl Fam<[u64; 4]> for CArr {'
    # Trait-object shapes — reference form, mutability, `+ Bound` markers,
    # Fn-family arg list and result, bare-pointer pointee, lifetime.
    'fn d_ref(x: &dyn Tag) -> u64 {'
    'fn d_mut(x: &mut dyn Tag) -> u64 {'
    'fn d_bnd(x: &dyn Tag + Send) -> u64 {'
    'fn d_arg(x: &dyn Gen<u64> + Send + Sync) -> u64 {'
    'fn d_fn1(x: &dyn Fn(u64) -> u64) -> u64 {'
    'fn d_fn0(x: &dyn Fn() -> u64) -> u64 {'
    'fn d_ptr(x: *mut dyn Tag) -> u64 {'
    "fn d_lft<'a>(x: &'a dyn Tag) -> u64 {"
)

fail=0
for want in "${EXPECT[@]}"; do
    if ! grep -qF "$want" "$DUMP"; then
        echo "FAIL: rendered dump missing: $want"
        fail=1
    fi
done

# A degraded node renders as the `<ty:CODE>` marker — never acceptable in a
# corpus fixture, and it is what a silently-dropped shape looks like.
if grep -q '<ty:' "$DUMP"; then
    echo "FAIL: dump contains un-rendered <ty:CODE> markers:"
    grep -n '<ty:' "$DUMP"
    fail=1
fi

if [ "$fail" != 0 ]; then
    echo "--- rendered dump ($DUMP) ---"
    cat "$DUMP"
    exit 1
fi
exit 0
