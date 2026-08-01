#!/usr/bin/env bash
# abi-analyze — qualify the ABI change in abi/logos.abi between two git revisions
# as ABI-preserving (patchset) or ABI-breaking (minor bump). Thin git wrapper
# around `logosc --abi-diff`; the verdict drives the minor-bump gate.
#
#   scripts/abi-analyze.sh [<revA>] [<revB>]
#     revA  baseline revision         (default: last release tag, else HEAD)
#     revB  revision to compare       (default: the working tree)
#
#   exit 0 = ABI-preserving, 1 = ABI-breaking, 2 = usage/IO error.
#
# Examples:
#   scripts/abi-analyze.sh                 # HEAD baseline vs working tree
#   scripts/abi-analyze.sh v0.1-pre HEAD   # between two tags/commits
set -euo pipefail

SPEC=abi/logos.abi

# Locate logosc: $LOGOSC, else the build tree, else PATH.
LOGOSC="${LOGOSC:-}"
if [ -z "$LOGOSC" ]; then
    if [ -x "build/bin/logosc" ]; then LOGOSC="build/bin/logosc"; else LOGOSC="logosc"; fi
fi

REV_A="${1:-HEAD}"
REV_B="${2:-}"

tmp_a="$(mktemp)"
trap 'rm -f "$tmp_a" "${tmp_b:-}"' EXIT
git show "${REV_A}:${SPEC}" > "$tmp_a"  # lint:git-ok — this tool compares two REVISIONS by name; the artefact of a past revision is not built here

if [ -n "$REV_B" ]; then
    tmp_b="$(mktemp)"
    git show "${REV_B}:${SPEC}" > "$tmp_b"
    target="$tmp_b"
else
    target="$SPEC"   # working tree (regenerate with: cmake --build build --target logos-abi)
fi

exec "$LOGOSC" --abi-diff "$tmp_a" "$target"
