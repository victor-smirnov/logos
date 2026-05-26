# Logos ↔ Rust grammar conformance audit

> Strategy (Victor 2026-05-25): stop point-fix bug-hunting; do a **cascade of
> refactoring + generalization**, working through the Rust Language Reference
> (`/home/victor/cxx/reference/src/*.md`) top-down. Bring the Logos grammar
> (`tools/peg_gen/grammars/logos.peg`) into conformance with the Rust grammar,
> **except** where `docs/DIVERGENCES.md` records a blessed divergence.

## Method

For each Rust grammar production (read top-down: lexical → items → statements →
expressions → patterns → types), classify the Logos counterpart:

- ✅ **conformant** — accepts the same forms.
- ⚠️ **divergence** — intentional, recorded in DIVERGENCES.md (§A) or a Logos
  addition. No action.
- ❌ **gap** — Logos is narrower / differently-shaped than Rust with no blessed
  reason. **Fix, generalizing the whole class** (not the one shape).
- 🔧 **over-enumerated** — Logos splits into many variant productions what Rust
  expresses with one general rule. Collapse + route through one sema path.

Each grammar change gates 100% (full ctest) and ships with a regression test.

## Findings

### Statements (`reference/src/statements.md`)

Rust: `Statement -> ; | Item | LetStatement | ExpressionStatement | MacroInvocationSemi`.
- `LetStatement -> let PatternNoTopAlt (: Type)? (= Expr | = Expr else Block)? ;`
- Assignment is an **expression** (`AssignmentExpression : Expr = Expr`,
  `CompoundAssignmentExpression : Expr op= Expr`) with a **place expression** LHS
  — there is exactly ONE assignment form, place generality comes free.

| Construct | Logos | Status |
|---|---|---|
| `let pat (: T)? (= e)?` | works incl. no-annotation + tuple pat (verified) | ✅ |
| `let pat = e else { }` | `let_else_stmt` | ✅ |
| assignment `place = value` | **~17 productions** (`assign_stmt`, `field_write_stmt`, `index_write_stmt`, `deref_write_stmt`, `chain_field_write_stmt`, `tuple_field_write_stmt` + `*_compound_assign_stmt` + `destructure_assign_stmt`) ending in a general `place_assign_stmt <- atom ASSIGN expr` catch-all | 🔧 over-enumerated — grammar parses via the catch-all, but the 16 specialized rules each route to a specialized sema path. Target: collapse to Rust's 2 forms over a place expr + one general place-write sema path (`gen_lvalue_addr`). Higher-risk (sema codegen must cover all place shapes); sequence after cleaner items. |
| expr-stmt with/without block | works | ✅ |

### Loops (`reference/src/expressions/loop-expr.md`)

| Construct | Logos | Status |
|---|---|---|
| `for PATTERN in iter { }` | was `IDENT`-only; **G-CONF-1 DONE** — for-each grammar gained a `pattern` alt (after the IDENT fast-path); `lower_for_each` binds a synth element var + destructures the pattern as a body prologue across all 4 paths (array/slice/&Vec/iterator). Tuple patterns by-value/by-ref/nested supported; richer forms reject cleanly. | ✅ (tuple); struct/tuple-struct pattern loop-vars are a follow-up |
| `loop` / `while` / `while let` / labeled | present | ✅ (verify details later) |

### Match (`reference/src/expressions/match-expr.md`)

| Construct | Logos | Status |
|---|---|---|
| `match EXPR { arms }` (any scrutinee) | `match p.x { }` works (verified) — the `match_head_var <- IDENT` is one alt; expression scrutinees route via the match-expression path | ✅ |

### Pattern element generality (`reference/src/patterns.md`)

- Tuple / slice / variant-payload pattern *elements* historically used
  `pat_binding` (bindings only) rather than full sub-patterns — the root behind
  the recurring "nested pattern" point-fixes (K4 / B170 / G172-11). Much has been
  widened incrementally; **audit pending** — confirm element productions allow
  arbitrary `pat_single` uniformly. (To do in the patterns pass.)

---

# Full grammar catalog (2026-05-25)

Compared the complete set of Rust Reference grammar blocks (`reference/src/**`,
the ` ```grammar ` fences) against the Logos PEG. Legend: ✅ conformant ·
⚠️ blessed divergence (DIVERGENCES.md / Logos model) · ❌ gap (fix) ·
🔧 over-enumerated (collapse).

## Paths (`paths.md`) — divergence (Logos two-tier path model)

⚠️ **Divergence — Logos path model (Victor 2026-05-25):**
- The **package name** is `.`-separated: `my.cool.package.name`.
- **Inside** a structure (item-internal access — type, trait, assoc const/type,
  method, variant) it is `::`-separated.
- A fully-qualified path therefore reads:
  **`my.cool.package.name.TraitName::CONST`** (dotted package prefix, then `::`
  into the item). Turbofish `Vec::<i64>` and qualified `<T as Trait>::Item` use
  `::` as usual.

No Rust `super`/`self`/`crate`/`$crate` segments (no module tree). NOT a Rust
catch-up item — this is the intended Logos design.

**Current state vs the model:** the `::`-into-item part works for a resolved
name (`S::C`, `Type::method`, verified). The **fully-qualified dotted-package +
`::`-item** form in *expression* position (`logos.mem.x.Vec::new()`) is a
**parse error** — **ACCEPTED AS-IS for now (Victor 2026-05-25)**: `use` the
package + short name (`Vec::new()`) is fine. Not in the active queue; revisit
with the broader package-model design.

## Items

| Rust production | Logos | Status |
|---|---|---|
| `Module` (`mod` / `mod {}`) | packages (`package x;` + `use`) | ⚠️ divergence |
| `ExternCrate` | n/a | ⚠️ divergence |
| `UseDeclaration` (`use Tree;`, `::`-paths, `*`, `{}`, `as`) | `use a.b.c;`, `use a.b.{V1,V2};` | ⚠️ **DIVERGENCE** (Victor 2026-05-25) — Logos will have a DIFFERENT package model, not Rust's module/use tree. Not a conformance item; revisited when the package model is designed. `as`/glob/`::`-paths are part of that future model, not Rust catch-up. |
| `Visibility` `pub`, `pub(crate)`, `pub(in path)`, `pub(self/super)` | `pub` only | ⚠️ **DIVERGENCE** (Victor 2026-05-25) — tied to the future (non-Rust) package model; restricted-visibility forms belong to that design, not Rust conformance. |
| `StructStruct` / `TupleStruct` / unit | all present incl. `struct Foo;` (G172-14) | ✅ |
| `Enumeration` + tuple/struct variants + discriminant | present; discriminant via `= Expr`/metacall/xref | ✅ (discriminant-from-arbitrary-const is const-eval ⚠️ A1) |
| `Union` | none | ⚠️/❌ — unions absent; rare, defer |
| `Trait` (`unsafe`/`auto`, supertraits, assoc items) | present | ✅ |
| `Implementation` inherent/trait/negative/unsafe | present | ✅ |
| `Function` qualifiers `const async safe unsafe extern` | `unsafe`/`extern`; `async`⚠️A4, `const`⚠️A2; **`safe` keyword** absent (extern-block niche) | ✅ for the non-divergent subset |
| `SelfParam` shorthand `&self`/`&mut self`/`self` + `self: Type` | BOTH `&self` shorthand (verified ✅) and explicit `self: &T` accepted | ✅ |
| `GenericParams`: lifetime / type / **const** params | present | ✅ |
| └ **default type param** `<T = Type>` | **parse error** (verified) | ❌ GAP → candidate fix |
| `WhereClause` incl. `for<…>` HRTB, lifetime preds | present (`where_pred`, HRTB) | ✅ |
| `TypeAlias` (+ bounds, where) | present | ✅ |
| `ConstantItem` / `StaticItem` (`static mut`) | `const`/`static`/`let` module-level; **`static mut`** (mutable global) absent | ✅ const/static; ❌ `static mut` gap (niche) |
| `ExternBlock` `extern { }` | `extern fn …;` decls only (no block grouping) | ⚠️ minor divergence |
| `MacroItem` (`macro_rules!`) | metaprog / `quote_*!` | ⚠️ divergence A3 |

## Statements (`statements.md`)

| Rust | Logos | Status |
|---|---|---|
| `LetStatement` `let pat (:T)? (= e (else block)?)? ;` | present, incl. no-annotation + tuple pat + let-else | ✅ |
| `ExpressionStatement` | present | ✅ |
| **assignment** (an *expression*: `Expr = Expr`, `Expr op= Expr`, place LHS) | **~17 statement productions** + general `place_assign_stmt <- atom ASSIGN expr` catch-all | 🔧 **over-enumerated** — biggest collapse target. Rust: 2 forms over a place expr. Logos parses via the catch-all but routes 16 specialized shapes to specialized sema. Collapse → one place-write sema path. Higher-risk; flagship generalization. |
| item-as-statement | present (nested fn, etc.) | ✅ |

## Expressions

Rust models all of these as one recursive `Expression`; Logos uses a
precedence tower (range→log→cmp→bit→shift→add→mul→cast→unary→postfix→atom) —
✅ equivalent. Form-by-form:

| Rust | Logos | Status |
|---|---|---|
| literals (char/str/raw-str/byte/byte-str/**c-str**/int/float/bool) | int/float/str/raw-str/char/byte-str/bool; **no C-string `c"…"`** | ✅ (C-string ⚠️ niche/divergence) |
| path / qualified-path expr | present (`::`-divergent) | ⚠️ |
| call / method-call / field / tuple-index / index | present | ✅ |
| **closure** `\|x\| expr` (expr body) AND `\|x\| -> T { }` | both work (verified) | ✅ |
| struct expr + `..base` | present | ✅ |
| array `[a,b]` / `[v; n]` | present | ✅ |
| tuple `(a, b)` / `(a,)` 1-tuple | present incl. `(a,)` (verified) | ✅ |
| **range exprs** `a..b`, `a..`, `..b`, `..`, `a..=b`, `..=b` (as VALUES) | only in for-loop head; `let r = 0..10` is a **parse error** (verified) | ❌ GAP — ranges aren't first-class value expressions. Needs Range/RangeInclusive/RangeFrom/… types + expr productions. Medium (stdlib + grammar). |
| if / if-let / **let-chains** (`if a && let P = e`) | if / if-let present; **let-chains** absent | ✅ core; ❌ let-chains gap (medium) |
| match (+ guard, + let-guard-chains) | present; let-guard-chains absent | ✅ core; ❌ guard let-chains (medium) |
| loop / while / while-let / for / labeled | present; `for PATTERN` ✅ (G-CONF-1) | ✅ |
| block / unsafe block / **const block** / **async block** | block, unsafe; const-block⚠️A2, async-block⚠️A4 | ✅ |
| await | ⚠️ A4 (fibres) | ⚠️ |
| break/continue/return (+ label/value) | present | ✅ |
| try `?` | present | ✅ |
| borrow `&`/`&mut`/`&&`/`&raw const/mut` | `&`/`&mut`/`&&`; `&raw` absent | ✅ core (raw-borrow niche) |
| underscore expr `_` (in assign LHS) | partial | ⚠️ verify |

## Patterns (`patterns.md`)

Rust uses full `Pattern` recursively in tuple / tuple-struct / slice / struct
element positions. Logos historically used `pat_binding` (bindings only) in some
element slots — the root of the recurring nested-pattern fixes — but the
element productions now route through `pat_single` with synth-guard machinery.
Verified working: `match p.x {1=>…}`, `W(a,b)`, nested `Some(Some(v))`,
`(a,(b,c))`, slice patterns. 

| Rust | Logos | Status |
|---|---|---|
| literal (incl. `-`) / ident (`ref`/`mut`/`@`) / wildcard / rest `..` | present | ✅ |
| reference `&`/`&&` `mut`? | PAT_REF present | ✅ |
| struct / tuple-struct / tuple / slice (full sub-patterns) | present (verified) | ✅ |
| grouped `( P )` | present (verified) | ✅ |
| **range patterns**: closed `a..=b` ✅; half-open `a..`, `..=b`, `..b` | closed inclusive ✅; half-open `a..` and `..=b` are **parse errors** (verified) | ❌ GAP — half-open range patterns |
| path pattern (const/enum unit by path) | present | ✅ |
| or-pattern at any depth | present (PAT_OR; fanned) | ✅ |

## Types

| Rust | Logos | Status |
|---|---|---|
| path type `Foo<…>` (`::`) | `.`/`::`-divergent | ⚠️ |
| ref `&'a mut T` / raw ptr `*const/*mut T` | present (verified `*mut T`/`*const T`) | ✅ |
| array `[T; N]` / slice `[T]` / `&[T]` | present | ✅ |
| tuple `()` / `(T,)` / `(A,B)` | present incl. `(T,)` (verified) | ✅ |
| never `!` / inferred `_` | present | ✅ |
| `dyn Bounds` (multi `dyn A + B`) + bare `dyn` | present (verified `&dyn A`, multi-bound parses) | ✅ |
| `impl Bounds` (multi) | `impl_type` single-bound `impl Trait<…>` | ⚠️ verify multi-bound `impl A + B` |
| bare fn `fn(A)->R` (+`unsafe`/`extern`/variadic/named params) | `fn_ptr_type` present | ✅ core |
| qualified `<T as Trait>::X` | present (G172-6) | ✅ |
| `ForLifetimes` HRTB on types | present | ✅ |

## Prioritized fix queue (gaps, generalize each)

1. **🔧 Assignment over-enumeration** (flagship). **STEP 1 DONE 2026-05-26 —
   compound-assign collapse (7 → 1).** All seven per-shape compound productions
   (`compound_assign`, `field_/chain_field_/index_/tuple_field_/deref_field_/
   field_index_compound_assign`) → ONE general `atom compound_op expr`
   (COMPOUND_ASSIGN, RECEIVER=place). lower_compound_assign: bare-VAR_REF fast
   path; else lower_place_compound_assign — `place = place op rhs` via a
   deref-write (read-twice, matching the old double-eval), or `op_assign(&mut
   place, rhs)` for an `*Assign` struct place. The ONE genuinely-different case
   (user `IndexMut` compound `g[i] += v`) is folded in as an INDEX_READ-on-struct
   branch (general addr-of can't dispatch IndexMut — the audit-predicted hard
   case). ~450 lines of specialised lowerings removed; named-place diagnostics
   preserved via render_place_node. Test: compound-assign-collapse-gconf.
   5227/5227. **STEP 2 (mapped 2026-05-26): the WRITE family** (field_write /
   index_write / tuple_field_write / chain_field_write / deref_field_write /
   field_index_write → place_assign). Materially harder than compound:
   `gen_lvalue_addr` already covers the SHAPES (field/tuple/chain/field-index/
   deref-field/array&slice index — proven by step-1's compound tests), so the
   work is (a) 2 genuine special folds the general addr-path lacks — DataRef
   ergonomic field-write (`p.field=v`→mut_ptr desugar) + user IndexMut/slice
   index-write; (b) diagnostic + mutability preservation — every write shape has
   fail-tests with shape-specific wording (overflow `'(*p).val': value X does
   not fit`, `*const`-write rejection, immutability), and place_assign currently
   does NEITHER a root-var mut-check NOR an overflow check (latent soundness
   gap). Plan: enrich place_assign (root-var mut-check + `*const`-write check +
   overflow check + named render_place_node diagnostics), fold DataRef +
   IndexMut, retire the 6 productions/lowerings, update the few write fail-test
   `.expected` to the unified named wording. Dedicated careful pass (central op);
   gate-driven iterate as step 1 did. NOT rushed at a session tail.
   **STEP 2 RE-ASSESSED 2026-05-26 (committed to it, did the deep analysis):
   the write family is NOT gratuitous duplication like compound was — each shape
   encodes genuinely-DIFFERENT validation/handling:** `deref_field_write` has
   ~8 pointer-specific diagnostics (`*mut`/`*const`/`&`/`&mut`, unsafe-ctx,
   non-struct pointee); `field_index_write` rejects `*const`-field writes
   (`b.data[0]=v`); `field_write` has the DataRef `mut_ptr` desugar; `index_write`
   has IndexMut + slice; `tuple_field_write` distinguishes tuple-STRUCT (`t.0` →
   field `"0"`) from a real tuple — and `gen_lvalue_addr`'s TupleIndex uses
   `tuple_llvm_type`, which FAILS for a tuple-struct, so routing it through
   place_assign would BREAK. A naive merge would RELOCATE all this into one giant
   function (+ heavy `.expected` churn + high regression risk on the central op),
   not REMOVE variant complexity — the opposite of the goal. **Verdict: true
   write unification needs a proper uniform place/lvalue SUBSYSTEM (uniform
   place-mutability + place-typing + autoref + IndexMut + tuple-struct), which is
   a real architectural project — the per-shape writers are the current
   functional stand-in for what Rust's borrow/place system does uniformly.**
   The compound collapse (step 1) was the genuinely-collapsible over-enumeration;
   the writes are deferred to that future place-subsystem project (or a narrow
   chain/tuple-only collapse with low payoff). Not a naive merge.
   **PLACE SUBSYSTEM — FOUNDATION DONE 2026-05-26 (8b1c8152).** Built the first
   piece: `check_place_writable` (uniform place-mutability: rejects writes
   through an immutable var / `*const` / shared-`&`, conservative) + an overflow
   check + named-place diagnostics, wired into lower_place_assign. This closes a
   real SOUNDNESS GAP (the general deep-nested place-write did no mut-check —
   `a[i][j]=v` on an immutable `a` was silently accepted) and makes the general
   place-write path do everything the per-shape writers' common checks do —
   the prerequisite for migrating them onto it. NEXT piece: `place_write_addr`
   that resolves a place to a `&mut T` address handling the special address
   forms gen_lvalue_addr lacks (IndexMut call, DataRef `mut_ptr`, tuple-struct
   field, slice) — once it exists the 6 per-shape writers route through the
   subsystem and retire. (Larger migration; do as a focused pass.)
   **MIGRATION ATTEMPT 2026-05-26 (chain-first) — REVERTED, instructive:** Tried the
   cheapest retirement: remove `chain_field_write_stmt` (+ `chain_path_id`/`chain_field_path`
   productions + `lower_chain_field_write`) so `a.b.c = v` falls through to `place_assign_stmt`,
   after generalizing `check_place_writable` to handle a raw-pointer ROOT (auto-deref through
   `*mut`/`*const`, replicating the chain writer's `*const` + "requires unsafe" rejects).
   Builds clean, but the GATE found **56 runtime miscompiles** (hermes/lforge: wrong exit codes,
   `lforge: manifest: top-level value must be a map`). ROOT: `gen_chain_field_write`
   (mlir_gen_stmt.cpp:1860) does **mid-chain pointer auto-deref** (a `*T` field is followed
   via load before GEPing the next segment, mlir_gen_stmt.cpp:1909-1947) and struct-type-name
   resolution that the general `gen_lvalue_addr`/`addr_of_temp` resolver does NOT replicate —
   so a chain through a pointer field (Hermes `DataRef`, embedded `*mut`) writes to the wrong
   address. CONCLUSION: the migration MUST go the other direction — first teach the general
   address resolver mid-chain pointer/DataRef auto-deref (the actual `place_write_addr` work),
   THEN retire the per-shape writers. Grammar-removal + fall-through is unsound until then.
   (Reverted to 88fc42a9; 5228/5228 green.)
   **PRECISE DIVERGENCE (diagnosed 2026-05-26, post-revert):** Two codegen-equivalence
   gaps separate the per-shape writers from the general `addr_of_temp`+`SDerefWrite` path:
   (1) mid-chain pointer auto-deref — actually OK: `gen_recv_struct` (mlir_gen.cpp:736-768)
   already loads a pointer field before descending, mirroring gen_chain_field_write:1953-1957.
   (2) **AGGREGATE STORE-BY-VALUE — the real gap.** `gen_chain_field_write` (mlir_gen_stmt.cpp:1973-1982)
   loads-then-stores the whole aggregate for ANY final field that is an `LLVMStructType` when the
   rhs is a `ptr` (covers tuples, embedded datatypes, fixed-array-as-struct, AND Struct kind).
   `SDerefWrite` (mlir_gen_stmt.cpp:527-543) only memcpys when the pointee KIND is `Struct`/`ZonedStruct`;
   any other aggregate-kind field falls to `coerce_int`+`StoreOp`, which stores an 8-byte pointer
   into a larger aggregate slot → silent heap corruption (the stdlib Map/Hermes internals that
   broke map_comp/match/lforge). **FIX for the sprint:** generalize `SDerefWrite`'s aggregate
   branch to trigger on `LLVMStructType` pointee (load-or-memcpy by value), not just Struct kind —
   then re-attempt the chain retirement, then the other writers. This is `place_write_addr`'s
   true prerequisite; bounded and codegen-local.
   **AGGREGATE-STORE FIX LANDED 2026-05-26 (7c33525a):** `SDerefWrite` now stores any
   `LLVMStructType`/`LLVMArrayType` pointee by value (memcpy), not just Struct/ZonedStruct kind.
   Green standalone (5228/5228).
   **CHAIN RETIREMENT RE-ATTEMPTED ON TOP — STILL 56 FAIL, re-reverted 2026-05-26.** New findings:
   - The `check_place_writable` VAR_REF branch MUST special-case a `*mut` ROOT (allow write THROUGH
     an immutable `*mut` binding without `let mut`, require `inside_unsafe_`, reject `*const`) —
     without it stdlib `*mut self`/`*mut`-let chain writes (`new_map`, `self.inner.strong`, rc.logos)
     are wrongly rejected as "assignment to immutable variable". (Branch is correct; keep for the sprint.)
   - With that branch, isolated repros of `*mut`→inline-embedded-struct→scalar AND `*mut`→pointer-mid-field→scalar
     BOTH PASS. So the surviving 56 (ALL hermes-stdlib + lforge, which depends on it) come from
     hermes REPRESENTATION SPECIAL-CASES the general place path doesn't replicate but
     `gen_chain_field_write`/`gen_recv_struct` do: **embedded `AnyVal` mid-fields** (`doc.root.raw`,
     document.logos:58 — AnyVal is special-cased u64-wrapper layout), **`RelPtr` `.offset` fields**
     (`arr.data.offset`, `m.keys.offset` across hbs_read/view/map/clone), and generic-container instances.
   - **NEXT SESSION:** pick ONE failing hermes fn (e.g. `document_set_root` / `Map<Bitmap,AnyVal>::init`),
     diff the MLIR/LLVM emitted by `gen_chain_field_write` vs the place path (`gen_lvalue_addr`+`SDerefWrite`)
     for its chain write, and teach `gen_lvalue_addr`/`gen_recv_struct` the missing AnyVal/RelPtr handling
     (this IS the `place_write_addr` work). Only then retire the chain writer. Chain writes WORK today
     via the dedicated writer — do NOT ship the grammar removal until the place path is representation-complete.
   **✅ CHAIN RETIREMENT LANDED 2026-05-26 (d5840c27), 5228/5228 with chain writer REMOVED.** The
   precise root (4th attempt, MLIR-traced on a minimal `#[zoned] struct Hdr { root: AnyVal }` +
   `h.root.raw = v` repro): a scalar-represented named field (AnyVal lowered as i32) is tagged with an
   EMPTY `struct_name` in the LLVM struct registry, so `gen_recv_struct`'s FieldRead branch hit the
   "not a struct" path and returned null → `EAddrOfTemp` fell back to a TEMP COPY and the write was
   silently dropped (NOT a layout/offset bug — the address, once resolved, is correct). Fix
   (3b4f739a + 7d47afd6): `gen_recv_struct` resolves an unrecorded-struct_name field's logical type
   via `all_struct_defs_` (the authoritative LIR struct def, same source `gen_chain_field_write`
   used) and treats a non-pointer named field as in-place. Plus the `check_place_writable` `*mut`-root
   branch and the SDerefWrite aggregate-by-value fix (7c33525a). Removed: `chain_path_id`,
   `chain_field_path`, `chain_field_write_stmt` productions, stmt dispatch, 114-line lowering, decl.
   SChainFieldWrite LIR node KEPT (closure-capture compound still uses it). Regression test:
   `chain_field_write_scalar_named`. **Lesson:** the earlier "hermes repr special-case" framing was
   right in spirit but the mechanism was narrower & cheaper than feared — a missing struct_name tag,
   fixed by consulting the LIR def. NEXT writers to retire (same place path, likely same-or-less work):
   `tuple_field_write`, `deref_field_write`, `field_index_write`, then `index_write`/IndexMut + DataRef.
   **Original investigation 2026-05-25 (NOT a pure-grammar collapse):** The ~17 grammar productions mirror ~9 distinct sema
   lowerings (`lower_assign`/`lower_field_write`/`lower_index_write`/
   `lower_tuple_field_write`/`lower_chain_field_write`/`lower_compound_assign`/
   `lower_destructure_assign`/`lower_place_assign`/inline DEREF_WRITE) that carry
   REAL differences the general `lower_place_assign` (addr_of_temp + deref_write)
   does NOT cover: user-defined `IndexMut` dispatch (`a[i]=v` on a struct),
   deeply-nested places (it rejects via `place_write_supported`), compound-assign
   (`+=`), and per-shape mutability diagnostics. So this is a **sema-unification
   sprint**, not a grammar tweak: first extend the general place-write path to
   subsume every case (IndexMut, deep-nest, compound, diagnostics), THEN retire
   the specialized productions+lowerings one at a time, each gated. High payoff,
   genuinely high risk — sequence deliberately, not as a quick win.
2. **Range expressions / slicing** — ✅ **DONE (slicing + half-open parse)**.
   Closed range VALUES (`let r = 0..10`) already worked (RangeI32/I64, with `use
   logos.lang.range`). Added: (a) the full range_expr family incl. half-open
   `a..`/`..b`/`..=b`/`..` (open side = missing LHS/RHS key); (b) **range-based
   slicing** `s[a..b]`/`s[a..]`/`s[..b]`/`s[..]`/`s[a..=b]` → sub-slice via a new
   `slice_get_range<T>(&[T], lo, hi) -> &[T]` stdlib helper (clamps bounds, so
   open ends pass 0 / i64::MAX). lower_index_read intercepts a RANGE_EXPR index;
   arrays decay to a slice (addr-of + array→slice coercion), `&[T]` sliced
   directly. FOLLOW-UPS: standalone open-ended range VALUES (`let r = 0..` —
   needs RangeFrom/To/Full structs), Vec/str range-slicing, `&a[..]` ergonomics.
3. ✅ **DONE — Default type parameters** `<T = Type>` / `<T: Bound = Type>`
   (structs + enums). Grammar: type_param ASSIGN alts store the default in the
   TYPE slot (positional `$3`/`$6` — works; the first attempt's failure was that
   `collect_struct` uses **`read_type_params`**, a DUPLICATE of
   `read_type_params_from` — both now read the default). resolve_type fills
   trailing params from defaults (substituting earlier args, `<T,U=T>`) before
   the arity check; no-default + too-few still errors cleanly. Test:
   default-type-params-gconf. Follow-up: defaults on fn/impl type-params, GATs.
   ~~ATTEMPTED + REVERTED note (superseded):~~ The sema side is straightforward: a `default_type`
   on TypeParam + fill trailing params in resolve_type's generic-type path
   (before check_type_arg_arity) substituting earlier args (`<T,U=T>`). But the
   GRAMMAR capture failed: adding `IDENT (COLON bounds)? ASSIGN type_ref` alts to
   `type_param`, neither `ITEMS: $...` (didn't capture the lone trailing
   type_ref) nor `TYPE: $3`/`$6` (positional) populated the slot — and the parse
   came out mis-structured (the preceding param `A` spuriously got `ITEMS`, the
   `B = i64` default landed nowhere). Root is in peg_gen's `$...`/positional
   capture semantics for an optional trailing rule-call after a `*` repetition in
   `type_param` (same family as the known struct-`$...` over-capture). Reverted to
   avoid degrading a clean parse-error into a confusing "expected N type args"
   semantic error. NEXT: a focused peg_gen pass — likely a dedicated
   `type_param_default <- ASSIGN type_ref` sub-rule with an explicit captured
   field, or fixing the `$...` collector to include lone trailing rule-calls.
4. **❌ Let-chains** `if a && let P = e` / `while …` / match-guard chains.
   **Assessed 2026-05-25:** more involved than "clean no-mono". `&&`/`||` live at
   `log_expr` (above `cmp_expr_ns`), so a let-chain condition operand must be
   `cmp_expr_ns` (no top-level `&&`) with `&&` parsed at the chain level. Two
   shapes:
   (a) **Full/general** — `if_conditions <- let_chain / expr_ns`, rewriting the
       core if/while condition; reroutes ALL `&&` conditions → regression-
       sensitive (touches every `if a && b`).
   (b) **Bounded/additive (recommended first)** — add alts that REQUIRE an
       explicit `let` (`if let P = e && <expr_ns>` and `if <cmp_ns> && let P =
       e …`), ordered before the existing if forms; pure-bool `&&` untouched
       (low risk). Desugar in lower_if: nest `if let P=e { if cond {THEN} else
       {ELSE} } else {ELSE}` (else duplicated — only one path runs). Covers the
       common 1-let case; arbitrary mixing is a follow-up. Medium; do with a
       fresh careful pass (central lower_if).
   ✅ **DONE (if-let, bounded form b)** — clean match-guard desugar: `if let PAT
   = e && <cond>` → `match e { PAT if <cond> => THEN, _ => ELSE }` (no else
   duplication). `&&` parsed at `cmp_expr_ns` level; trailing cond is full
   `expr_ns` (multi-`&&` ok). Top-level + tuple/struct-nested payload bindings
   visible to the guard (guard-prologue re-extract); nested ENUM-VARIANT binding
   in the guard rejects cleanly (no miscompile). Additive — pure-bool `if a && b`
   untouched. FOLLOW-UPS: `while let … && …`, match-guard chains, cond-first
   (`if cond && let P = e`), nested-variant-binding-in-guard.
5. ~~**Half-open range patterns** `a..`, `..b`, `..=b`~~ ✅ **DONE** (3097f848) —
   open side clamps to scrutinee type min/max (closed range at the boundary).
   Nested-in-payload half-open (`Num(1..)`) still a follow-up.
6. Lower priority: `_ = expr` underscore-assign, `static mut`, unions, C-strings, `&raw`.

**Excluded (DIVERGENCE — future non-Rust package model):** `UseDeclaration`
(use-tree, `as`, glob, `::`-paths), `Visibility` (`pub(crate)`/`pub(in …)`),
modules (`mod`), extern-crate. These are NOT Rust-conformance items — they will
follow Logos's own package model, designed later.

Done: **G-CONF-1** `for PATTERN in iter` ✅.
