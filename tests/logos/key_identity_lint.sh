#!/usr/bin/env bash
# key_identity_lint.sh REPO_ROOT LEDGER
#
# A LOOKUP KEY IS NOT AN IDENTITY — and the class kept arriving one instance per
# round until somebody counted it.
#
# A map keyed by an entity's NAME answers for whoever registered that name
# FIRST. A second declaration of the same name — a user `struct TypeId` where
# the stdlib has one, a user `fn popcount_u64`, a user `impl Drop for String` —
# silently inherits the first one's meaning. Six instances were confirmed
# separately, each costing a full round:
#
#   1. the mangler's type-arg spelling: a user struct sharing a stdlib name got
#      the stdlib's memory LAYOUT, its drop and its operators (#58/#59/#60).
#   2. `ffo_canonical` / `find_func_op`: strips the package off callee AND every
#      definition, then binds first-wins.
#   3. mono devirtualising a TypeVar-receiver call writes its own worklist key
#      as the callee name while the function is emitted under the LIR name (#83).
#   4. `impls_` keyed by a BARE trait target (#88).
#   5. `closure_kind_` keyed by `type_str(signature)`, not the literal (#90).
#   6. sema's `resolve_type_assoc_ref` — measured INERT, the honest fourth cell.
#
# The class is MECHANICALLY ENUMERABLE: every string-keyed lookup is a grep. So
# it gets a census, not a seventh bug report.
#
# ── WHAT IS COUNTED, AND WHY NOT EVERYTHING ─────────────────────────────────
# The naive population — every `std::*map<std::string,…>` in src/compiler — is
# 352 declarations and 1,554 access sites, of which 1,519 are textually "bare".
# A 372-row ledger is a RUBBER STAMP: nobody can argue a row, so nobody reads
# one, and that is precisely the failure separator_split_lint.sh refused when it
# declined to add `::` to its pattern. This lint narrows by KEY DOMAIN, which is
# what the class actually is:
#
#   SUBJECT = an access into a string-keyed container whose KEY EXPRESSION is
#             produced by an ENTITY-NAME producer and is not composed by a
#             package-qualifying primitive.
#
# Both halves are DERIVED from the tree, never listed:
#   * the container set is every identifier ending in `_` declared as StrMap /
#     StrSet / std::[unordered_][multi]map|set<std::string,…> anywhere in
#     src/compiler or include/logos/compiler. `StrMap`/`StrSet` matter: `impls_`,
#     `traits_`, `coherence_keys_` and `cfg_features_` are spelled that way and
#     are INVISIBLE to a `<std::string` grep. A lint built on the headline grep
#     alone would not see #88 at all.
#   * an ENTITY-NAME PRODUCER is any call whose callee name ends in `name` or is
#     `type_str` — `struct_name()`, `enum_name()`, `trait_name()`,
#     `concrete_struct_name()`, `pkg_name()`, `type_var_name()`, `link_name()`,
#     `type_str()`. Derived from the call text, not enumerated here.
#   * QUALIFYING PRIMITIVES are `sema_key(pkg,name)` and `qualify_pkg(pkg,name)`.
#     A key composed by either carries the package and is not a subject.
#
# That yields 44 sites in 12 files — a population somebody can argue about
# row by row, which is the only kind that gets read.
#
# ── THE FOUR CELLS, AND WHY NONE OF THEM IS AN UNCHECKED HATCH ──────────────
# An exemption nobody checks is worse than no gate, because the green now
# vouches for it. Every cell here is checked in the ABUSE direction — two are
# DERIVED (the ledger cannot claim them, it can only agree), two demand a
# ground written AT THE SITE where the next reader will be:
#
#   O  ORDER PROPERTY — qualified probe FIRST, bare slot LAST. Held by the site
#      itself: a package-qualifying call appears within the preceding 900
#      characters. This is `find_struct_repr_`'s documented shape, and reversing
#      it once reddened two imported tests. DERIVED. Abuse direction: delete the
#      qualified probe that precedes and the cell flips to a bare lookup — RED,
#      naming the site. The ledger cannot assert O onto a site that lacks it.
#
#   Q  QUALIFIED BY CONSTRUCTION — the key is a MANGLED LINK NAME
#      (`sym::link_name(...)`), which carries module and package by
#      construction. DERIVED from the key text. Abuse direction: re-key the site
#      to a bare `.name()` and Q no longer derives — RED.
#
#   D  BARE BY A CARRIED DESIGN DECISION. Requires a `KEY-IDENTITY:` ground
#      comment within 12 lines above the site. Abuse direction: tag D in the
#      ledger without writing the ground, or delete the ground later — RED,
#      naming the site. The ground lives in the source, not here, because a
#      ledger entry is read once and a comment is read by whoever edits next.
#
#   B#nn BARE AND REACHABLE — a KNOWN OPEN DEFECT, carrying its task number.
#      Requires a `KEY-IDENTITY: OPEN #nn` comment at the site with the SAME
#      number as the ledger row. Abuse direction: tag B on a site with no open
#      defect recorded at it, or renumber one side only — RED. A defect may sit
#      in this cell; it may not sit here silently or anonymously.
#
# HELD IN BOTH DIRECTIONS, like separator_split_lint.sh: a site that DISAPPEARS
# is red too. A conversion that qualifies a key is a deliberate ledger edit in
# the same commit; a count that drifts down on its own is a measurement nobody
# is reading.
#
# ── WHAT THIS LINT DOES NOT COVER, stated so nobody reads it as total ────────
#   * KEYS LAUNDERED THROUGH A LOCAL. `std::string n{ft.struct_name()}; …
#     impls_.count("StableLayout::" + n)` at sema.cpp is a bare entity-name key
#     that this lint DOES NOT SEE, because the producer call is not in the key
#     expression. This is an UNDER-count and it is the same blind spot
#     separator_split_lint.sh records for its own pattern. Two of the three
#     sibling probes at that site ARE seen, so the site is in the census — but
#     do not read the per-site counts as exhaustive within a statement.
#   * THE SCAN-BY-NAME SUB-POPULATION. `callee == "popcount_u64"` is the same
#     class with no map in it (a linear scan is a lookup), and it produced the
#     WRONG-ANSWER instance: a user `fn popcount_u64` compiled to `llvm.ctpop`
#     while its own body was emitted and never called. It is held TWICE, and
#     this file used to claim it could not be censused at all:
#       FACT 3 pins the two guards in `sema_expr.cpp` STRUCTURALLY — they exist,
#       and the comparisons are CONTAINED in them by brace matching.
#       FACT 4 pins the POPULATION TREE-WIDE — per file, a count plus a digest
#       of the sorted literals. The earlier claim that "a regex cannot tell a
#       name-scan from any other string compare, so it is not censused" was
#       REFUTED by this round's own verify: it planted an intercept in
#       `sema_stmt.cpp` and this gate stayed GREEN, while `mlir_gen.cpp` already
#       held `if (name == "AnyVal")` — a silent wrong answer of exactly the
#       shape FACT 3 exists to stop, in a file FACT 3 never reads. A regex
#       indeed cannot CLASSIFY a name-scan; that is the human's job at the pin
#       move. It can refuse to let one appear unnoticed, and that is FACT 4.
#   * `/* … */` blocks. Only `//` comments are stripped, so a subject inside a
#     block comment counts as live code — the safe direction, and the compiler
#     sources use `//` throughout.
#
# EXIT CODES: 0 pass, 1 census/claim mismatch, 2 THE LINT COULD NOT MEASURE.
# A subject that does not resolve must never read as zero hits.
set -u

ROOT=${1:?usage: key_identity_lint.sh <repo root> <ledger>}
LEDGER=${2:?usage: key_identity_lint.sh <repo root> <ledger>}
SRC="$ROOT/src/compiler"
INC="$ROOT/include/logos/compiler"

for d in "$SRC" "$INC"; do
    [ -d "$d" ] || { echo "FAIL(2): subject does not resolve: $d"; exit 2; }
done
[ -f "$LEDGER" ] || { echo "FAIL(2): ledger does not resolve: $LEDGER"; exit 2; }
command -v python3 >/dev/null || { echo "FAIL(2): no python3 — the census cannot run."; exit 2; }

SCANNER="$(dirname "$0")/key_identity_scan.py"
[ -f "$SCANNER" ] || { echo "FAIL(2): scanner does not resolve: $SCANNER"; exit 2; }

# ── FACT 0: THE INSTRUMENT'S OWN LIVENESS ───────────────────────────────────
# A lint that silently matches nothing certifies nothing. Push KNOWN subjects
# through the SAME scanner and require each to be seen or not seen as claimed.
# If any canary fails the lint is BLIND and says so (exit 2) instead of passing.
CANARY=$(mktemp -d); trap 'rm -rf "$CANARY"' EXIT
mkdir -p "$CANARY/src/compiler" "$CANARY/include/logos/compiler"
cat > "$CANARY/src/compiler/canary.cpp" <<'CEOF'
struct C {
    StrMap<int> planted_reg_;
    std::unordered_map<std::string,int> planted_std_;
    void a(TypeRef t) { planted_reg_.find(std::string(t.struct_name())); }
    void b(TypeRef t) { planted_std_.count(type_str(t)); }
    void c(TypeRef t) { planted_reg_.find(sema_key(pkg_, t.struct_name())); }
    void d(TypeRef t) { planted_reg_.find(qualify_pkg(pkg_, t.struct_name())); }
    void e(TypeRef t) { planted_reg_.find(t.some_opaque_slot()); }
    void f(TypeRef t) { /* planted_reg_.find(std::string(t.enum_name())); */ }
    void g(TypeRef t) { // planted_reg_.find(std::string(t.enum_name()));
    }
};
CEOF
# ⚠ COUNT SITES, NOT ROWS. The first version of this canary counted output
# LINES and was BLIND to exactly the defect it was written for: disabling
# comment stripping turns the `//` plant in g() into a subject, but g() and the
# block-comment plant in f() share a (file, registry, cell, key) row, so the row
# count stayed 3 and the canary passed. MEASURED. Sum column 4.
cn=$(python3 "$SCANNER" "$CANARY/src/compiler" "$CANARY/include/logos/compiler" 2>/dev/null \
     | awk -F'\t' '{s+=$4} END{print s+0}')
# a() and b() are subjects. c()/d() are qualified by the two roots. e() is not
# an entity name. g() is a `//` comment and must vanish. f() is a BLOCK comment
# and counts as live — the safe direction (over-count, never under-count),
# asserted here so that over-count stays deliberate rather than accidental.
if [ "$cn" != "3" ]; then
    echo "FAIL(2): the key-identity scanner is BLIND or MISCALIBRATED."
    echo "         Planted 2 bare entity-name subjects + 1 block-comment"
    echo "         over-count + 2 qualified + 1 non-entity + 1 // comment;"
    echo "         the scanner saw $cn subject SITES, expected 3."
    python3 "$SCANNER" "$CANARY/src/compiler" "$CANARY/include/logos/compiler" 2>&1 | sed 's/^/           /'
    exit 2
fi

got=$(python3 "$SCANNER" "$SRC" "$INC") || { echo "FAIL(2): scanner errored on the real tree."; exit 2; }
nsites=$(printf '%s\n' "$got" | awk -F'\t' '{s+=$4} END{print s+0}')
if [ "${nsites:-0}" -lt 1 ]; then
    echo "FAIL(2): the census is EMPTY over src/compiler. Either every key in the"
    echo "         compiler became qualified (state it in the ledger and re-pin),"
    echo "         or the scanner stopped resolving its subject. It does not read"
    echo "         as a pass."
    exit 2
fi

want=$(grep -vE '^\s*(#|$)' "$LEDGER" | sort)
got=$(printf '%s\n' "$got" | sort)

rc=0
# ── THE ONE HATCH THE DIFF ALONE WOULD LEAVE OPEN ───────────────────────────
# A site with no cell scans as `!UNGROUNDED`, which fails the diff below — but
# only until somebody pastes the `!UNGROUNDED` row INTO the ledger and the diff
# goes quiet. Cell D and cell B exist so a bare key carries a reason; a row with
# neither is refused here by name, whichever side it appears on.
if printf '%s\n' "$want" "$got" | grep -q '!UNGROUNDED'; then
    rc=1
    echo "FAIL: a bare entity-name key with NO GROUND."
    echo
    echo "  Every cell in this census carries a reason: O and Q are derived from"
    echo "  the site, D and B#nn are written AT the site as a \`KEY-IDENTITY:\`"
    echo "  comment. \`!UNGROUNDED\` is not a cell — it is the absence of one, and"
    echo "  pinning it in the ledger would make the green vouch for a key nobody"
    echo "  has argued. Qualify the key, or write the ground where the next"
    echo "  reader will be."
    echo
    printf '%s\n' "$got" | grep '!UNGROUNDED' | sed 's/^/  tree:   /'
    printf '%s\n' "$want" | grep '!UNGROUNDED' | sed 's/^/  ledger: /'
    echo
fi
if [ "$got" != "$want" ]; then
    rc=1
    echo "FAIL: the key-identity census does not match $(basename "$LEDGER")"
    echo
    echo "  A NEW row is a NEW string-keyed lookup whose key is an entity NAME"
    echo "  with no package on it — the class that cost six rounds. Qualify the"
    echo "  key (sema_key / qualify_pkg), or add the row with a cell and the"
    echo "  ground the cell demands."
    echo "  A REMOVED row is red too: a conversion is a deliberate ledger edit in"
    echo "  the same commit, so the count can only move on purpose."
    echo
    diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") \
        | sed 's/^</  ledger only: /; s/^>/  tree only:   /' | grep -E 'ledger only|tree only'
    echo
fi

# ── FACT 3: THE SCAN-BY-NAME GUARDS ARE STILL THERE AND STILL CONTAIN ───────
# The wrong-answer instance lived in a linear scan, not a map: ~66
# `callee == "<bare name>"` comparisons that ran BEFORE resolve_function_call,
# so a user `fn popcount_u64` became llvm.ctpop. The fix was two guards, and
# FACT 4 below holds the population everywhere else. This asserts the guards
# STRUCTURALLY:
# both exist, and the comparisons are CONTAINED IN them by brace matching.
# The counts are pinned in both directions — a new intrinsic comparison written
# OUTSIDE the guard moves the outside count and reds, which is the whole point.
if ! python3 "$SCANNER" "$SRC" "$INC" --guards "$LEDGER"; then rc=1; fi

# ── FACT 4: THE SCAN-BY-NAME POPULATION, TREE-WIDE, COUNT + ROSTER ──────────
# FACT 3 reads ONE file. The verify of this very round planted a bare-name
# intercept in `sema_stmt.cpp` and this gate stayed green — and the tree was
# already carrying `if (name == "AnyVal")` in `mlir_gen.cpp`, a silent wrong
# answer (a user `struct AnyVal` reads a garbage field, no diagnostic, exit 1).
# A gate blind to the shape that produced the defect is worse than no gate,
# because the green vouches for the blind spot.
#
# So the population is pinned per FILE: a count, and an md5-8 of the sorted
# literal list. A new intercept moves the count of whatever file it is born in;
# a literal swapped for another at a constant count moves the digest. Bitten in
# four shapes (new site in sema_stmt.cpp; new site in mlir_gen.cpp; literal
# swap at constant count; a pinned file's intercepts deleted) — all four red,
# restore green. Exits 2 rather than 0 when the population reads as empty: this
# compiler never holds zero, so zero means the scanner stopped matching.
python3 "$SCANNER" "$SRC" "$INC" --scans "$LEDGER"; s4=$?
if [ "$s4" = 2 ]; then exit 2; fi
if [ "$s4" != 0 ]; then rc=1; fi

# ── FACT 5: A NAME PASSED, NOT A NAME COMPARED (task #106) ──────────────────
# FACT 4's four matchers ALL key on `==` / `!=`. The whole `struct_lit("Type",
# …)` channel — a bare entity name handed to a synthesis call as an ARGUMENT —
# is not a comparison, so seventeen sites were invisible to it, one of them two
# lines below a site #102 had converted. That is the SECOND time a channel
# escaped this gate because a matcher tracked a SPELLING rather than the
# question; the first was the nested-paren hole closed in #99, over sites that
# round had itself converted. FACT 5 asks the other half of the question.
#
# The population is pinned PER CALLEE (count + roster digest), and the callee
# set is DERIVED — every callee whose first argument is a nominal-entity-shaped
# literal — never hand-listed. So a future `struct_lit("Foo", …)` reds at birth
# as a NEW ROW even though `struct_lit` holds zero such sites today.
#
# ⚠ FACT 5 DOES NOT CLASSIFY, and most of its rows are CORRECT: `make_synth_*`
# is the #102 FIX, and its bare literal is the KEY into the owner table rather
# than a lookup. Presence is a census, not an accusation.
#
# FACT 5's OWN CANARY. Same reason FACT 0 exists: a matcher that silently stops
# matching would turn this whole fact green. Planted subjects go through the
# SAME scanner.
ACAN=$(mktemp -d); trap 'rm -rf "$CANARY" "$ACAN"' EXIT
mkdir -p "$ACAN/src/compiler" "$ACAN/include/logos/compiler"
cat > "$ACAN/src/compiler/acanary.cpp" <<'AEOF'
struct C {
    void a() { make_widget_type("AnyVal"); }
    void b() { struct_lit("QuoteItemBlob", f, t); }
    void c() { getenv("LOGOS_DUMP_EVERYTHING"); }
    void d() { emplace_back("kind", v); }
    void e() { make_widget_type(pkg, "AnyVal"); }
    void f() { // struct_lit("Commented", f, t);
    }
};
AEOF
# a() and b() are subjects on two DIFFERENT callees -> 2 rows, 2 sites.
# c() is ALL-CAPS (env var) and must not match — it is the 91-site class whose
# inclusion would make this gate unkeepable. d() is all-lowercase (a FIELD name,
# not an entity). e() has the name in SECOND position — a stated blind spot,
# asserted here so the limit stays deliberate rather than accidental. f() is a
# `//` comment and must vanish.
arows=$(python3 "$SCANNER" "$ACAN/src/compiler" "$ACAN/include/logos/compiler" \
        --argscan-raw 2>/dev/null | grep -c .)
asites=$(python3 "$SCANNER" "$ACAN/src/compiler" "$ACAN/include/logos/compiler" \
        --argscan-raw 2>/dev/null | awk '{s+=$3} END{print s+0}')
if [ "$arows" != "2" ] || [ "$asites" != "2" ]; then
    echo "FAIL(2): the FACT 5 arg-position matcher is BLIND or MISCALIBRATED."
    echo "         Planted 2 entity-name arguments on 2 callees + an ALL-CAPS"
    echo "         env string + an all-lowercase field name + a second-position"
    echo "         name + a // comment; the scanner saw $arows row(s)/$asites site(s),"
    echo "         expected 2/2."
    python3 "$SCANNER" "$ACAN/src/compiler" "$ACAN/include/logos/compiler" \
        --argscan-raw 2>&1 | sed 's/^/           /'
    exit 2
fi

python3 "$SCANNER" "$SRC" "$INC" --argscans "$LEDGER"; s5=$?
if [ "$s5" = 2 ]; then exit 2; fi
if [ "$s5" != 0 ]; then rc=1; fi

if [ "$rc" = 0 ]; then
    nrows=$(printf '%s\n' "$got" | grep -c .)
    echo "key-identity lint: census matches the ledger ($nrows rows, $nsites bare"
    echo "  entity-name-keyed sites); cells O/Q re-derived, D/B grounds present at"
    echo "  their sites; scan-by-name guards contain their comparisons; the tree-wide"
    echo "  bare-name intercept roster matches per file; the bare entity-name ARGUMENT"
    echo "  roster matches per callee (FACT 5, #106); canaries live."
fi
# `rc` is a LITERAL, never a captured process status: set to 0 once above and to
# 1 at exactly five `rc=1` sites in this file, so the 8-bit ceiling that turns
# `exit 256` into a green run is unreachable. Every path that cannot MEASURE has
# already exited 2 directly, so a 0 here always means "measured, and clean".
exit $rc  # lint:exit-ok — `rc` is set only to the literals 0 and 1, see above
