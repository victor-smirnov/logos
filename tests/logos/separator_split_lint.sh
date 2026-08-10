#!/usr/bin/env bash
# separator_split_lint.sh — the SEPARATOR CLASS cannot regrow silently.
#
# `__` is legal inside a Logos identifier AND is the separator that mangled
# names are composed with. So `name.find("__")` is not a decomposition, it is a
# GUESS about where a boundary fell — and it guesses wrong for every owner or
# method whose own name ends in `_` or contains `__`. That guess has produced
# real miscompiles: a generic enum instance never emitted (`enum foo_<T>` →
# `foo___i64`, base guessed as `foo`), a struct method never defined while its
# call was emitted, a symbol spelled two different ways by producer and
# consumer.
#
# The fix carried the parts instead of re-deriving them, and put every split
# that survives in ONE place (src/compiler/mangled_name.hpp) behind primitives
# that take the carried parts. But nothing stopped the NEXT one: a raw
# `find("__")` written tomorrow reintroduces the class with no signal at all.
# This gate is that signal.
#
# WHAT IT COUNTS: raw separator splits in the compiler sources, per file.
# Comments are stripped first — a site that EXPLAINS the class (and most of the
# surviving ones do, at length) must not read as a violation of it, or the
# incentive becomes "document less".
#
# HELD IN BOTH DIRECTIONS. A file that gains a split is RED. A file that loses
# one is RED too, so the ledger can only move by being edited deliberately:
# a number that drifts down on its own is a measurement nobody is reading.
#
# ⚠ WHAT THIS DOES NOT COVER, stated so nobody reads the census as total:
#   * only the SPLIT direction — cutting a composed name at the separator. The
#     JOIN direction (`other.starts_with(cand + "__")`, `s.contains("." + cand +
#     "__f__")`) is the same class and is NOT counted, because textually it is
#     indistinguishable from the SOUND pattern: recomposing from carried parts
#     and comparing for equality is the model the fix is built on, and it is
#     spelled the same way. A join probe is safe exactly when the candidate
#     cannot be a proper prefix of another declared name; that is an argument
#     about the registry, not about the text, so no lint can make it.
#   * only `//` comments are stripped. A split inside a /* … */ block counts as
#     live code — the safe direction (over-count, never under-count), and the
#     compiler sources use `//` throughout.
set -u

SRC_DIR=${1:?usage: separator_split_lint.sh <src/compiler dir> <ledger>}
LEDGER=${2:?usage: separator_split_lint.sh <src/compiler dir> <ledger>}

# The one file allowed to split: it IS the anchored/registry-matching home, and
# its primitives take the carried parts.
EXEMPT_BASENAME=mangled_name.hpp

# A "split" is any of the shapes that cut a composed name at the separator.
#
# ⚠ `::` JOINED THE PATTERN 2026-08-10, AND IT WAS NOT HYPOTHETICAL. `Mono`'s
# `concrete_impls_` keyed facts by the composed string `"trait::type"` and
# `populate_trait_engine_` split it with `k.find("::")`, guarded by a comment
# arguing the left operand is always a BARE `impl.trait_name()`. Canonicalising
# trait identity puts a path-qualified `pkg::Hash` on that left — and the
# invariant the comment rested on is gone, silently. The repair was to stop
# composing a string at all (the set is now a `std::pair`), and BOTH parse sites
# were deleted with it. This line exists so the next `::` split is seen the day
# it is written rather than the day it is canonicalised.
#
# ⚠ AND `::` IS DELIBERATELY *NOT* IN THE PATTERN — I added it, MEASURED the
# result, and took it out. It matches 12 sites (sema.cpp 8, sema_impl.hpp 3,
# sema_collect.cpp 1), and most are the LEGITIMATE use: parsing a real path
# `pkg::item` at the character that separates a path. A composed-key split and a
# path parse are not syntactically distinguishable, so a blanket `::` alternation
# turns this ledger into a rubber stamp — twelve entries nobody can argue about
# individually, which is precisely what the "A NEW ENTRY IS NOT A FORMALITY" rule
# in the ledger exists to prevent.
#
# The two characters are unsafe for DIFFERENT reasons and want different
# instruments. `__` is legal INSIDE an identifier, so a split there is a guess
# about text — censusable. `::` is not: the grammar refuses `impl pkg::Trait for T`
# (MEASURED: `syntax error near 'impl'`, rc 4), so a `::` split is safe exactly as
# long as nothing composes a QUALIFIED name into that operand — a whole-program
# invariant, which a per-site regex cannot see and a per-site READ can. That read
# is filed as its own task rather than faked here.
PATTERN='\.(find|rfind)\("__"\)|starts_with\("__"\)|ends_with\("__"\)'

count_file() {
    # strip // comments, then count matches
    sed 's://.*::' "$1" | grep -cE "$PATTERN" || true
}

emit_census() {
    for f in "$SRC_DIR"/*.cpp "$SRC_DIR"/*.hpp; do
        [ -e "$f" ] || continue
        b=$(basename "$f")
        [ "$b" = "$EXEMPT_BASENAME" ] && continue
        n=$(count_file "$f")
        [ "$n" -gt 0 ] && printf '%s\t%s\n' "$b" "$n"
    done | sort
}

# ── the instrument's own liveness ────────────────────────────────────────────
# A lint that silently matches nothing certifies nothing. Push a KNOWN violation
# through the SAME counter and require it to be seen; if this fails, the gate is
# blind and says so instead of passing.
canary_tmp=$(mktemp -d)
trap 'rm -rf "$canary_tmp"' EXIT
printf 'int f(const S& s) { auto p = s.find("__"); return p; }\n' > "$canary_tmp/canary.cpp"
if [ "$(count_file "$canary_tmp/canary.cpp")" != "1" ]; then
    echo "FAIL: separator-split lint is BLIND — its counter does not see a planted"
    echo "      \`find(\"__\")\`. The census below would read as a pass for any tree."
    exit 1
fi
printf 'int f(const S& s) { // s.find("__") in a comment\n  return 0; }\n' > "$canary_tmp/cmt.cpp"
if [ "$(count_file "$canary_tmp/cmt.cpp")" != "0" ]; then
    echo "FAIL: separator-split lint counts COMMENTS as code — every site that"
    echo "      documents the class would read as a violation of it."
    exit 1
fi

got=$(emit_census)
want=$(grep -vE '^\s*(#|$)' "$LEDGER" | sort)

if [ "$got" = "$want" ]; then
    echo "separator-split lint: census matches the ledger ($(printf '%s\n' "$got" | grep -c . ) files, $(printf '%s\n' "$got" | awk -F'\t' '{s+=$2} END{print s+0}') sites); canaries live."
    exit 0
fi

echo "FAIL: the raw-separator-split census does not match tests/logos/separator_split.ledger"
echo
echo "  A NEW split reintroduces the separator class: \`__\` is legal inside an"
echo "  identifier, so cutting a composed name at it is a guess. Carry the parts,"
echo "  or use src/compiler/mangled_name.hpp, whose primitives take them."
echo "  A REMOVED split is red too — update the ledger in the same commit, so the"
echo "  number can only move deliberately."
echo
diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") \
    | sed 's/^</  ledger only: /; s/^>/  tree only:   /' | grep -E 'ledger only|tree only'
exit 1
