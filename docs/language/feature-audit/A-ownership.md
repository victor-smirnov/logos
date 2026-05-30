# Category A — Ownership (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`)

7 features audited: 4 OK, 2 WARN, 1 GAP-leaning. Move/Copy/Drop/Borrow/Reborrow have Rust-aligned names and largely conformant semantics. Variance is implemented with the canonical 4-point lattice. Lifetimes are the weakest leg: elision is point-checked, named regions only loosely propagate, HRTB is captured at the bound but not subtyping-checked end-to-end.

---

## 1. Move

**Rust nomenclature:** *move semantics* / "value moved from place" / E0382 "use of moved value" (spec: `destructors.md`, `expressions.md` §value vs place).

**Logos nomenclature:** `is_move_type(t)` predicate; per-variable `VarState::moved` / `moved_line`; partial-move map `moved_fields`; sema helpers `mark_moved_expr` / `track_args_moved` (`src/compiler/sema_expr.cpp:465,5457`); the shared aggregate-recursion skeleton is `moveclass::is_move_type` (`include/logos/compiler/move_classify.hpp:30-45`); user-visible diagnostics: `"use of moved variable '{}'"` (`src/compiler/sema_expr.cpp:465`) and `"use of moved value '{}' (moved on line N)"` (`src/compiler/borrow_check.cpp:682`).

**Match verdict:** OK — predicate name, state field (`moved`), and diagnostic strings match Rust's "moved value" idiom.

**Implementation pointer:** Two phases share the recursion skeleton; the leaf rules diverge by design:
- Sema (live `TypePool`): `src/compiler/sema_expr.cpp:5457` (`track_args_moved`), `src/compiler/sema_expr.cpp:465` (use-after-move report).
- Borrow check (post-mono): `src/compiler/borrow_check.cpp:108-138` (`is_move_type` leaf gates `&mut T` → move, structs Copy-set lookup, enums any-payload-moves).
- Shared skeleton: `include/logos/compiler/move_classify.hpp:30-45`.

**Interactions check:**
- Copy — OK. Falls through to `struct_is_move` / `enum_is_move` which gate on `ts.copy_types` (`borrow_check.cpp:122-127`). Copy⊥Drop invariant respected.
- Drop — OK. Move suppresses Drop on the source (`sema.cpp:2772`, the `moved_vars_` skip in `collect_drops`).
- Borrow — OK. `borrow_check.cpp:689` rejects `cannot move '{}' while it is borrowed`.
- `let` / assignment — OK. `sema_stmt.cpp:233` synthesises an SDrop for the LHS prior value; drop-before-replace landed (DIVERGENCES B8).
- Closure (capture modes) — OK. `borrow_check.cpp:1119-1138` handles `move` vs non-`move`; RFC-2229 field-precise capture in (commit 70526c97 / 20c817d5).
- Match (scrutinee move) — OK. `mark_match_scrutinee_moved` widened to PLACE scrutinees (51d2e29e, MEMORY.md B7).
- Function call (by-value args) — OK (`track_args_moved` at `sema_expr.cpp:2700,2944,3592`).
- Return — OK (consume flows through `collect_all_drops` at `sema.cpp:2781`).
- Struct / tuple field move (partial moves) — OK at the first field level (`borrow_check.cpp:1029-1037`); deeper paths (a.b.c) collapse to the outermost segment (`sema.cpp:2747-2751`).
- Pattern bindings — OK. `bind_pattern_ref` records moves; the `moved_vars_` set carries dotted paths.
- `unsafe` (raw ptr semantics) — partial. `Ptr` kind exists (`sema.hpp:52`); raw-ptr deref-as-move is not specially tracked (correct — raw ptr is Copy in Rust).

**Gaps / debt:**
- Partial-move tracking is one-level (B78); nested-field consume aliases the parent (`sema.cpp:2747-2751`). Probably fine until a user moves out of `a.b.c` and later reads `a.b.d`.
- No surface test for `mem::replace`-style move-out-and-write-back idioms in `stdlib/mem/`.
- The sema-side leaf and the borrow-check-side leaf both treat `&mut T` as move, but the latter is asymmetric vs. the sema rule that uses generic-bound `T: Copy` (`compute_auto_copy_types`); behaviourally fine but worth a one-line comment.

---

## 2. Copy

**Rust nomenclature:** `Copy` marker trait (`special-types-and-traits.md` §Copy). Built-in for primitives, `&T`, function pointers; auto-derivable; mutually exclusive with `Drop`.

**Logos nomenclature:** `"Copy"` is a built-in trait name (recognised by string match in `sema_collect.cpp:2454,754,3016`). Storage is `SemaChecker::copy_types_` (set of struct names) plus the post-mono mirror `TypeSets::copy_types` (`borrow_check.cpp:50`). Auto-Copy is `SemaChecker::compute_auto_copy_types` (`sema.cpp:2509`). Trait-bound check `T: Copy` lives in `current_type_bounds_` (DIVERGENCES B1).

**Match verdict:** OK — Logos uses the Rust spelling exactly (`"Copy"`); the auto-Copy promotion mirrors `#[derive(Copy)]` for scalar-only structs.

**Implementation pointer:**
- Manual impl collection: `src/compiler/sema_collect.cpp:3016-3020` (rejects `unsafe impl Copy` for the safe built-in).
- Structural auto-Copy: `src/compiler/sema.cpp:2509-2580` (`compute_auto_copy_types`).
- Per-kind triviality table: `src/compiler/sema.cpp:2511-2526` (primitives + `&T` + `&mut T` + FnPtr + payload-less enum).
- Generic-bound: `is_move_type` consults `current_type_bounds_` so `T: Copy` no longer treated as move (B1 done).

**Interactions check:**
- Move — OK (mutually exclusive; see Move above).
- Drop — OK. `compute_auto_copy_types` excludes any struct with `Drop` impl (`sema.cpp:2531` `has_drop_impl`). Manual `impl Copy` for a struct with `impl Drop` is implicitly rejected by `compute_auto_copy_types` skipping it; explicit cross-impl rejection (errors message) is not surfaced — WARN.
- Auto-traits — N/A (Logos lacks Send/Sync auto-derivation; tracked under category H).
- Generics (`T: Copy`) — OK (B1 closed 2026-05-22).
- `#[derive(Copy)]` — WARN. Logos `#[derive]` is a metaprog handler (blessed divergence A3); auto-Copy is structural and covers the same surface but the spelling differs. Per-trait handlers `#[derive_copy]` exist (`stdlib/`).
- Primitives — OK (`sema.cpp:2513-2522`).
- References (`&T` Copy, `&mut T` NOT Copy) — OK (`sema.cpp:2520` includes `Ref` and `MutRef` as field-trivially-Copy at struct-membership level, but `borrow_check.cpp:118` correctly marks bare `&mut T` as MOVE).

  Note the asymmetry: a struct holding a `&mut T` field is auto-Copy promoted at `sema.cpp:2520`, but a bare `&mut T` value is move-tracked. This is a subtle WARN — Rust says a struct with a `&mut T` field is NOT auto-Copy (struct-as-whole has at least one non-Copy field). Likely a real bug.

- Function pointers — OK (`FnPtr`, `TaggedPtr` in trivial list).
- Closures — partial. No "auto-Copy if all captures Copy" rule in `sema.cpp:2509-2580`. Closures with only Copy captures should themselves be Copy in Rust — Logos doesn't promote.

**Gaps / debt:**
- `&mut T` as a struct field shouldn't promote the struct to Copy. Inspect `sema.cpp:2520`: `K::MutRef` is in the trivially-Copy list. This is a soundness bug.
- Closure auto-Copy when all captures are Copy is missing.
- No diagnostic for `impl Copy for X where X: Drop` (currently the silent skip in `compute_auto_copy_types` only blocks the structural path).

---

## 3. Drop / RAII

**Rust nomenclature:** Destructor / `core::ops::Drop::drop` / "drop glue" (spec: `destructors.md`).

**Logos nomenclature:** SDrop statement (`lir_mirror_emit_drop`); destructors emitted via `SemaChecker::make_drop_stmt` (`src/compiler/sema.cpp:2686-2762`); scope-end emission via `collect_drops` / `collect_all_drops` / `collect_drops_to_loop` (`sema.cpp:2764-2810`). Mlir-gen lowers SDrop in `gen_stmt_kind(SDropView)` (`src/compiler/mlir_gen_stmt.cpp:879`). Special sentinel `"__box_dyn__drop"` for owning `Box<dyn>` (`sema.cpp:2705`). `Drop` trait is recognised as a built-in by name match (`sema_collect.cpp:2454`); per-target lookup uses `impls_["Drop::" + target]` (`sema.cpp:2531`).

**Match verdict:** OK on naming (uses `Drop`); WARN on RAII *coverage* — drop-elaboration is recent (B8 done 2026-05-28) and partial-move drop suppression is one-level.

**Implementation pointer:**
- Drop trait collection: `src/compiler/sema_collect.cpp:2454` (built-in name match).
- SDrop emission: `src/compiler/sema.cpp:2686-2762` (`make_drop_stmt`).
- Scope-exit drops: `src/compiler/sema.cpp:2764` (`collect_drops`).
- Mlir-gen lowering: `src/compiler/mlir_gen_stmt.cpp:879` (`gen_stmt_kind(SDropView)`).
- Drop elaboration (conditional-init / drop flags): see commit `3e7bb5df` referenced from DIVERGENCES B8.
- ManuallyDrop wrapper: `stdlib/mem/manually_drop/manually_drop.logos:1-50`.

**Interactions check:**
- Move (suppresses Drop on source) — OK (`sema.cpp:2772` skip in `collect_drops`).
- Copy (mutually exclusive) — OK at the auto-Copy level (`sema.cpp:2531`); WARN: no explicit user-facing error for `impl Copy for X` when `X` is `Drop`.
- Variables / scopes — OK. Scope-exit dropping in declaration-reverse order (`sema.cpp:2768`).
- Struct / Enum (per-field/payload) — OK. `has_droppable_fields` walks fields; enum payload drops via mono-cloned drop fns.
- Trait `Drop` — OK (named-match recognised).
- `mem::ManuallyDrop` — OK. `stdlib/mem/manually_drop/manually_drop.logos:30` (`ManuallyDrop<T>`) provides the suppression wrapper.
- Pinning (`Pin<T>`) — GAP. No `Pin` type in stdlib (grep found only mono-pin "lazy pin" book-keeping at `mono.cpp:624`, unrelated). Combined with the async-as-fibres divergence (A4) Pin is intentionally absent — list as a documentation gap.
- Assignment (drop-before-replace + drop elaboration) — OK (B8 resolved 2026-05-28).
- Panic (drop on unwind) — partial. Unwinding tables exist but Drop-on-unwind invariants vs panic strategy aren't explicitly tested here.
- Const eval (no `Drop` in const fns) — N/A (const-fn replaced by metacall; A2).
- `?Sized` / DST (drop glue via vtable for `Box<dyn>`) — OK. `sema.cpp:2699-2708` emits `"__box_dyn__drop"` sentinel; mlir-gen calls vtable slot 0; owning `Box<CustomDst>` covered under B2 (commit ac85cb0e).

**Gaps / debt:**
- No surfaced diagnostic for `impl Copy for X` when X has `impl Drop`.
- `Pin<T>` absent — needs an entry in DIVERGENCES (currently buried inside A4 async).
- Drop-order spec for `match` arms is implemented but lacks an explicit test that exercises pattern-matching guards (the spec §destructors.scope.bindings.match-arm section).

---

## 4. Borrow `&T` / `&mut T`

**Rust nomenclature:** Shared / mutable reference (`types/pointer.md` §References, `expressions/operator-expr.md` §Borrow).

**Logos nomenclature:** `LogosType::Kind::Ref` and `LogosType::Kind::MutRef` (`include/logos/compiler/sema.hpp:53-54` — comments say "&T — shared reference (borrow-checked)" and "&mut T — exclusive mutable reference"). Grammar production `ref_type` (`tools/peg_gen/grammars/logos.peg:1465-1479`). AST codes `REF_TYPE` / `MUT_REF_TYPE`. Borrow-expression nodes are `EAddrOf` (named var `&x`) and `EAddrOfTemp` (sub-expression `&expr` / reborrows). Borrow checker = `src/compiler/borrow_check.cpp` (2387 LOC); see header comment lines 1-25.

**Match verdict:** OK — Logos type-kinds and grammar productions correspond directly to Rust's `&T` / `&mut T`.

**Implementation pointer:**
- Type kinds: `include/logos/compiler/sema.hpp:53-54`.
- Grammar: `tools/peg_gen/grammars/logos.peg:1465-1479` (also `1473-1479` for `&&T` double-ref via `AND` token).
- Borrow check (exclusivity + use-after-move + dangling): `src/compiler/borrow_check.cpp:108-2387`.
- Field-path borrows (B83): `borrow_check.cpp:161-167,219-231,260-294` (`extract_borrow_place`).
- Two-phase borrows (B82): `borrow_check.cpp:150-154` (state), `borrow_check.cpp:1000-1010` (`take_borrow` reservation path).
- Dropck (B87): `borrow_check.cpp:326-427`.

**Interactions check:**
- Lifetimes — partial. `param_lifetimes_` map (`borrow_check.cpp:320`) + return-lifetime check (`borrow_check.cpp:895-957`); see Lifetimes below.
- Reborrow — OK (`try_implicit_reborrow_mut` at `sema_expr.cpp:11200`).
- Mutability of bindings — OK (let-mut tracked separately).
- Borrow checker — OK (the whole file).
- Variance (`&T` covariant, `&mut T` invariant) — OK (`sema.cpp:6835-6852`).
- Patterns (`ref` / `ref mut`) — present but listed under category F audit.
- Coercions (`&mut → &`, `&T → *const T`) — partial. Downgrade reborrow OK (`sema_expr.cpp:11222-11230`); `&T → *const T` is not first-class — `Ptr` is `*mut`/`*const` raw pointer with `mut_ptr` flag.
- Method receivers — OK (`bind_method_receiver` at `sema_expr.cpp:11179`).
- DST (fat refs to `[T]`, `str`, `dyn`) — OK. `Slice`, `TraitObject`, `DstRef` kinds (`sema.hpp:60,62,89`).
- Closures (capture-by-ref) — OK.
- Two-phase borrows — OK (B82).
- Field-path borrows — OK (B83).
- NLL — partial. `BorrowRecord.holder` (`borrow_check.cpp:216`) and the "released after holder's last_use_line" rule give an NLL approximation; full region-based NLL still under region_infer scaffolding.

**Gaps / debt:**
- Slice mutability isn't tracked at the type level — DIVERGENCES B6 (`&[T]` and `&mut [T]` both canonicalise to `Kind::Slice`; index-write through `&[T]` not rejected).
- `&T → *const T` raw coercion not exercised — verify via test before claiming OK.
- Logos has no separate `&raw const` / `&raw mut` syntax (spec `type.pointer.raw.constructor`).

---

## 5. Lifetimes

**Rust nomenclature:** lifetime parameter / `'a` / `'static` / elision rules / outlives (`'a: 'b`) / HRTB `for<'a>` (`types.md` §References, `lifetime-elision.md`, `trait-bounds.md`).

**Logos nomenclature:** Lifetime token = `LIFETIME = /'[a-z_][a-z0-9_]*/` (`tools/peg_gen/grammars/logos.peg:445`); per-fn / struct / impl `lifetime_params: Vec<std::string>` (`sema_decl.cpp:439,860,968,970,1356`); outlives bounds in `info.lifetime_outlives` / `info.impl_lifetime_outlives` (`sema_collect.cpp:2100-2155,3283-3286`); HRTB binders in `trait_bound.hrtb_binders` (`sema.cpp:3341-3355`, `sema_collect.cpp:875-932`); region inference scaffolding at `src/compiler/region_infer.cpp` (858 LOC) and `RegionId`/`RegionConstraint::{Outlives,Contains}` (`include/logos/compiler/region_infer.hpp:82-91`); `'static` is detected by string match (`sema_decl.cpp:26,108,901,1008`) — no special internal kind.

**Match verdict:** WARN — Logos uses Rust spellings, but the *semantics* are partial: lifetimes are stored, region inference is scaffolding-only, and the borrow checker's lifetime-correctness path is elision-shaped point-checks (`borrow_check.cpp:895-957`), not a region-graph result.

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:445` (LIFETIME) and `1465-1479` (ref_type carries LIFETIME).
- Parameter collection: `src/compiler/sema_decl.cpp:439-441,860,925-926,968-970,1004-1005,1356-1357`.
- Outlives capture: `src/compiler/sema_collect.cpp:2100-2155,3283-3286` (`read_lifetime_outlives` + `_from`).
- Return-lifetime check (Rust elision rules): `src/compiler/borrow_check.cpp:895-957` (rule wording at 932-946).
- Region inferer (B70-72 scaffolding): `src/compiler/region_infer.cpp:28-101` (CFG build + fixed-point solver).
- Outlives transitive closure: `src/compiler/sema.cpp:3700` (note "lifetime model is elision-based at this layer").

**Interactions check:**
- Borrow — partial. `param_lifetimes_` indexes parameter-side lifetimes for the return-from-which-param check; field-of-self-aggregate has a deferred-to-typecheck escape (`borrow_check.cpp:910-923`).
- References — OK (`Ref` / `MutRef` carry lifetime in their schema slots).
- Generics (lifetime params) — OK at storage; elaborate transitive checks behind a real region engine still WIP.
- Lifetime elision — partial. Function-position elision is enforced post-hoc by `borrow_check.cpp:932-946` (the "single ref-param" rule); the parser does NOT inject elided lifetimes structurally — they remain empty strings, and downstream logic uses `lt.empty()` as the "elided" probe. Trait-object default object lifetimes (`'static` outside expressions, inferred otherwise) — no specific code paths grepped — likely GAP.
- HRTB (`for<'a>`) — partial. `for<'a>` parses to `HRTB_BINDERS` (grammar line 73), `hrtb_binders` stored on `trait_bound` (`sema.cpp:3338-3358`); satisfaction-checking in `mono_clone.cpp:4392-4466` is bijectivity-only ("universal-position + bijectivity"). The full subtype-via-substitution rule (`subtype.md` §subtype.higher-ranked) is approximated.
- Trait objects (`+ 'a` bound) — OK structurally (grammar `dyn_type` lines 1355-1366 + auto-bounds with `LIFETIME` (line 1354), and inner `'static` handling in `sema_decl.cpp:26`).
- Subtyping (`'a: 'b` outlives) — partial. Variance machinery composes correctly (`sema.cpp:6835-6852`); the actual `outlives_adj_` adjacency map (`borrow_check.cpp:341`) is built from declared `where`-clauses but not from inferred region-graph relations.
- Where-clauses (outlives) — OK at collection (`sema_collect.cpp:2132-2155`).
- Structs (`struct S<'a>`) — OK (`sd.lifetime_params = sinfo->lifetime_params` at `sema_decl.cpp:860`).
- Impls (`impl<'a> T for S<'a>`) — OK (`sema_decl.cpp:1352-1357`).
- `'static` — partial. Handled as a string `"'static"` (`sema_decl.cpp:26,108,901,1008`); no dedicated `Kind` slot. The default trait-object lifetime rule (`lifetime-elision.md` §trait-object) is not implemented.
- Dropck / region inference — partial. Dropck path at `borrow_check.cpp:326-427` (B87) covers the conservative "binding holds borrows of soon-dropped sources" check; full Polonius-style dropck eyes is WIP.
- Async (borrow-across-await) — N/A (async is the fibres divergence A4).

**Gaps / debt:**
- Region inference is solver-built but its output is not consumed by the lifetime conformance check (`region_infer.hpp:127` accessor exists; no caller in `borrow_check.cpp`).
- Default trait-object lifetime bound rule (`lifetime-elision.md` §trait-object) not implemented.
- HRTB subtype check is bijectivity-only, not full instantiation-based subtype (`subtype.md` §higher-ranked).
- `'static` as a structural lifetime kind (vs string match) — minor but a soundness foot-gun.
- No `'_` placeholder lifetime handling grepped (spec `lifetime-elision.function.explicit-placeholder`) — string match would treat it like any other named lt unless special-cased.

---

## 6. Reborrow

**Rust nomenclature:** *reborrow* — implicit re-take of a `&mut T` (or downgrade to `&T`) at coercion sites (`types/pointer.md` §Mutable references implicit, `type-coercions.md`).

**Logos nomenclature:** `SemaChecker::try_implicit_reborrow_mut` (`src/compiler/sema_expr.cpp:11200-11249`). Comment explicitly uses the term "reborrow" throughout (`sema_expr.cpp:11170-11178`). The wrapped expression has shape `AddrOfTemp(Deref(arg), is_mut)`; borrow-check recognises the reborrow shape via `lir_view::is_reborrow_shape` (`mlir_gen_dyn.cpp:1356`).

**Match verdict:** OK — name matches Rust, shape is canonical.

**Implementation pointer:**
- Sema-side wrap: `src/compiler/sema_expr.cpp:11200-11249` (`try_implicit_reborrow_mut`).
- Call-arg coercion driver: `src/compiler/sema_expr.cpp:11186-11198` (`coerce_arg_to_param`, dispatches `CFLAG_IMPLICIT_REBORROW`).
- Method receiver: `src/compiler/sema_expr.cpp:11179-11184` (`bind_method_receiver`).
- Let-binding coercion site: `src/compiler/sema_stmt.cpp:1722-1731` ("Rust auto-reborrows `&mut T` at COERCION sites in `let _: T = rhs`").
- Mlir-gen peephole unwrap: `src/compiler/mlir_gen_dyn.cpp:1350-1356` (recognises `is_reborrow_shape` and skips the temp).
- Mono passthrough: `src/compiler/mono_clone.cpp:2278` (reborrow vs rebind distinction for `&mut T`).

**Interactions check:**
- Borrow — OK. Reborrow takes a fresh borrow on the underlying var (`sema_expr.cpp:11203-11208`).
- Method receivers (auto-reborrow) — OK (`sema_expr.cpp:11179`).
- Call args (auto-reborrow) — OK (`sema_expr.cpp:11196`).
- Let-binding type ascription — OK (`sema_stmt.cpp:1722-1731`).
- Two-phase borrows — OK indirectly. Reborrow-shape recognition keeps borrow_check on the AddrOfTemp(Deref(VarRef)) path that integrates with B82.
- NLL release — OK. The reborrow's holder ties into the NLL release rule at `borrow_check.cpp:216`.
- Coercions — OK. Downgrade reborrow (`&mut → &`) at call-arg coercion (`sema_expr.cpp:11222-11230`) with `allow_downgrade` gate.

**Gaps / debt:**
- Reborrow only fires for `VarRef` / `FieldRead` / `IndexRead` roots (`sema_expr.cpp:11240-11244`). A reborrow of a method-call result that returns `&mut T` does not get the wrap — relies on the result already being AddrOfTemp.
- Receiver-position downgrade (`&mut T` arg → method on `&T` impl) is explicitly disabled (`sema_expr.cpp:11227 allow_downgrade=false`) — by design; document as an interaction note rather than gap.

---

## 7. Variance & subtyping

**Rust nomenclature:** Variance, subtyping (`subtyping.md`); the 3-point lattice {covariant, contravariant, invariant} (Rust also tracks "bivariant" for unused params internally).

**Logos nomenclature:** `enum class Variance : uint8_t { BiVar, Co, Contra, Inv }` (`include/logos/compiler/variance.hpp:31`). Helpers `variance_meet` (`variance.hpp:33`), `variance_compose` (`variance.hpp:40`), `variance_name` (`variance.hpp:48`). Per-def table `DefVarianceTable` (`variance.hpp:63`). Subtype relation `subtype(...)` declared at `include/logos/compiler/subtype.hpp:4-12` ("variance + outlives subtype relation"). Variance inference: `SemaChecker::compute_variances` (`src/compiler/sema.cpp:6916`), recursing through `variance_in_type` (`sema.cpp:6822`).

**Match verdict:** OK — naming aligned, full 4-point lattice (Logos adds `BiVar` for unused params, which matches rustc's internal model); per-kind rules match the Rust spec table at `subtyping.md` §subtyping.variance.builtin-types.

**Implementation pointer:**
- Lattice + composition: `include/logos/compiler/variance.hpp:31-65`.
- Recursive variance computation: `src/compiler/sema.cpp:6822-6912` (`variance_in_type`).
- Per-def inference fixpoint: `src/compiler/sema.cpp:6916-6990+` (`compute_variances`).
- Subtype relation: `include/logos/compiler/subtype.hpp:4-12` (declaration) + corresponding `.cpp`.

**Interactions check:**
- Lifetimes — OK. `&'a T` covariant in `'a` (`sema.cpp:6837`), `&'a mut T` covariant in `'a` (`sema.cpp:6845`), invariant in pointee (`sema.cpp:6848-6850`).
- References — OK (per the spec table at `subtyping.md` §variance.builtin-types row 1-2).
- Generics (param variance) — OK. `Struct`/`Enum`/`ZonedStruct` look up per-param variance (`sema.cpp:6868-6898`); default Co when unknown.
- Trait objects — partial. `subtyping.md` table says `dyn Trait<T> + 'a` is covariant in `'a` and invariant in `T`; Logos's `variance_in_type` switch (`sema.cpp:6830+`) has no `TraitObject` case — falls to `default → BiVar` (`sema.cpp:6909-6910`). This is a GAP.
- Function pointers — OK. Contra in args, Co in return (`sema.cpp:6900-6908`).
- HRTB — partial. `higher-ranked` subtype rule (`subtyping.md` §subtype.higher-ranked) — HRTB substitutability not implemented (see §5 Lifetimes Gaps).
- PhantomData — N/A. No `PhantomData` in stdlib grep; the canonical type-erasing helper. Logos generic-data parametricity tracked via type-args list, but no `PhantomData<T>` to mark variance via an unused param.
- DST coercions — partial. Unsize coercion (`T → [T]`, `T → dyn`) exists structurally; variance composition through fat-ref types not tested under variance audit.
- `*const T` / `*mut T` — OK. `K::Ptr` branch (`sema.cpp:6853-6857`) makes `*const` Co, `*mut` Inv in pointee.

**Gaps / debt:**
- `TraitObject` falls through to BiVar in `variance_in_type` — should be Co in lifetime, Inv in each type arg per the spec table. **Likely soundness bug** when a generic param appears under a `dyn Trait<P>` field.
- `UnsafeCell<T>` (Logos stdlib equivalent) — Logos's interior-mutability story is sketchier; if a Cell-like is implemented as a plain struct, the variance inferer will mark it Co not Inv. Cross-category gap with G (Memory / safety).
- `PhantomData<T>` analogue is not in stdlib — needed for downstream generic libs that want to force a specific variance.
- `subtype()` relation (declared in `subtype.hpp`) is used by `sema_stmt.cpp:5` but the call sites (variance + outlives at let-coercion / return) should be enumerated to confirm full coverage; not done in this audit.

---

## Cross-category gaps

- **B (Type system primitives) — DST mutability of slices.** `&[T]` and `&mut [T]` collapse to the same `Kind::Slice` (DIVERGENCES B6). Audit: Borrow §4. Owner: sema.
- **D (Generics & bounds) — `Sized`/`?Sized` interaction with HRTB and dropck.** Sized/?Sized exists (DIVERGENCES B2/B3 done); HRTB subtype rule is partial. Pre-condition for full lifetime/variance soundness.
- **F (Patterns) — `ref` / `ref mut` patterns** — assumed under category F audit; their interaction with borrow exclusivity should be checked there.
- **G (Memory / safety) — Interior mutability / `UnsafeCell`.** Variance audit hit the missing `UnsafeCell` Inv-pointee rule; the broader interior-mutability surface lives in category G.
- **H (Concurrency) — Send / Sync auto-traits, `'static` bound on dyn for thread move.** Same `'static` string-match weakness shows up here.
- **O (Other) — Pin** absent (Drop interaction §3); cross-cuts async (A4) and Drop.

---

## Recommended next moves

Single-session work items, ordered by user-visible / soundness impact:

1. **Fix the `&mut T`-field auto-Copy soundness bug** (`src/compiler/sema.cpp:2520`): remove `K::MutRef` from `field_kind_is_trivially_copy`; add a regression test that constructs `struct S { r: &mut i32 }` and asserts moving an `S` invalidates the source.
2. **Add `TraitObject` to `variance_in_type`** (`src/compiler/sema.cpp:6830+`): Co in outer `'a`, Inv in each type arg, following the `subtyping.md` table row 10. Test via `dyn Trait<i32>` field.
3. **Implement the default trait-object lifetime bound rule** (`lifetime-elision.md` §trait-object): currently no code path enforces `'static` outside expressions / inferred inside; sketch as a one-pass pre-mono normalisation in `sema_collect.cpp` near `dyn_type` resolution.
4. **Wire `region_infer.cpp` output into `borrow_check.cpp` lifetime conformance** (currently `borrow_check.cpp:895-957` is point-checks; the solver result is computed but unused). Even a minimal hookup that consumes the `Contains/Outlives` map would tighten elision diagnostics.
5. **Closure auto-Copy when all captures Copy**: extend `compute_auto_copy_types` to walk closures' capture lists; minor.
6. **Surface diagnostic for `impl Copy for X` when X has `impl Drop`** — currently silently dropped by `compute_auto_copy_types`'s structural path; user-visible improvement.
7. **Document Pin absence** in `docs/DIVERGENCES.md` (or note under A4 explicitly) so it stops being a phantom gap.
