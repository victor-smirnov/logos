# Bag-hunt categorization & cluster analysis

**Phase 3 deliverable** of the [refactor + adversarial bag-hunt](../../../.claude/plans/memoized-whistling-cherny.md). Aggregates findings from all 14 per-group catalogs, identifies architectural clusters, and maps each cluster to a strategic fix and a forward-looking note for AST-analysis / Datalog phases.

**Inputs**: 14 per-feature catalogs in [docs/baghunt/](.).
**Total confirmed bugs**: ~122 entries across catalogs (some deferred, some are positive design choices flagged for clarification).

## Bug counts by feature group

| Group | File | Bugs | Notes |
|---|---|---|---|
| Module & Visibility | [module-visibility.md](module-visibility.md) | 11 | 4× P0 parser-assert crashes; cross-pkg fn vs trait inconsistency |
| Items | [items.md](items.md) | 11 | Recursive struct → SEGFAULT; dup field/variant/trait silent |
| Functions | [functions.md](functions.md) | 10 | Trailing-comma cluster; dup param silent |
| Const & Type aliases | [const-alias.md](const-alias.md) | 6 | Self-ref const → SEGFAULT |
| Type System | [types.md](types.md) | 10 | 2× P0 SEGFAULTs (unit tuple, impl-Trait param) |
| Generics & Bounds | [generics.md](generics.md) | 10 | Dup type-param/lifetime silent; bound arity silent |
| Statements & Assignments | [statements.md](statements.md) | 10 | **Block-scope shadow LEAKS** (P0); let-else non-diverging silent |
| Pattern Matching | [patterns.md](patterns.md) | 8+2 | 5 surface gaps (let pat=, slice ..rest, byte-string, etc.) |
| Expressions & Precedence | [expressions.md](expressions.md) | 9+1 | 4× silent miscompiles (overflow, div0, shift-neg, unary on unsigned) |
| Literals (non-Writ) | [literals.md](literals.md) | 4 | underweight; struct-update type mismatch silent |
| Writ Literals | [writ.md](writ.md) | 6+2 | **Nested @{...}/@[...] don't parse** (P0 major gap) |
| Attributes & Meta blocks | [attributes.md](attributes.md) | 8 | Whole attr system has no validation |
| Metaprog | [metaprog.md](metaprog.md) | 5 | Best-validated group; instantiate non-generic silent |
| Lexical | [lexical.md](lexical.md) | 7 | 2× ASSERT crashes (empty file, BOM); char literal collision with lifetimes |

**Total**: ~122 bug entries (110 confirmed + ~12 deferred / forward-flagged).

## P0 highlights — compiler crashes + silent miscompiles

### Crash (SEGFAULT or LOGOS_ASSERT)

| Bug | Trigger |
|---|---|
| B-mv-05 | `package fn;` (keyword as pkg name) |
| B-mv-06 | missing `package` declaration |
| B-mv-07 | `package main.;` (trailing dot) |
| B-mv-08 | `package ;` (empty path) |
| B-it-01 | recursive by-value struct |
| B-ca-01 | self-referential const `pub const X: i32 = X + 1;` |
| B-ty-01 | empty tuple type `()` in fn sig |
| B-ty-02 | `impl Trait` at parameter position |
| B-lx-01 | empty source file |
| B-lx-02 | UTF-8 BOM at file start |

**Total: 10 confirmed P0 compiler crashes.** All on malformed/edge-case input.

### Silent miscompiles (P0 — runs but produces wrong output)

| Bug | Symptom |
|---|---|
| B-st-01 | block-scoped `let` LEAKS out of inner block (return loads inner shadow) |
| B-it-03 | duplicate struct field name silently kept (layout undefined) |
| B-it-04 | duplicate enum variant kept |
| B-ca-04 | duplicate const def silently shadows |
| B-fn-02 | duplicate fn parameter name silent |
| B-gn-01/02 | duplicate type/lifetime param silent |
| B-ex-01 | int overflow at compile-time silently wraps |
| B-ex-02 | division by zero literal silent (runtime SIGFPE) |
| B-ex-03 | shift by negative count silent (LLVM UB) |
| B-ex-04 | unary `-` on unsigned silent |
| B-ex-05 | `int as Struct` cast silent |
| B-ex-07 / B-he-04 / B-lx-04 | integer literal overflow saturates |
| B-li-03 | struct update `..base` with mismatched base type silent |
| B-he-02 | duplicate Writ-map key kept |
| B-pt-01 | duplicate binding name in tuple pattern silent |

**Total: 16+ silent miscompiles.**

## Architectural clusters

Below are the load-bearing clusters. Each spans multiple feature groups and admits a single architectural fix.

### Cluster 1 — `assertion-as-diagnostic` (6 P0 crashes)

**Member bugs**: B-mv-05, B-mv-06, B-mv-07, B-mv-08, B-lx-01, B-lx-02

**Pattern**: Parser hits `LOGOS_ASSERT(LOGOS-PARSE-001)` instead of producing an error node when a required token is absent (missing `package` decl, malformed path, empty file, BOM, etc.). Compiler aborts on user input.

**Architectural fix**: Replace `LOGOS_ASSERT` calls in the parser-output code with `error()` recovery. Audit all `LOGOS_ASSERT.*PARSE` sites; many should become diagnostic productions with synthesized error nodes.

**Forward-looking**: With AST analysis (Phase 5) this becomes a "structural validity" pass over the parsed AST; until then, fix at source.

### Cluster 2 — `missing-uniqueness-check` (8 distinct sites, ~10 bug instances)

**Member bugs**: B-it-03 (struct fields), B-it-04 (enum variants), B-it-05 (trait defs), B-fn-02 (fn params), B-ca-04 (const defs), B-gn-01 (type-params), B-gn-02 (lifetime-params), B-pt-01 (pattern bindings), B-he-02 (Writ-map keys), B-at-03 (annotations)

**Pattern**: Wherever sema collects a list of named items (fields, params, variants, etc.) it appends without dup-check. Same fix pattern needed at every site; only some sites currently have it (struct-lit dup-detect works, struct-def dup-detect doesn't).

**Architectural fix**: Single helper `validate_unique_names(items, kind_label)` that returns Err with location. Apply at all collection sites. One PR, ~10 call sites.

**Forward-looking**: Trivial Datalog query: `dup_name(item) :- in_list(item, list), in_list(item2, list), name(item) == name(item2), item != item2`. Phase 5 lints automatically catch any new sites.

### Cluster 3 — `no-attribute-validation` (~5 bugs)

**Member bugs**: B-at-01 (unknown attr), B-at-02 (`#[type_code]` on generic), B-at-04 (`#[tag_dispatch]` on non-trait), B-at-05 (`#[zoned]` on enum), B-at-06 (`#[derive(NonExistent)]`), B-at-07 (type_code in reserved range), B-mt-04 (unknown trigger)

**Pattern**: Attribute system has no per-attr spec; any `#[name]` parses + sema-OK; only some have specialized handlers that consume them. Most "wrong-attr-on-wrong-target" silently does nothing.

**Architectural fix**: Per-attribute spec table:
```cpp
struct AttrSpec {
    std::string name;
    AllowedTargets targets;     // bitmask: Struct | Enum | Datatype | Trait | Fn
    ValueShape value_shape;     // None | Int(min, max) | String | EnumLit | KvList
    DupPolicy dup_policy;       // Forbid | LastWins | Aggregate
};
```
Plus a single validation pass that checks every annotation against the registry. New attrs get added by registering rather than by code-search.

**Forward-looking**: When AST analysis lands, the registry becomes machine-queryable — external tools can ask "what attributes can target X" without reading sema source.

### Cluster 4 — `missing-arity-check` (5 bugs)

**Member bugs**: B-ty-03 (too-many type-args), B-ty-04 (too-few), B-ty-05 (args on non-generic), B-gn-04 (bound-args), B-mt-03 (instantiate-args)

**Pattern**: Generic-instantiation type-arg count not validated at sema. Symptom varies — silent drop, leak to mlir-gen, type-mismatch with cryptic display.

**Architectural fix**: Single `check_type_arg_arity(template, args)` helper called at every generic-instantiation site. ~5 call sites.

**Forward-looking**: Datalog: `arity_mismatch(use, def) :- generic_use(use, args), generic_def(def, params), template_of(use, def), len(args) != len(params)`.

### Cluster 5 — `diagnostic-from-codegen` (4 bugs)

**Member bugs**: B-ty-04 (too-few type-args), B-st-05 (break with bad label), B-pt-07 (arm after catch-all), B-mt-03 (instantiate arity)

**Pattern**: Sema lets through invalid programs; mlir-gen catches them with cryptic verifier errors that reference low-level IR concepts ("unresolved TypeVar 'B' — mono_pass required"). User can't trace back to source.

**Architectural fix**: Audit mlir-gen failure paths; for each, identify the corresponding sema check that should have caught it earlier. Move validation upstream.

**Forward-looking**: This becomes a CI invariant: "no mlir-gen error should fire on a sema-clean program". Phase 5 lint enforces.

### Cluster 6 — `missing-cycle-guard` (5 bugs)

**Member bugs**: B-it-01 (recursive by-value struct), B-it-02 (recursive enum), B-ca-01 (self-ref const), and likely more not yet probed (recursive datatypes, recursive traits with bounds).

**Pattern**: Type / definition resolution recurses without a "currently being resolved" set. Crashes via stack overflow or assertion when the user creates a cycle.

**Architectural fix**: Add visited-set / depth-limit guard at each resolution entry: `register_struct`, `register_tagged_enum`, `resolve_const`. Specific cycle types might need custom diagnostics ("infinite-size type 'Node'").

**Forward-looking**: Datalog: `cycle(t) :- depends_on(t, t')+, depends_on(t', t)+`. Detect at fact-base level.

### Cluster 7 — `lexer-greedy-collision` (3 bugs)

**Member bugs**: B-ty-07 (`&&mut T` parsed as logical-AND), B-ty-08 (`||` zero-arg closure), B-lx-07 (`'A'` char literal vs lifetime token)

**Pattern**: Lexer greedy-tokenizes operator pairs that have alternate semantic interpretations in type/closure positions.

**Architectural fix**: Either context-aware lexer (heavy) OR documentation requiring spaces (`& &mut`). Or grammar-side: add alternate productions accepting both `AMP AMP` and `AMPAMP` followed by appropriate continuations.

**Forward-looking**: A token-level spec that links tokens to position-context could resolve at parse time.

### Cluster 8 — `literal-saturation-no-error` (3 bugs, all instances of one root)

**Member bugs**: B-ex-07, B-he-04, B-lx-04 — same numeric-literal-overflow path.

**Pattern**: `parse_int_literal` silently saturates on overflow. Documented in [feedback_literal_saturation](../../../.claude/projects/-home-victor-devel-logos/memory/feedback_literal_saturation.md).

**Architectural fix**: One-line bounds check in the literal parser. High-leverage.

### Cluster 9 — `no-reachability-lint` (3 bugs)

**Member bugs**: B-st-07 (unreachable wildcard arm), B-st-08 (dead code after return), B-pt-07 (arm after catch-all)

**Pattern**: No CFG-walk reachability analysis. Unreachable code compiles silently.

**Architectural fix**: Small reachability pass over LIR (one walk over function body, mark statements as reachable based on block-terminator analysis). Emits warnings on unreached statements.

**Forward-looking**: This is a textbook Phase 5 / AST-analysis pass — natural Datalog formulation.

### Cluster 10 — `assignment-matrix-incomplete` (1 confirmed, more suspected)

**Member bugs**: B-st-04 (`*p += v` not parsed)

**Pattern**: Assignment matrix in [statements.md](../spec/statements.md) lists 15 LHS shapes × 10 compound ops = 150 combinations. Some are wired, some aren't. The matrix lacks systematic coverage.

**Architectural fix**: Audit + complete the matrix. New helper to mechanically generate all combinations from a single core "assign" lowering.

### Cluster 11 — `pattern-surface-coverage-gap` (5 bugs)

**Member bugs**: B-pt-02 (struct pat in let), B-pt-03 (byte-string), B-pt-04 (nested pat in enum payload), B-pt-05 (slice ..rest), B-pt-06 (float literal in pat)

**Pattern**: Pattern grammar is asymmetric across positions (`let pat = expr;` vs `match arm`); several Rust-style forms simply don't parse.

**Architectural fix**: Unify pattern grammar — single `pattern` non-terminal accepted everywhere, with refutability check at use site.

**Forward-looking**: Phase 5 reachability + exhaustiveness become richer with the unified surface.

### Cluster 12 — `nested-form-untested` (1 huge bug)

**Member bug**: B-he-01 — nested `@{...}` / `@[...]` don't parse despite grammar allowing recursion.

**Pattern**: All existing tests use FLAT Writ literals; the recursive corner of `writ_val` was never exercised. Single-bug architectural fix.

**Architectural fix**: Debug + fix the parser dispatch for nested `@`-prefix tokens inside `writ_val`.

## Distribution by tag (top categories)

Counts as of 2026-05-06 (regenerated via [/tmp/regen_tags.py](/tmp/regen_tags.py) from per-catalog tables; columns: open / fixed / N/A — N/A = not-a-bug or not-reproduced after re-verification).

| Tag | Open | Fixed | N/A | Total | Note |
|---|---|---|---|---|---|
| `oversight:simple` | 2 | 58 | 1 | 61 | low-hanging fruit; mostly closed |
| `design:incomplete` | 5 | 30 | 10 | 45 | intentional gaps, roadmap items |
| `tech-debt:missing-uniqueness-check` | 0 | 9 | 0 | 9 | Cluster 2 — closed |
| `tech-debt:no-attribute-validation` | 1 | 6 | 0 | 7 | Cluster 3 |
| `tech-debt:misleading-diagnostic` | 2 | 4 | 1 | 7 | cross-cutting |
| `tech-debt:assertion-as-diagnostic` | 0 | 6 | 0 | 6 | Cluster 1 — closed |
| `tech-debt:missing-arity-check` | 0 | 5 | 0 | 5 | Cluster 4 — closed |
| `tech-debt:diagnostic-from-codegen` | 0 | 4 | 0 | 4 | Cluster 5 — closed |
| `tech-debt:missing-cycle-guard` | 0 | 3 | 0 | 3 | Cluster 6 — closed |

**Global totals**: 15 open + 95 fixed + 13 N/A = 123 entries across 14 catalogs.

The `design:incomplete` cluster is now the dominant **open** category — these are surface-incompleteness gaps (a feature works in position X but not in Y, an attribute lacks a related form, etc.) that are individually small but tedious to enumerate. Phase 5 fact-base / linter work will surface most of them automatically.

The `oversight:simple` tag is large because most P0 silent miscompiles are "we just didn't add the check". They're not deep architectural problems — once identified, the fixes are small. The architectural value comes from clusters: identifying the *family* of missing checks (uniqueness, arity, cycle, etc.) and adding them all at once.

## Forward-looking — what becomes trivial in Phase 5

When **AST analysis (transformative metaprog)** and **Datalog over fact-base** land:

| Cluster | Becomes |
|---|---|
| Missing-uniqueness | Datalog: `dup_name(...)` query — automatic across all kinds |
| Missing-arity | Datalog: `arity_mismatch(use, def)` query |
| Missing-cycle-guard | Datalog: SCC over depends-on graph |
| No-reachability-lint | AST walk → reachability fact-base |
| No-attribute-validation | Spec registry queryable from external tools |
| Pattern-surface coverage | Refactor as data: pattern shapes become first-class |

This means the strategy in Phase 4 should distinguish:
- **Fix now** (the architectural patches that close current bugs)
- **Defer to Phase 5** (issues that get a much cleaner solution with fact-base infra)

For example: per-cluster diagnostic improvements ("show pkg in type names") need now-fixes because they ship visible behavior change. But cross-feature consistency lints (e.g. "every list-of-names should be unique") can wait for Datalog and become near-zero-cost.

## Phase 4 deliverables (next)

1. `docs/baghunt/strategy.md` — dependency-ordered fix plan for clusters above.
2. Per-cluster fix sketches: which sema/parser/mlir-gen files need touching.
3. Sequencing: which architectural fixes go first (so cleanups land on a clean base).
4. Future-deferred items explicitly marked.
