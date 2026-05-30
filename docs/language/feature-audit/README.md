# Logos Feature Audit — Index

Generated: 2026-05-30. Spec basis: rust-lang/reference (local checkout at `/home/victor/cxx/reference`).

Per-category audits comparing the Logos compiler/stdlib against the Rust Language Reference, feature-by-feature. Each report enumerates feature naming, implementation pointers (file:line), neighbour interactions, gaps/debt, and single-session work items.

## Table of contents

- [A — Ownership](A-ownership.md) — Move, Copy, Drop/RAII, Borrow, Lifetimes, Reborrow, Variance.
- [B — Type system primitives](B-type-system-primitives.md) — primitives, Never, Tuple, Array, Slice, str, raw pointer, fn-item, fn-pointer, Closure, TraitObject, ImplTrait, Inferred `_`, layout/`repr`, coercions, DST.
- [C — Items](C-items.md) — fn, struct, enum, union, const/static, type alias, trait, impl, module, use, extern block, associated items.
- [D — Generics and bounds](D-generics-and-bounds.md) — type params, lifetime params, const params, where-clauses, trait bounds, HRTB, `Sized`/`?Sized`, GATs.
- [E — Expressions and control flow](E-expressions-and-control-flow.md) — let, block, if/if let, match, loops, closure, `?`, async/await, return, field/method/call, operator overloading, range, cast.
- [F — Patterns](F-patterns.md) — pattern kinds, refutability, binding modes.
- [G — Memory and safety](G-memory-and-safety.md) — interior mutability, memory model/atomics, variables.
- [H — Concurrency](H-concurrency.md) — Send/Sync, async fn / async block.
- [I — Modules, names, visibility](I-modules-names-visibility.md) — paths, visibility/privacy, name resolution/preludes/namespaces.
- [J — Macros and metaprogramming](J-macros-and-metaprogramming.md) — `macro_rules!` analogue, procedural macros analogue.
- [K — Unsafe](K-unsafe.md) — `unsafe` surface, UB list.
- [L — Attributes](L-attributes.md) — built-in attributes, `#[cfg]` / `cfg!`.
- [M — Const evaluation](M-const-evaluation.md) — const expressions / `const fn` / `const { }`.
- [N — FFI, linkage, ABI](N-ffi-linkage-abi.md) — `extern "ABI" fn` / blocks, inline assembly.
- [O — Other (Panic, Divergence)](O-other-panic-divergence.md) — Panic, Never `!` / divergence.

## Aggregate verdict counts

Counts are per audited feature (one feature ≈ one Rust-spec subsystem entry).

| Category | OK | WARN | GAP | Total |
|----------|----|------|-----|-------|
| A — Ownership | 4 | 2 | 1 | 7 |
| B — Type system primitives | 10 | 4 | 2 | 16 |
| C — Items | 6 | 4 | 2 | 12 |
| D — Generics and bounds | 3 | 4 | 1 | 8 |
| E — Expressions / control flow | 6 | 5 | 2 | 13 |
| F — Patterns | 0 | 3 | 0 | 3 |
| G — Memory / safety | 0 | 2 | 1 | 3 |
| H — Concurrency | 0 | 1 | 1 | 2 |
| I — Modules, names, visibility | 0 | 3 | 0 | 3 |
| J — Macros / metaprogramming | 0 | 2 | 0 | 2 |
| K — Unsafe | 0 | 2 | 0 | 2 |
| L — Attributes | 0 | 2 | 0 | 2 |
| M — Const evaluation | 0 | 1 | 0 | 1 |
| N — FFI / linkage / ABI | 0 | 1 | 1 | 2 |
| O — Other (Panic / Divergence) | 0 | 2 | 0 | 2 |
| **Total** | **29** | **38** | **11** | **78** |

OK 37%, WARN 49%, GAP 14%. The dominant verdict is WARN: the feature lands and works for the common case, with named edges, naming drift, or partial enforcement that Rust spec considers load-bearing.

## Top cross-category findings

Ordered by how many category audits each item touches, then by soundness/parity impact.

1. **Named-lifetime region inference is not consumed at borrow-check / variance / dropck**. `region_infer.cpp` is scaffolding only; declared `'a: 'b` outlives, `T: 'a`, HRTB binders, default trait-object lifetime rule (`'static` outside expr / inferred inside) all parse but do not flow into `borrow_check.cpp`'s lifetime-conformance path. Categories: **A** (Lifetimes/Variance), **D** (lifetime params, HRTB, outlives), **B** (TraitObject variance fall-through to BiVar), **F** (binding-modes ↔ borrow), **G** (`'static` storage class).
2. **`#[repr(...)]` attribute family entirely missing**; layout is hard-coded Rust-default. Blocks FFI conformance for `repr(C)` / `repr(transparent)` / `repr(packed)` / `repr(uN)` enums; the imported `#[repr(u32)]` clauses are silently dropped. Categories: **B** (type layout), **C** (Struct/Enum/Union), **L** (attributes), **N** (FFI), **K** (UB list around layout).
3. **`UnsafeCell` is not a lang-item; auto-trait `!Sync` for `Cell`/`RefCell` not derived**. Borrow checker permits `&T → *mut T` writes inside any `unsafe { }` indistinguishably from a real `UnsafeCell`. Variance pass does not treat `UnsafeCell<T>` as invariant in `T`. Categories: **G** (interior mutability), **A** (Variance), **H** (Send/Sync of Cell), **K** (UB validity rules).
4. **Auto-trait machinery has known holes that propagate**: (a) closures conservatively `!Send`/`!Sync` (`sema_auto_trait.cpp:199-201`); (b) `dyn Trait + Send` parses but is *informational only* — no enforcement at unsize coercion; (c) `Arc<T>` lacks `unsafe impl Send/Sync` and the structural rule rejects it because of its raw `*mut` field. Categories: **H** (Send/Sync), **B** (closure types, trait objects), **G** (`Sync` on statics), **A** (closure auto-Copy parallel).
5. **`Eq`/`Ord` ↔ `PartialEq`/`PartialOrd` naming inversion**. Logos stdlib's `Eq` plays Rust's `PartialEq`, `Ord` plays `PartialOrd` (`<`/`<=`/`>`/`>=` dispatch through `Ord::lt`). Ported `impl PartialOrd for X` does not bind. Categories: **E** (operator overloading), **F** (match guard `==`), **C** (trait names).
6. **Pattern / refutability factoring is duplicated and ad-hoc**: at least 3 sites (`mlir_gen_stmt.cpp:3521`, `mlir_gen_expr.cpp:3877`, `sema_stmt.cpp:990-1003`) each encode their own irrefutability check. `let`-destruct accepts a narrow hand-listed shape set; fn-params accept only `IDENT` / `mut IDENT` / `(pat,…)`; const-as-pattern is special-cased per literal kind. Categories: **F** (patterns), **C** (fn-params, const items), **E** (let / let-else / match guards).
7. **`let-chain` / multi-`&&` chains capped at one segment** across `if let`, `while let`, and `match` guard positions. `let`-in-guard absent. Categories: **E** (if, match, loops), **F** (refutability composition).
8. **Atomics ship as stdlib API but the `Ordering` arg is discarded — every operation lowers to `seq_cst` on x86, unsoundly across non-TSO targets**. Atomics do not use `UnsafeCell<T>` for their storage. No compiler intrinsic; no memory model document. Categories: **G** (memory model / atomics), **H** (Send/Sync of atomics), **B** (`Ordering` pattern-match), **N** (replacing extern-asm with LLVM intrinsics), **K** (UB `undefined.race`).
9. **`static` / `static mut` collapsed to `CONST_DEF`**: `static NAME: T = expr;` parses but uses the const-inline lowering (no stable address, no `'static` storage anchor); `static mut` not in the grammar. Soundness foot-gun for `&STATIC` lifetime. Categories: **C** (items), **G** (variables/storage), **M** (const vs static distinction), **K** (`unsafe extern { static FOO; }`), **A** (`'static` lifetime resolution).
10. **Built-in attribute coverage is narrow + grammar restricts attributes to top-level items**. Missing: `#[repr]`, `#[inline]`, `#[non_exhaustive]`, `#[must_use]`, `#[deprecated]`, `#[allow]/#[deny]/#[warn]/#[forbid]/#[expect]`, `#[track_caller]`, `#[link*]`, `#[panic_handler]`, `#[doc = "…"]`, `#[unsafe(attr)]` wrapper, tool attributes. Outer attrs do not parse on fields, variants, trait/impl items, match arms, blocks, statements, fn params, generic params. `#[cfg(...)]` and `#[cfg_attr]` work for the single-arg case but the structured attribute path lacks `all/any/not` combinators (asymmetric vs `cfg!()`), and `cfg_attr` wrapped-attr activation is a stub. Categories: **L** (attributes), **C** (items grammar), **K** (`#[unsafe]` wrapper), **N** (link*, no_mangle siblings), **O** (`#[track_caller]`, `#[panic_handler]`), **B** (`#[repr]`), **F** (`#[non_exhaustive]`).

## Recommended next moves (ranked, deduplicated)

Each item is sized for a single working session unless noted. Cross-reference the per-category report for the exact file:line plumbing.

### Tier 1 — soundness / parity bugs (fix now, generalize)

1. Remove `K::MutRef` from `field_kind_is_trivially_copy` so a struct holding `&mut T` does not auto-promote to `Copy` (`A`).
2. Wire `dyn_auto_bounds` (`+ Send` / `+ Sync` / `+ 'a`) enforcement at the unsize-coercion site; currently informational-only (`H`, `B`).
3. Add `unsafe impl<T: Send + Sync> Send/Sync for Arc<T>` in `stdlib/mem/sync/arc.logos` (one-line fix; otherwise `Arc<i32>: !Send`) (`H`).
4. Add `TraitObject` arm to `variance_in_type` so `dyn Trait<T>` is Co in `'a`, Inv in each type arg, instead of falling through to BiVar (`A`).
5. Tighten Never coercion: `Never → T` only; reject `T → Never` (currently bidirectional at `sema.cpp:1619`) (`B`, `O`).
6. Generalise the name-keyed `callee == "panic"` divergence carve-outs at `sema_expr.cpp:11899-11905` and `:12057-12095` to use `is_divergent_call` (`O`).
7. Tighten the bare-key fallback tier in `lookup_qualified_` (`sema_impl.hpp:2432`) so the pub check isn't bypassed for non-host items (`I`).
8. `loop {}` with no `break` should type as `!`/`Never` instead of `Void` (`E`).
9. Implement chained auto-deref so `match &&Some(x) { Some(x) => … }` binds correctly through both `&` layers (`F`).
10. `let-else` divergence check: assert the else block ends in a hard terminator / `Never` (`E`).

### Tier 2 — high-impact stdlib / surface gaps

11. Ship missing `#[derive_<trait>]` stdlib handlers: **Debug** (highest impact for coretest imports), **PartialEq/Eq**, **Default**, **Hash**, **PartialOrd/Ord**, **Copy**. One per session (`J`, `L`).
12. Add `unreachable!()` / `todo!()` / `unimplemented!()` `#[fn_macro]` wrappers in `stdlib/std/fmt/fmt.logos` (`O`).
13. Rename / alias `Eq`/`Ord` ↔ `PartialEq`/`PartialOrd` so ported `impl PartialOrd for X` binds (`E`, `C`).
14. Add `Range`/`RangeFrom`/`RangeTo`/`RangeFull`/`RangeInclusive`/`RangeToInclusive` generic stdlib types (today only `RangeI32`/`RangeI64`) (`E`).
15. Re-base `?` on a real `Try` / `FromResidual` trait surface; retire the hardcoded `Ok`/`Err`/`Some`/`None` name match (`E`).
16. Implement `DerefMut`-driven method autoderef for `&mut self` methods on smart-pointer receivers (`E`, `B`).
17. Add `AtomicUsize` / `AtomicIsize`; replace `extern fn logos_atomic_*` with MLIR atomic intrinsics driven by `Ordering` (`G`, `H`, `N`).

### Tier 3 — grammar / AST refactors that unblock parity

18. Add `let-chain` multi-`&&` support to `if_expr` / `while_stmt` / `match_arm` via a shared `LetChain` non-terminal; let-in-guard (`E`).
19. Add `Kind::InferredType` + grammar `_` alt in `type_ref`, enabling `let x: Vec<_> = …` (`B`).
20. Add `Kind::FnItem` distinct from `Kind::FnPtr`; coerce on use; fixes `if cond { foo::<i32> } else { foo::<u32> }` (`B`).
21. Slice mutability — add a `mut` bit to `Kind::Slice`, reject `a[i] = v` through `&[T]`, coerce `&mut [T] → &[T]` (`A`, `B`).
22. Hoist `is_refutable(Pat, ScrutTy)` into a single canonical predicate; drive `let` acceptance + `if let`/`while let` warnings + `match` shortcut (`F`).
23. Generalise fn-params to accept any irrefutable pattern (today only `IDENT` / `mut IDENT` / `(pat,…)`) (`F`, `C`).
24. Split `static` from `const` (distinct AST node, stable address, `'static` storage anchor); land `static mut` as a separate kind (`C`, `G`, `M`).
25. Introduce `PAT_PATH` + general constants-as-patterns; structural-equality check (`F`).
26. Split `PAT_VARIANT_DATA` → `PAT_TUPLE_STRUCT` + `PAT_STRUCT_VARIANT`; split `PAT_WILD` → wildcard + ident (`F`).
27. Split `Kind::Slice` → `Kind::SliceRef` (cosmetic, plus split `Kind::Str` from `Kind::Slice(u8)`) (`B`).
28. Add `union` item (`KW_UNION`, `union_def`, `LUnionDef`); even parse-only unblocks Rust imports that mention `union` (`C`, `B`, `K`).
29. Add `extern { … }` block surface + ABI string literal (`"C"`, `"system"`, `"C-unwind"`); ABI tag on `Kind::FnPtr` (`N`, `B`).
30. Add `#[repr(C)]` / `#[repr(transparent)]` / `#[repr(uN)]` parser + plumb into `layout_of` (`B`, `C`, `L`, `N`).
31. Allow annotations on struct fields, enum variants, trait items, impl items (grammar widening + new `AttrTarget` variants) (`L`, `C`).

### Tier 4 — semantics / passes

32. Wire `region_infer.cpp` output into `borrow_check.cpp` lifetime conformance; implement default trait-object lifetime rule; full HRTB instantiation subtype (`A`, `D`).
33. Definite-assignment analysis pass over CFG so `let x: T;` followed by partial init is a compile error (`G`).
34. `UnsafeCell` lang-item: variance Inv carve-out, auto-`!Sync` for any struct containing it; rename `pub enum Never {}` → `Infallible` (`G`, `O`, `A`, `H`).
35. Object-safety enforcement walk for `items.traits.dyn-compatible.*` (no Sized supertrait beyond opt-in, no GAT, opaque return, etc.) (`C`, `D`).
36. Closure auto-trait propagation: walk capture types in `sema_auto_trait.cpp` instead of conservatively returning `false` (`H`, `B`).
37. Symmetric `#[cfg]` combinator path so the structured attribute supports `all/any/not` like `cfg!()` does; activate `#[cfg_attr]` wrapped attributes (`L`).
38. Close `K10-co-06` — thread a `ConstResolver` seam through `ctfe::do_eval` so `metacall { N }` folds path-to-const references (`M`).
39. Unify `is_const_evaluable` with `ctfe::do_eval` so the const-item shape gate and the evaluator share one source of truth (`M`).
40. `!`-fallback inference rule (Rust 2024 `divergence.fallback`) for inference vars unified only against `Never` (`O`, `B`).

### Tier 5 — documentation hygiene

41. Single-pass `docs/DIVERGENCES.md` augmentation: add/clarify rows for the blessed model items this audit surfaced as undocumented — `package` vs `mod` + dotted-`.` separator, variadic tuple `(A...)`, `dyn Fn*(...) → Kind::Closure` collapse, `I24/U24/I56/U56` widths, `Void` kind, no `repr`, no Union, no `static mut`, `extern fn` block-form absent, `panic` is abort-only, `catch_unwind` signature divergence, atomic-`Ordering` ignored.
42. Stand up `docs/language/undefined-behavior.md` mirroring `behavior-considered-undefined.md` section-by-section with one-line "enforced/partial/unenforced" per anchor (`K`).
43. Add a "Migrating from `macro_rules!`" section to `docs/language/reference/macros.md`; catalog the prelude macro builtins (`J`).
44. Harmonise `annotation` ↔ `attribute` naming in grammar / AST / diagnostics (`L`).

---

For per-feature detail (Rust-spec citation, exact Logos file:line, interaction edges, gap rationale), open the corresponding category report linked in the [Table of contents](#table-of-contents).
