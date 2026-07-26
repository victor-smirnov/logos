#!/usr/bin/env bash
# abi-check — the minor-bump gate. Four checks:
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
#
#   scripts/abi-check.sh [<base-ref>]
#     base-ref   what to compare against (default: origin/main)
#
#   env: LOGOSC       path to logosc      (default build/bin/logosc, else PATH)
#        LOGOS_LIB_DIR stdlib archive dir (default <logosc dir>/../lib/logos)
#
#   exit 0 = OK (preserved, or broke-with-bump), 1 = gate failure, 2 = IO error.
set -uo pipefail

SPEC=abi/logos.abi
BASE="${1:-origin/main}"

LOGOSC="${LOGOSC:-}"
if [ -z "$LOGOSC" ]; then
    if [ -x "build/bin/logosc" ]; then LOGOSC="build/bin/logosc"; else LOGOSC="logosc"; fi
fi
LIB_DIR="${LOGOS_LIB_DIR:-$(dirname "$LOGOSC")/../lib/logos}"

if [ ! -f "$SPEC" ]; then echo "abi-check: $SPEC missing"; exit 2; fi
if [ ! -d "$LIB_DIR" ]; then
    echo "abi-check: stdlib archive dir '$LIB_DIR' not found — set LOGOS_LIB_DIR"; exit 2
fi

# ── 0. build age ──────────────────────────────────────────────────────────────
# An archive older than logosc means the build did not catch up with the
# compiler; the spec emitted from it is the old surface wearing a new name.
for a in "$LIB_DIR"/liblogos-lang.a "$LIB_DIR"/liblogos-mem.a \
         "$LIB_DIR"/liblogos-lcm.a  "$LIB_DIR"/liblogos-std.a; do
    [ -f "$a" ] || { echo "abi-check: missing archive $a — build first"; exit 2; }
    if [ "$LOGOSC" -nt "$a" ]; then
        echo "::error:: $a is older than $LOGOSC — the spec would describe the"
        echo "          previous surface. Rebuild (ninja / cmake --build) first."
        exit 1
    fi
done

# ── 1. freshness, against the BUILD ───────────────────────────────────────────
fresh_spec="$(mktemp)"; base_spec="$(mktemp)"
trap 'rm -f "$fresh_spec" "$base_spec"' EXIT
if ! "$LOGOSC" --emit-abi -L "$LIB_DIR" -o "$fresh_spec" 2>/dev/null; then
    echo "abi-check: --emit-abi failed"; exit 2
fi
if ! diff -q "$fresh_spec" "$SPEC" >/dev/null 2>&1; then
    echo "::error:: $SPEC does not match the built stdlib — regenerate and commit:"
    echo "          cmake --build build --target logos-abi"
    diff -u "$SPEC" "$fresh_spec" | sed -n '1,40p'
    exit 1
fi
# Fresh, but is it COMMITTED? CI compares the committed spec against the base.
if ! git diff --quiet -- "$SPEC"; then
    echo "::error:: $SPEC is regenerated but uncommitted — commit it with the change."
    git --no-pager diff --stat -- "$SPEC"
    exit 1
fi

# ── 2. verdict vs base ────────────────────────────────────────────────────────
if ! git show "${BASE}:${SPEC}" > "$base_spec" 2>/dev/null; then
    echo "abi-check: WARNING — no $SPEC at '${BASE}', so NOTHING was compared."
    echo "           Pass a base ref that has one (e.g. scripts/abi-check.sh HEAD~1)."
    exit 0
fi
"$LOGOSC" --abi-diff "$base_spec" "$SPEC"
verdict=$?
[ "$verdict" = 2 ] && { echo "abi-check: --abi-diff error"; exit 2; }

# ── 3. bump gate ──────────────────────────────────────────────────────────────
ver_of() {  # "MAJOR MINOR" from CMakeLists project(VERSION) at a ref ($1=ref, ""=worktree)
    local src
    if [ -z "$1" ]; then src="$(cat CMakeLists.txt)"; else src="$(git show "$1:CMakeLists.txt")"; fi
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
exit 0
