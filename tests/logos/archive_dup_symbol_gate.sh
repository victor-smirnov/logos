#!/usr/bin/env bash
# archive_dup_symbol_gate — no STRONG symbol is defined by two library archives.
#
#   archive_dup_symbol_gate.sh <logosc> <lib-dir> <repo> <work-dir> <test-bin-dir>
#
# THE DEFECT (#738). emit_module reads the dependency archives' `nm` output as
# the binary-skip set: a function whose link name is in it is declared, not
# emitted. The reader used `fgets` into `char[512]`, so a link name of 512+
# characters arrived as fragments and matched nothing. A dependency's private
# helper reached through a pub generic was then re-emitted, strong, into the
# consumer archive; the metaprog JIT loads both and stops on
# "JIT session error: Unexpected definitions". Seen on branch deem as
# liblogos-lcm.a re-defining two logos_mem.* functions of 521 and 594
# characters; main had no name that long, so nothing there could see it.
#
# WHAT IS ASSERTED.
#  1. CANARY. tests/logos/longsym builds a dependency whose private helper
#     mangles to 600+ characters and is called from a pub generic, and a
#     consumer that instantiates the generic. The dependency must define that
#     helper strong with a name longer than 512 characters (else the long path
#     is not exercised), and the consumer archive must not define it.
#     CONTROL, measured: truncating each nm line to 511 characters in
#     emit_module made the consumer define the helper (1 strong copy).
#  2. CENSUS. Over the stdlib archives and the memoria test archives, no strong
#     name (T/D/R/B) is defined in two of them. Floor: at least 1000 names.
set -uo pipefail

LOGOSC="${1:?logosc}"
LIBDIR="${2:?stdlib bin dir}"
REPO="${3:?repo root}"
WORK="${4:?work dir}"
TBIN="${5:?test bin dir}"

rm -rf "$WORK" || exit 1
mkdir -p "$WORK/tmp" || exit 1
WORK="$(cd "$WORK" && pwd)"     || exit 1
REPO="$(cd "$REPO" && pwd)"     || exit 1
LIBDIR="$(cd "$LIBDIR" && pwd)" || exit 1
TBIN="$(cd "$TBIN" && pwd)"     || exit 1
case "$LOGOSC" in /*) ;; *) LOGOSC="$(cd "$(dirname "$LOGOSC")" && pwd)/$(basename "$LOGOSC")" ;; esac
fail=0
bad() { printf 'FAIL: %s\n' "$*"; fail=1; }

# Link names contain spaces: the name is everything after address and letter.
# Emits "<name>" per defined strong global.
strong_names() { nm -g --defined-only "$@" 2>/dev/null \
    | sed -nE 's/^[0-9a-fA-F]+ [TDRB] (.*)$/\1/p' | grep -v '^_binary_' | sort -u; }

emit() {  # emit <manifest> <out.a>
    ( cd "$REPO" && TMPDIR="$WORK/tmp" "$LOGOSC" --emit-module "$1" \
        -L "$LIBDIR" -L "$WORK" -o "$2" ) > "$2.log" 2>&1
}

# ── 1. canary ──────────────────────────────────────────────────────────────
DEP="$WORK/liblongsym-dep.a"
USE="$WORK/liblongsym-use.a"
emit tests/logos/longsym/dep/longsym-dep.module "$DEP"; rc=$?
[ "$rc" = "0" ] || { bad "emit longsym-dep (rc=$rc)"; tail -5 "$DEP.log"; exit 1; }
emit tests/logos/longsym/use/longsym-use.module "$USE"; rc=$?
[ "$rc" = "0" ] || { bad "emit longsym-use (rc=$rc)"; tail -5 "$USE.log"; exit 1; }

strong_names "$DEP" > "$WORK/dep.txt"
strong_names "$USE" > "$WORK/use.txt"
long=$(grep 'private_helper_with_a_long_mangled_name' "$WORK/dep.txt" | awk 'length($0) > 512' | grep -c '' || true)
[ "$long" = "1" ] || bad "canary: the dependency defines $long strong helper names longer than 512 characters, expected 1 — the long-name path is not exercised"
both=$(comm -12 "$WORK/dep.txt" "$WORK/use.txt" | grep -c '' || true)
if [ "$both" != "0" ]; then
    bad "canary: $both strong names defined by both longsym-dep and longsym-use:"
    comm -12 "$WORK/dep.txt" "$WORK/use.txt" | cut -c1-200
fi
echo "canary: helper name length $(grep 'private_helper' "$WORK/dep.txt" | awk '{print length($0)}'), defined in both: $both"

# ── 2. census ──────────────────────────────────────────────────────────────
archives=("$LIBDIR"/liblogos-*.a "$TBIN"/libmemoria-ctr.a "$TBIN"/libmemoria-store.a "$TBIN"/libmemoria-testkit.a)
total=0
: > "$WORK/all.txt"
for a in "${archives[@]}"; do
    [ -f "$a" ] || { bad "archive missing: $a"; continue; }
    strong_names "$a" > "$WORK/one.txt"
    n=$(grep -c '' "$WORK/one.txt" || true)
    total=$((total + n))
    sed "s|\$|\t$(basename "$a")|" "$WORK/one.txt" >> "$WORK/all.txt"
done
[ "$total" -ge 1000 ] || bad "census: only $total strong names examined (floor 1000)"
awk -F'\t' '{c[$1]++; f[$1]=f[$1]" "$2} END{for(k in c) if(c[k]>1) print substr(k,1,200) "\t" f[k]}' \
    "$WORK/all.txt" > "$WORK/dups.txt"
ndup=$(grep -c '' "$WORK/dups.txt" || true)
if [ "$ndup" != "0" ]; then
    bad "census: $ndup strong names defined in more than one archive:"
    head -20 "$WORK/dups.txt"
fi
echo "census: ${#archives[@]} archives, $total strong names, $ndup defined twice"

if [ "$fail" != "0" ]; then echo "archive_dup_symbol_gate: FAILED"; exit 1; fi
echo "archive_dup_symbol_gate: OK"
