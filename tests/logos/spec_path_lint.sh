#!/usr/bin/env bash
# spec_path_lint.sh REPO_ROOT
#
# THE LANGUAGE SPEC IS PROSE, AND PROSE HAS NO EXIT CODE.
#
# `docs/spec/*.md` is the normative description of Deem, EL and Trama. Every
# rule carries an `*Evidence:*` line naming the source that implements it — the
# one part of a spec a machine CAN check. Nothing checked it, and it rotted in
# two independent ways at once:
#
#   1. THE PACKAGE NAME WAS WRITTEN AS A DIRECTORY. The spec said
#      `stdlib/std/wql/…` and `stdlib/std/deem/…` throughout, because the
#      PACKAGE is `logos.std.wql`. The directory has always been
#      `stdlib/mem/wql/` (PACKAGE ≠ MODULE). Roughly thirty Evidence lines
#      pointed at paths that had never existed, through every review this repo
#      has run.
#   2. P5 DELETED FIVE OF THE FILES IT CITES, and the spec kept describing the
#      capabilities they implemented in the present tense — including a rule
#      (`deem.source.engine-state`, ADR 0016 M5 case S) for a capability the
#      same change withdrew.
#
# The census has a gate (`tests/logos/census_pin_gate.sh`) whose FACT 1 does
# exactly this for one document, and it caught six drifts on the day it landed.
# This applies the cheapest half of it to the spec: EVERY PATH TOKEN NAMES
# SOMETHING THAT EXISTS. It judges no prose and checks no claim — it only
# guarantees the nouns are real.
#
# A file the spec must be able to discuss AFTER it is deleted is declared with a
# `<!-- spec-gone: <path> — <why> -->` comment, and the declaration is checked in
# the direction that can be abused: the path must NOT exist. Same shape as the
# census gate's GONE-FILE / FACT 7.
#
# AND IT PROVES ITSELF: three canaries plant a known defect in a COPY and demand
# this same reader sees it, so a broken reader reports the GATE broken rather
# than the spec clean.
set -u

ROOT=${1:?usage: spec_path_lint.sh <repo root>}
[ -d "$ROOT" ] || { echo "GATE BROKEN: repo root $ROOT is not a directory"; exit 4; }
cd "$ROOT" || exit 4

SPECS=$(ls docs/spec/*.md 2>/dev/null)
[ -n "$SPECS" ] || { echo "GATE BROKEN: no docs/spec/*.md — if the spec moved, repoint this gate"; exit 4; }

# ── readers ─────────────────────────────────────────────────────────────────
# Path-shaped tokens: those carrying a directory, under a known top-level dir.
path_tokens() {
    grep -ohE '(docs|tests|stdlib|src|tools|scripts|abi)/[A-Za-z0-9_./{},*-]*[A-Za-z0-9_}*]' "$@" | sort -u
}
# Declared corpses: `<!-- spec-gone: <path> — <why> -->`
gone_lines() { grep -ohE '<!-- *spec-gone: *[^>]*-->' "$@" | sed -E 's/<!-- *spec-gone: *//; s/ *-->$//'; }
gone_paths() { gone_lines "$@" | awk '{print $1}'; }

fail=0
note() { echo "FAIL: $*"; fail=$((fail + 1)); }

check_paths() {
    local p q
    gone_paths "$@" | sort -u > "$TMPD/gone"
    while read -r p; do
        [ -n "$p" ] || continue
        grep -qxF "$p" "$TMPD/gone" && continue     # declared gone; the corpse check owns it
        # brace lists and globs both go through the shell, like the census gate
        for q in $(eval echo "$p" 2>/dev/null); do
            if compgen -G "$q" > /dev/null 2>&1 || [ -e "$q" ]; then continue; fi
            note "the spec names \`$q\`, which does not exist.
      Either it moved (fix the Evidence line) or it was deleted — and then the
      rule beside it is describing a language that is gone. If the spec must
      still discuss it, declare it: <!-- spec-gone: $q — why -->"
        done
        # ⚠ THERE IS DELIBERATELY NO "DID IT EXPAND TO NOTHING" CHECK, and the
        # first version of this loop had one — `[ "$hit" -ge 0 ] || true`, which
        # `logos_00_gate_lint` correctly flagged as vacuous (R6). Replacing it
        # with `[ "$cand" -ge 1 ]` was no better: MEASURED, `eval echo` on every
        # form this regex can produce yields at least the token itself —
        # `docs/x{}`, `docs/nomatch*`, `docs/spec/{a` all echo back unchanged —
        # so a zero count is UNREACHABLE and the check could never fire. A guard
        # that cannot fire is the defect one level up, so it is gone; every token
        # reaches the existence test above, which is the assertion.
        # (Command substitution inside `eval` is not a hazard here: the token
        # regex admits no `$` and no backtick, so nothing in a spec file can be
        # executed by this expansion.)
    done < <(path_tokens "$@")
}

check_gone() {
    local line p why
    while read -r line; do
        [ -n "$line" ] || continue
        p=${line%%[[:space:]]*}
        why=$(printf '%s' "$line" | sed -E 's/^[^[:space:]]+[[:space:]]*//; s/^— *//')
        [ -e "$p" ] && note "the spec declares \`$p\` spec-gone, but it EXISTS.
      That declaration exempts the path from the existence check, so declaring a
      LIVE file gone is a hole punched in this gate — which is the one thing it
      is for."
        [ -n "$why" ] || note "the spec-gone line for \`$p\` carries no reason.
      A rule that survives its implementation has to say what replaced it, or a
      reader cannot tell a withdrawal from an oversight."
    done < <(gone_lines "$@")
}

check_all() { check_paths "$@"; check_gone "$@"; }

TMPD=$(mktemp -d); trap 'rm -rf "$TMPD"' EXIT

# ── SELF-CANARIES, before the real run ──────────────────────────────────────
canary() {                                        # canary <name> <sed program>
    local name=$1 prog=$2 n f
    mkdir -p "$TMPD/c"; rm -f "$TMPD/c"/*.md
    for f in $SPECS; do sed -E "$prog" "$f" > "$TMPD/c/$(basename "$f")"; done
    if diff -rq "$TMPD/c" docs/spec > /dev/null 2>&1; then
        echo "GATE BROKEN: canary '$name' did not modify the spec — its pattern no"
        echo "  longer matches, so it has been measuring nothing. Re-point it."
        exit 4
    fi
    n=$( fail=0; check_all "$TMPD/c"/*.md > /dev/null 2>&1; echo "$fail" )
    [ "$n" -gt 0 ] || { echo "GATE BROKEN: canary '$name' planted a defect the checker did"
                        echo "  NOT see. Whatever it is blind to there, it is blind to for real."; exit 4; }
}

canary moved-path  's#stdlib/mem/wql/el\.logos#stdlib/mem/wql/el_GONE.logos#'
canary gone-live   's#<!-- spec-gone: stdlib/mem/deem/query\.logos#<!-- spec-gone: stdlib/mem/deem/deem.logos#'
canary gone-mute   's#(<!-- spec-gone: stdlib/mem/deem/query\.logos)[^>]*-->#\1 -->#'

# ── the real check ──────────────────────────────────────────────────────────
fail=0
check_all $SPECS
[ "$fail" -ne 0 ] && exit 1

n_paths=$(path_tokens $SPECS | grep -c .)
n_gone=$(gone_lines $SPECS | grep -c .)
echo "spec path lint: $(echo "$SPECS" | wc -w) spec file(s) hold. $n_paths path token(s)"
echo "  resolve (brace lists and globs expanded); $n_gone declared spec-gone file(s)"
echo "  are really gone and each says why. Three self-canaries live."
echo "  ⚠ This gate checks NOUNS, not claims: a rule can still describe behaviour"
echo "  the code no longer has, as long as it cites files that exist."
exit 0
