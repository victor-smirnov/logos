#!/usr/bin/env bash
# abi-check — the CI minor-bump gate. Run AFTER building and regenerating the
# spec (cmake --build <dir> --target logos-abi). Three checks:
#
#   1. FRESHNESS  — the committed abi/logos.abi must equal the just-regenerated
#                   one (a dev who changed the ABI must commit the new spec).
#   2. VERDICT    — qualify the change vs a base ref as ABI-preserving/breaking
#                   (logosc --abi-diff).
#   3. BUMP GATE  — if the ABI broke, the version (CMakeLists project VERSION)
#                   must have a higher major or minor than the base; else fail.
#
#   scripts/abi-check.sh [<base-ref>]
#     base-ref   what to compare against (default: origin/main)
#
#   exit 0 = OK (preserved, or broke-with-bump), 1 = gate failure, 2 = IO error.
set -uo pipefail

SPEC=abi/logos.abi
BASE="${1:-origin/main}"

LOGOSC="${LOGOSC:-}"
if [ -z "$LOGOSC" ]; then
    if [ -x "build/bin/logosc" ]; then LOGOSC="build/bin/logosc"; else LOGOSC="logosc"; fi
fi

# ── 1. freshness ──────────────────────────────────────────────────────────────
if [ ! -f "$SPEC" ]; then echo "abi-check: $SPEC missing"; exit 2; fi
if ! git diff --quiet -- "$SPEC"; then
    echo "::error:: $SPEC is stale — run 'cmake --build build --target logos-abi' and commit it."
    git --no-pager diff --stat -- "$SPEC"
    exit 1
fi

# ── 2. verdict vs base ────────────────────────────────────────────────────────
base_spec="$(mktemp)"; trap 'rm -f "$base_spec"' EXIT
if ! git show "${BASE}:${SPEC}" > "$base_spec" 2>/dev/null; then
    echo "abi-check: no $SPEC at $BASE (first spec on this branch?) — skipping verdict."
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
