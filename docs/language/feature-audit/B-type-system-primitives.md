# Category B — Type system primitives (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`)

16 features audited: **10 OK**, **4 WARN**, **2 GAP**. The primitive table, Tuple, Array, Slice, str, raw pointers, Closure, TraitObject and DST are all present and named in line with Rust. `Never` is implemented but only as a coercion sink (no exhaustiveness use). `repr(...)` is absent — layout is hard-coded Rust. Function-item ZST type is collapsed into FnPtr — a deliberate simplification with a real soundness consequence in `if`/`match` unification.

---

## 1. Primitive types (`bool`, integer, float, `char`, unit)

**Rust nomenclature:** `bool`, `u8..u128`, `i8..i128`, `usize`/`isize`, `f32`/`f64`, `char`, unit `()` (spec: `types/boolean.md`, `types/numeric.md`, `types/char.md`).

**Logos nomenclature:** `LogosType::Kind::{Bool, U8, I8, U16, I16, U24, I24, U32, I32, U56, I56, U64, I64, U128, I128, Usize, Isize, F32, F64, Char, Void}` (`include/logos/compiler/sema.hpp:48-77`); unit is *also* the empty-`Tuple` kind via `unit_type` grammar (`tools/peg_gen/grammars/logos.peg:1543`). Name lookup at `src/compiler/sema.cpp:2203-2224`. The literal-widening sentinels are `Kind::IntLit` / `Kind::FloatLit` (`sema.hpp:64-65`).

**Match verdict:** OK — names match Rust exactly, with the *additions* `I24/U24/I56/U56` (Hermes-tag-aligned widths, blessed Logos extension) and `Void` (used for `fn() {}` return). `usize/isize` are distinct from u64/i64 via `Kind::Usize/Isize` exactly as Rust requires (`sema.cpp:2220-2221`); target pointer width is `g_target_pointer_bits = 64` (`sema.hpp:43`).

**Implementation pointer:**
- Type kinds: `include/logos/compiler/sema.hpp:48-77`.
- Name resolution: `src/compiler/sema.cpp:2203-2224` (`prim_by_name`).
- Display: `src/compiler/sema.cpp:1768-1793` (`type_name`).
- Layout: implicit (no explicit primitive layout table; sizes baked into `mlir_gen.cpp`'s `layout_of`).

**Interactions check:**
- Copy — OK. All primitives in the trivially-Copy bucket (`sema.cpp:2511-2526`).
- Coercions (integer widening) — OK. `can_widen_int` at `sema.cpp:1644` permits safe widening (`u32→i64`, `u8→u32` etc.); IntLit defaults to any integer (`sema.cpp:1620`).
- Pattern literals — OK (literal patterns at `Kind::IntLit/FloatLit/Bool/Char` handled by `gen_match`).
- Operator overloading (built-in ops bypass) — OK. Built-in arithmetic is special-cased in `sema_expr.cpp:1799-1820`.
- Const eval — n/a (intentional divergence: const-eval is via metacall, project_no_const_eval).
- Numeric suffixes (lexer) — partial. `i32/u64/f32` suffixes are lexed; `123u24` extension widths handled via IntLit→target widening.
- Repr (size/align fixed) — WARN. Rust spec table `i128 may have align 4 or 8`; Logos's `layout_of` codifies a single platform — no `#[repr(align(N))]` override.

**Gaps / debt:**
- `Kind::Void` survives as "no return value" but isn't a Rust type — keep it internal; do not expose at the type-name layer (currently `sema.cpp:2223` maps `"void"` to `Kind::Void` — a hold-over). Rename internally to `Kind::ZeroReturn` or strip the surface lookup.
- `I24/U24/I56/U56` are blessed Hermes-aligned divergences but should be flagged in DIVERGENCES.md (no entry today; grep confirms).

---

## 2. Never type `!`

**Rust nomenclature:** the never type `!`; "diverging expression"; subtypes every type (`types/never.md`, `divergence.md`).

**Logos nomenclature:** `LogosType::Kind::Never` (`include/logos/compiler/sema.hpp:99-106`); type-name lookup `prim_by_name("!")` (`src/compiler/sema.cpp:2224`); display "!" (`sema.cpp:1900`); coercion subtype hook (`sema.cpp:1618-1619`); `if`-branch unification keys off Never (`sema_expr.cpp:12030, 12125-12127`).

**Match verdict:** OK on naming and subtype semantics.

**Implementation pointer:**
- Kind + display: `include/logos/compiler/sema.hpp:99`, `src/compiler/sema.cpp:1900`.
- Subtyping (Never→T accepted both directions): `src/compiler/sema.cpp:1618-1619`.
- If/then/else convergence keys on Never: `src/compiler/sema_expr.cpp:12082, 12125-12127`.
- Binary-op handling treats Never operand as Never result: `src/compiler/sema_expr.cpp:1799-1813`.

**Interactions check:**
- Subtyping — OK (bidirectional permissive at `sema.cpp:1618`; the spec only requires Never→T, but symmetric permissiveness is harmless since Never never materializes a value).
- Divergence (`return` / `break` / `panic`) — OK. Diverging-expr types propagate via Never.
- `match` exhaustiveness — partial. Rust permits empty `match x {}` when scrutinee is `!`; grep for `Never.*exhaust` returns nothing in `sema_expr.cpp` — uninhabited-arm elimination not implemented.
- Variance — n/a (Never has no parameters).
- `loop {}` (infinite) — likely OK via `BREAK` divergence path (grep for `loop {}` in sema didn't show an explicit Never-injection; relies on no-break inference).
- Function return types — OK (`fn diverge() -> !` parses and is honoured for unification).
- Type inference — OK.

**Gaps / debt:**
- No exhaustiveness/uninhabited-arm rule for empty `enum`s or `match x: !`. Add a `is_uninhabited(t)` query and treat its match as exhaustive.
- Spec says Never can ONLY appear in fn return positions presently — Logos accepts `Never` more liberally (e.g. as expr type). This is *Logos-lenient*, not wrong, but flag it.
- The bidirectional coercion (line 1619: `to.kind() == Never` also returns true) is slightly looser than Rust — any value coerces *to* `!` in Logos, which Rust would reject. Soundness-significant: a value-producing expr in a `-> !` position should error.

---

## 3. Tuple

**Rust nomenclature:** tuple type `(T1, T2, ...)`, unit `()`, 1-ary `(T,)`; spec `types/tuple.md`.

**Logos nomenclature:** `LogosType::Kind::Tuple` (`include/logos/compiler/sema.hpp:59`); grammar production `tuple_type` (`tools/peg_gen/grammars/logos.peg:1551-1556`); unit is `unit_type` emitting `TUPLE_TYPE` with no items (`logos.peg:1543-1544`); ctor `make_tuple_type` (`src/compiler/sema.cpp:3791, 3803, 4970, 4979`); element access via `tuple_elems()` (`sema_expr.cpp:1326-1337`).

**Match verdict:** OK — names match.

**Implementation pointer:**
- Kind: `include/logos/compiler/sema.hpp:59`.
- Grammar: `tools/peg_gen/grammars/logos.peg:1551-1556`.
- Unit special-case: `tools/peg_gen/grammars/logos.peg:1543-1544` (zero-element TUPLE_TYPE).
- Element-wise compatibility: `src/compiler/sema.cpp:1658-1663`.
- Field access (`.0`): `src/compiler/sema_expr.cpp:1326-1337`.

**Interactions check:**
- Struct (similar memory) — OK. Tuple uses the same TypeRef → layout machinery as struct; `layout_of(Tuple)` lives in mlir-gen.
- Patterns (tuple-pat) — OK (referenced from category F audit; sema_expr handles tuple destructure).
- Field access (`.0`) — OK (`sema_expr.cpp:1326-1337`).
- Construction expr — OK (`sema_expr.cpp:1236-1283`, tuple literal lowering with `hint_tuple_type_`).
- Drop (per-element) — OK (TupleClass is in the move/drop recursion at `sema.cpp:1337, 1822, 2443`).
- Generics — OK (`(A, B)` accepts type params).
- Variance (per-element) — n/a in Logos (variance lattice exists but not surfaced for tuple).
- Variadic tuple `(A...)` — Logos extension (blessed?) at `logos.peg:1547-1552`. **No DIVERGENCES entry** — should be documented.

**Gaps / debt:**
- 1-ary tuple `(T,)` parses (`logos.peg:1555-1556`) but no tests exercise it under intersection with patterns / coercions.
- Variadic tuple `(A...)` is a Logos addition — needs a DIVERGENCES entry (currently undocumented blessed extension).
- Tuple field-name model uses positional indices (`.0`) matching Rust; no nominal "tuple struct" intersection issues observed.

---

## 4. Array `[T; N]`

**Rust nomenclature:** array type `[T; N]` with `N: usize` const-expr (`types/array.md`).

**Logos nomenclature:** `LogosType::Kind::Array` (`include/logos/compiler/sema.hpp:55`); grammar `arr_type` (`tools/peg_gen/grammars/logos.peg:1578`); ctor `make_array(elem, size, symbolic)` (`src/compiler/sema.cpp:3659, 5165, 5191, 5222`); array decay to `*const T` at `sema.cpp:1645-1648`.

**Match verdict:** OK.

**Implementation pointer:**
- Kind: `include/logos/compiler/sema.hpp:55`.
- Grammar: `tools/peg_gen/grammars/logos.peg:1578` (and short form `[T; N]`).
- Subst / clone: `src/compiler/sema.cpp:3642-3659`.
- Sema array-size literal: `sema.cpp:5165, 5191, 5222`.

**Interactions check:**
- Const params (`N`) — OK. `arr_size` field stores numeric `N`; `arr_size_var` carries symbolic N (`make_array(elem, n, symbolic)`).
- Slice decay `&[T; N] → &[T]` — OK (`sema.cpp:1730` accepts `&Array` to `Slice`); also array-to-`*const T` decay at `sema.cpp:1645-1648`.
- Patterns (array-pat) — covered under category F.
- Indexing (`Index`/`IndexMut`) — OK (bounds-checked via `gen_index` path).
- Drop (per-element) — OK (Array iteration in `collect_drops` recurses).
- Move (element-by-element; cannot move out of array of non-Copy) — partial. No explicit "cannot move out of array" diagnostic was found via grep — likely accepts and miscompiles. Soundness gap.
- `Copy` (auto if elem `Copy`) — OK (auto-copy `compute_auto_copy_types` walks elem; `sema.cpp:2511-2526`).
- Repr / layout (contiguous) — OK (`layout_of` in `mlir_gen_expr.cpp:4624`).

**Gaps / debt:**
- "Cannot move out of `arr[i]` of non-Copy `T`" diagnostic absent. Should be added before any test exercises move-from-array.
- `[T; 0]` ZST handling: no special-case in `layout_of` calls — likely works but untested.

---

## 5. Slice `[T]`

**Rust nomenclature:** DST `[T]`; only behind a fat pointer `&[T]`, `&mut [T]`, `Box<[T]>` (`types/slice.md`).

**Logos nomenclature:** `LogosType::Kind::Slice` (the fat pointer form, conflates `&[T]` and `[T]`-behind-ref) (`include/logos/compiler/sema.hpp:60`); bare unsized `[T]` is `Kind::UnsizedSlice` (`sema.hpp:77-82`) and canonicalises to `Kind::Slice` under `&`/`*` (`sema.cpp:3668-3669, 3701-3702`); grammar `slice_type` (`tools/peg_gen/grammars/logos.peg:1455-1462`); ctor `make_slice_type(elem, is_mut)` (`sema.cpp:2233, 3669, 3702, 3808, 4595, 4842, 4921`).

**Match verdict:** WARN — naming is reasonable but `Kind::Slice` represents `&[T]` (fat pointer), NOT `[T]` itself, which Rust would call `&[T]`. Logos's bare `[T]` is `UnsizedSlice`. Recommend renaming `Kind::Slice` → `Kind::SliceRef` for clarity, with `UnsizedSlice` keeping its name. The current naming is the historical source of soundness confusion (see B6 in DIVERGENCES — mut bit not tracked).

**Implementation pointer:**
- Kinds: `include/logos/compiler/sema.hpp:60, 77-82`.
- Grammar: `tools/peg_gen/grammars/logos.peg:1455-1462`.
- Box<[T]> owning form: encoded as `OwningKind::Box` in Slice's `const_val` (DIVERGENCES B3, done 2026-05-29 `8c49d59b`).
- Display: `src/compiler/sema.cpp:1829-1832`.

**Interactions check:**
- DST / `?Sized` — OK. `UnsizedSlice` is the bare-DST form; `Self: ?Sized` substitutes it (`sema_decl.cpp:1572-1582`).
- References (fat `&[T]`) — OK.
- Box (`Box<[T]>`) — OK (B3 closed).
- Indexing — OK.
- Patterns (slice-pat) — see category F.
- `IntoIterator` — present via stdlib.
- Lifetimes — partial. Slice carries an optional `LIFETIME` slot in the AST (`logos.peg:1456`) but downstream sema "currently doesn't enforce slice-with-lifetime distinctly" (per the grammar comment, `logos.peg:1453`).
- `str` (special slice of `u8`) — OK. `make_slice_type(u8_t())` is the `&str` form (`sema.cpp:2233`).

**Gaps / debt:**
- **Soundness, B6 open**: `&[T]` and `&mut [T]` both canonicalise to `Kind::Slice`; index-WRITE through a shared `&[T]` is NOT rejected (DIVERGENCES B6 — deferred until import test demands it).
- Lifetime annotation on slices is parsed-and-dropped — `&'a [T]` accepts but doesn't enforce.
- Rename suggestion: `Kind::Slice` → `Kind::SliceRef` to match its actual semantics (ref-to-slice fat-pair).

---

## 6. `str`

**Rust nomenclature:** `str` — DST; `&str` is a fat pointer; layout = `[u8]`; assumes UTF-8 (`types/str.md`).

**Logos nomenclature:** **No `Kind::Str`.** `str` is the name parsed by `prim_by_name("str")` (`src/compiler/sema.cpp:2232-2233`) which returns either `make_unsized_slice_type(u8_t())` or `make_slice_type(u8_t())` — i.e. `str` is just `[u8]`/`&[u8]` internally. String literals are typed as `&str` via the same Slice<u8> mechanism. The stdlib owning string `String` is its own struct.

**Match verdict:** WARN — Rust's `str` is a distinct DST that *happens* to share layout with `[u8]`; Logos collapses them. The UTF-8 invariant isn't enforced. Conformant on layout, divergent on type identity.

**Implementation pointer:**
- `src/compiler/sema.cpp:2229-2233` (`str` parses to Slice<u8>).
- No standalone `Kind::Str`.

**Interactions check:**
- DST — OK via UnsizedSlice<u8>.
- Slice (parallel design) — OK (literally Slice<u8>).
- `&str` literals — OK.
- UTF-8 invariants — GAP. No validation at the type level; calling a Logos `str` method with non-UTF-8 bytes is silent UB in Rust terms.
- Patterns (string-literal) — partial (string-literal pattern compares as &[u8] equal — needs grammar check).
- `String` heap-owning — OK (separate struct, lives in stdlib).
- Box (`Box<str>`) — partial. `Box<[u8]>` works (B3); whether `Box<str>` parses-and-resolves to that same encoding wasn't verified.

**Gaps / debt:**
- Add `Kind::Str` distinct from `Kind::Slice(u8)` so methods can be dispatched separately and UTF-8 invariants tracked. Today, `&str` and `&[u8]` are indistinguishable types — a `&str` is auto-compatible with a `&[u8]` parameter, which Rust would reject.
- Recommend canonical name `Kind::Str` mirroring Rust naming.

---

## 7. Raw pointer `*const T` / `*mut T`

**Rust nomenclature:** raw pointer types `*const T`, `*mut T`; deref requires `unsafe` (`types/pointer.md`).

**Logos nomenclature:** `LogosType::Kind::Ptr` (`include/logos/compiler/sema.hpp:52`); grammar `ptr_type <- STAR (KW_CONST | KW_MUT) type_ref` (`tools/peg_gen/grammars/logos.peg:1563-1569`); mutability encoded in `mut_ptr()` slot. Deref unsafe enforcement: `src/compiler/sema_expr.cpp:2165-2167` ("dereference of raw pointer requires unsafe context").

**Match verdict:** OK.

**Implementation pointer:**
- Kind: `include/logos/compiler/sema.hpp:52`.
- Grammar: `tools/peg_gen/grammars/logos.peg:1563-1569`.
- Unsafe enforcement: `src/compiler/sema_expr.cpp:2165-2167`.
- Reference→Ptr coercion: `src/compiler/sema.cpp:1708-1712`.

**Interactions check:**
- `unsafe` (deref requires) — OK (`sema_expr.cpp:2167`).
- Coercions (`&T → *const T`, `*mut → *const`) — OK (`sema.cpp:1708-1712`).
- Move (Copy regardless of `T`) — OK. `Ptr` is in the trivially-Copy bucket (`sema.cpp:2521`).
- FFI — partial. `extern "C"` is grammar-level; raw ptr is the FFI type currency.
- `transmute` — covered via metaprog / `as` cast; not audited here.
- NULL / pointer arithmetic — present.
- Variance (invariant in `*mut`, covariant in `*const`) — n/a (no surface variance check).
- `&raw const` / `&raw mut` syntax — GAP. Rust 2024 syntax; grammar has no entry. Currently `&x as *const T` is the substitute (works but takes a borrow detour).

**Gaps / debt:**
- No `&raw const` / `&raw mut` syntax. Needed for sound construction of raw ptrs to unaligned fields (`#[repr(packed)]`).
- Raw-ptr fat form (`*const [T]`) goes through `Kind::Slice` (`logos.peg:1563-1566`) — naming drift; reusing Slice for a *raw* unsafe pointer means borrow-check sees it as safe. Soundness-significant.

---

## 8. Function-item types

**Rust nomenclature:** anonymous ZST per fn item; coerces to fn pointer; spec `types/function-item.md`.

**Logos nomenclature:** **No distinct kind.** Grep for `Function item type`, `fn-item`, `ZeroSizedFn` in `src/compiler/` returned nothing. A function reference is typed as `Kind::FnPtr` directly (`src/compiler/sema.cpp:5105-5109`).

**Match verdict:** GAP — Logos collapses fn-item into fn-pointer. Rust spec says they are *distinct*: `fn foo() {} let x = foo;` gives `x` an unnameable ZST type that coerces to `fn()`.

**Implementation pointer:**
- The "this is a fn-item / fn-ptr" decision lives at `src/compiler/sema.cpp:5105-5109` (`tc == la::FN_PTR_TYPE → Kind::FnPtr`).
- No grep hit for a separate fn-item kind anywhere in the tree.

**Interactions check:**
- Function pointers (coercion) — n/a (no fn-item type to coerce *from*; everything is already FnPtr).
- Generics (each instantiation is its own type) — WARN. In Rust, `foo::<i32>` and `foo::<u32>` have *different* fn-item types; if Logos types them both as the same `fn() -> i32` / `fn() -> u32` FnPtr, monomorphization still produces distinct functions but the type identity flattens. Probable practical bug: `if cond { foo::<i32> } else { foo::<u32> }` would type-check as identical FnPtrs.
- Closure (compatible coercion when non-capturing) — OK (Closure ↔ FnPtr coercion at `sema.cpp:1670-1675`).
- Const eval — n/a.

**Gaps / debt:**
- Add `Kind::FnItem` as a ZST distinct from `Kind::FnPtr`; coerce on use. Spec-conformance + better generic identity. Currently the absence creates the soundness corner above.
- No diagnostic naming convention (`fn(u32) -> i32 {fn_name}`) — error messages probably show the bare FnPtr signature, hiding which specific fn was meant.

---

## 9. Function pointers `fn(T) -> U`

**Rust nomenclature:** bare-fn type `fn(T1, T2) -> U`, with optional `unsafe`/`extern "ABI"`, `for<'a>` HRTB (`types/function-pointer.md`).

**Logos nomenclature:** `LogosType::Kind::FnPtr` (`include/logos/compiler/sema.hpp:69`); grammar `fn_ptr_type` (`tools/peg_gen/grammars/logos.peg:1510-1535`); display "fn(T1,T2)->R" (`src/compiler/sema.cpp:1859-1869`).

**Match verdict:** OK — name matches; grammar covers `fn(T)->R`, `unsafe fn(T)->R`, `for<'a> fn(...)`.

**Implementation pointer:**
- Kind: `include/logos/compiler/sema.hpp:69`.
- Grammar (incl. HRTB and `unsafe` qualifier): `tools/peg_gen/grammars/logos.peg:1510-1535`.
- Construction: `src/compiler/sema.cpp:5105-5109`.

**Interactions check:**
- Function items (coercion) — n/a (fn-item type absent; see above).
- Closures (non-capturing → fn ptr) — OK. Closure↔FnPtr coercion exists, but only for non-capturing was not verified at grep level.
- ABI / extern — partial. `extern "C"` is parsed at the fn-item level; `extern "C" fn` pointer type not surfaced as a kind variant.
- Variance (contra in args, co in return) — n/a (no surface variance for FnPtr).
- HRTB (`for<'a> fn(&'a T)`) — partial. Grammar accepts HRTB binders (`logos.peg:1502-1525`, `HRTB_BINDERS` slot); the binders are "parsed-and-captured for future region inference" (`logos.peg:1502-1505` comment). Not subtyping-checked.
- `unsafe fn(...)` — OK on the grammar side (IS_UNSAFE flag captured); enforced at call sites (`sema_expr.cpp:3213`).
- FFI — partial (see ABI above).

**Gaps / debt:**
- ABI tag on FnPtr (`extern "C" fn`) absent at the type level. Cross-ABI calls won't be distinguished.
- HRTB on fn-ptr captured but not enforced — same as the lifetimes deficiency in category A (Lifetimes WARN).

---

## 10. Closure types

**Rust nomenclature:** anonymous closure type implementing `Fn`/`FnMut`/`FnOnce`; capture modes; spec `types/closure.md`.

**Logos nomenclature:** `LogosType::Kind::Closure` (`include/logos/compiler/sema.hpp:61`); grammar `closure_type <- PIPE ... PIPE ARROW type_ref` (`tools/peg_gen/grammars/logos.peg:1493-1497`). RFC-2229 phase-1 + phase-2 capture done (per MEMORY.md "place-writer retirement" + DIVERGENCES B2/B7). `dyn Fn*(...) → Kind::Closure` collapse at `sema.cpp:4988-4993`.

**Match verdict:** OK on naming; the type is named "Closure" matching Rust's informal name.

**Implementation pointer:**
- Kind: `include/logos/compiler/sema.hpp:61`.
- Grammar: `tools/peg_gen/grammars/logos.peg:1493-1497`.
- Display: `src/compiler/sema.cpp:1850-1858`.
- Capture / RFC-2229: per commits 70526c97 (phase-1), 20c817d5 (phase-2), `4a` references in MEMORY.md.

**Interactions check:**
- `Fn`/`FnMut`/`FnOnce` traits — OK (mlir-gen routes closure calls via vtable when boxed; direct call otherwise).
- Capture modes (`move`, by-ref) — OK.
- RFC-2229 field-precise capture — OK (phase-2 landed, MEMORY.md).
- Drop (captured drops) — OK (closure env carries droppable fields; SDrop emits via the same recursion).
- Lifetimes (captures borrow) — partial — same lifetime-elision deficit as category A.
- Trait objects (`dyn FnMut(...)`) — OK. `dyn Fn*(...)` collapses directly to `Kind::Closure` (`sema.cpp:4988-4993`) — Logos design simplification, blessed but undocumented in DIVERGENCES.
- Send/Sync — GAP (Send/Sync auto-trait absent globally — category H concern).
- Generics (closures via `impl Fn`) — OK (`impl_type` grammar `KW_IMPL IDENT LPAREN closure_type_args RPAREN ARROW type_ref` at `logos.peg:1241-1244`).
- Async (async closures) — n/a (async is fibres divergence).
- Function pointer coercion (non-capturing only) — OK at the kind level; check that capturing closures are *rejected* from coercing to FnPtr was not verified.
- `mem::take` interplay — n/a.

**Gaps / debt:**
- `dyn Fn(...)` collapsed to `Kind::Closure` is a blessed simplification — needs DIVERGENCES entry.
- Closure-to-FnPtr coercion-when-capturing rejection not grep-confirmed; if missing, capturing closures coerce silently — soundness.
- Closure auto-Copy / auto-Clone from MEMORY.md / category A still open (no closure auto-Copy when all captures Copy).

---

## 11. Trait objects `dyn Trait`

**Rust nomenclature:** `dyn Trait` — fat pointer `{data, vtable}` over a dyn-compatible base trait + auto-traits; supertrait upcasting; spec `types/trait-object.md`.

**Logos nomenclature:** `LogosType::Kind::TraitObject` (`include/logos/compiler/sema.hpp:62` — fat pointer form); `LogosType::Kind::UnsizedDyn` is the bare-DST form (`sema.hpp:83-88`), canonicalises to `TraitObject` under `&`/`*` (`sema.cpp:3670-3673`); grammar `dyn_type` (`tools/peg_gen/grammars/logos.peg:1355-1442`); object-safety check `check_trait_object_safe` (`sema.cpp:2638`); owning `Box<dyn>` flag `owning_trait_object()` (`sema.cpp:1377, 2280, 2433, 4555`).

**Match verdict:** WARN — Rust's canonical name is "trait object" (lowercase, dyn-prefixed surface). Logos uses `TraitObject`/`UnsizedDyn` which is fine, but the documentation should align: Rust calls the *type* `dyn Trait`, Logos's `Kind::TraitObject` is conformant naming.

**Implementation pointer:**
- Kinds: `include/logos/compiler/sema.hpp:62, 83-88`.
- Grammar: `tools/peg_gen/grammars/logos.peg:1355-1442` (many variants for HRTB, lifetime, Fn-family).
- Object safety: `src/compiler/sema.cpp:2638`.
- Vtable layout (supertrait methods + upcasting): per recent commit 527182b9 (MEMORY.md `ref_dyn_supertrait_vtable`).

**Interactions check:**
- Traits (object-safety) — OK (`check_trait_object_safe`, sema.cpp:2638).
- DST — OK (`UnsizedDyn` canonicalises to `TraitObject` under ref/ptr).
- References / Box (`&dyn`, `Box<dyn>`) — OK. Owning Box<dyn> uses `owning_trait_object` flag (`sema.cpp:1377-1378`).
- Lifetimes (`+ 'a` bound) — partial (parsed, not enforced).
- Supertraits (upcasting + vtable) — OK (commit 527182b9, vtable holds super-vtable ptrs).
- Auto traits (`+ Send`, `+ Sync`) — GAP (no Send/Sync globally).
- Coercions (unsize `T → dyn`) — OK (`sema.cpp:1679-1686`).
- Generics (`?Sized` to accept dyn) — OK.
- Method dispatch (vtable) — OK.
- Drop (via vtable slot 0) — OK (`sema.cpp:2693-2706` special `__box_dyn__drop`).

**Gaps / debt:**
- Auto-traits on dyn (`dyn Trait + Send`) — grammar has `dyn_auto_bounds` but enforcement of `Send`/`Sync` propagation is absent.
- Lifetime bound `+ 'a` parses but isn't subtyping-checked.

---

## 12. `impl Trait`

**Rust nomenclature:** `impl Trait` — existential (return) / universal (arg) position; spec `types/impl-trait.md`.

**Logos nomenclature:** `LogosType::Kind::ImplTrait` (`include/logos/compiler/sema.hpp:67`); grammar `impl_type` (`tools/peg_gen/grammars/logos.peg:1239-1250`); arg-position desugaring to synthetic generic at `src/compiler/sema_decl.cpp:269, 290, 315`; return-position handling at `sema_decl.cpp:471, 805`.

**Match verdict:** OK.

**Implementation pointer:**
- Kind: `include/logos/compiler/sema.hpp:67`.
- Grammar: `tools/peg_gen/grammars/logos.peg:1239-1250`.
- Arg-position lowering: `src/compiler/sema_decl.cpp:269-316`.
- Return-position handling: `src/compiler/sema_decl.cpp:471, 805`.

**Interactions check:**
- Traits — OK.
- Generics (sugar for type params in arg pos) — OK. Sema's `pending_impl_trait_params_` collects, then folds into the fn's generic-param list (`sema_decl.cpp:269, 315`).
- Lifetimes (capture rules) — partial. Edition-2024 automatic capture (RPIT captures all in-scope generics) is not explicitly implemented; precise `use<...>` bound absent.
- Closure types — OK (Fn-family `impl Fn(...) -> R` parses).
- Async — n/a.
- `dyn Trait` (sibling but distinct) — OK (separate kind).
- Type inference — OK.

**Gaps / debt:**
- No RPIT `use<...>` precise-capture bound.
- Edition-2024 lifetime auto-capture not implemented (no test triggers it yet).

---

## 13. Inferred type `_`

**Rust nomenclature:** `_` inferred type; cannot appear in item signatures (`types/inferred.md`).

**Logos nomenclature:** **No `Kind::Inferred`.** Grep `Kind::Infer` / `InferType` / `InferredType` in include/src/compiler returned nothing. Partial-turbofish placeholders use a separate path: `sema_expr.cpp:3224` ("K-misc: `_` placeholders (from partial turbofish like `apply::<i64, _>`)"). Array-size placeholder mechanism (`arr_placeholder`) at `sema_expr.cpp:4369-4745` is for *array* inference, not the spec's general inferred type.

**Match verdict:** GAP — Rust permits `let x: Vec<_> = ...` and `let x: _ = expr;`. Logos's `_` works *only* in turbofish arg position. In a `let` ascription or arg type, `_` isn't a recognised type-ref alternative in the grammar (`logos.peg:1257` `type_ref <- ...` enumeration shows no underscore alternative).

**Implementation pointer:**
- Turbofish underscore: `src/compiler/sema_expr.cpp:3224`.
- Array-size inference placeholder: `sema_expr.cpp:4369-4745`.
- Grammar `type_ref` alternatives: `tools/peg_gen/grammars/logos.peg:1257` — no inferred-type alt.

**Interactions check:**
- Type inference — partial. Inference happens but driven by literal-defaulting (IntLit/FloatLit) and arg-from-hint, NOT a first-class `_` type. Probably suffices for the imported test corpus today.
- Generics (turbofish) — OK (the one place `_` works).
- Patterns (`_` wildcard distinct from type) — OK (`PAT_WILD` is a distinct pattern AST node).
- Const params (`_` inference) — n/a.

**Gaps / debt:**
- Add `Kind::InferredType` plus grammar alt `inferred_type <- UNDERSCORE` in `type_ref`. Sema resolves inferred-types via unification at the point-of-use.
- Without it, `let x: Vec<_> = ...` won't parse; `Vec<_>` only works in expression turbofish.

---

## 14. Type layout / `#[repr]`

**Rust nomenclature:** type layout (size, alignment, field offsets); `#[repr(Rust)]`, `#[repr(C)]`, `#[repr(transparent)]`, `#[repr(packed)]`, `#[repr(align(N))]`, `#[repr(uN)]` for enums (`type-layout.md`).

**Logos nomenclature:** **No `repr` attribute.** Grep for `repr_c`, `reprC`, `#[repr`, attribute-repr in `src/compiler/` and grammar returned the single comment hit `src/compiler/sema_collect.cpp:1167` (about cfg_attr-wrapped repr). Layout is fixed-Rust via mlir-gen's `layout_of` (`mlir_gen.cpp:133`, `mlir_gen_expr.cpp:2899, 4624, 4630`).

**Match verdict:** GAP — `#[repr(...)]` is absent. Layout uses Rust-style padding/align by default (good) but no override path.

**Implementation pointer:**
- `layout_of` driver: `src/compiler/mlir_gen_expr.cpp:4624-4630` (and `mlir_gen.cpp:133`).
- No `repr` attribute parsing or `SemaStructInfo::repr` field grep-able.

**Interactions check:**
- Struct/Enum/Union (memory layout) — partial. Default Rust layout works; no `repr(C)`/`packed`/`align` opt-in.
- Primitives (size/align) — OK at the fixed-platform level.
- DST (tail rule) — OK (DstRef + custom-DST B2 closed).
- FFI (`repr(C)`) — GAP. Cross-language data exchange uses Rust-default layout which is unstable.
- Niche optimization (Enum) — partial. Enums use value-repr (B7 done) but niche-tag for `Option<&T>`-style discriminant elision wasn't verified.
- `transmute` (size match) — present via metaprog (`size_of` etc.).
- `Sized` — OK (DST tracked via `is_dst`).
- Alignment (slice indexing) — OK.

**Gaps / debt:**
- Add `#[repr(C)]`, `#[repr(transparent)]`, `#[repr(packed)]`, `#[repr(uN)]` for enums.
- DIVERGENCES entry needed: "Logos uses Rust-default layout exclusively; no `#[repr]` opt-ins."
- Union type entirely absent (category C concern, but tied to repr — `Kind::Union` not in `LogosType::Kind`).

---

## 15. Type coercions

**Rust nomenclature:** implicit coercions at "coercion sites"; let-ascription, fn arg, return, struct field, assignment, array/tuple sub-exprs (`type-coercions.md`).

**Logos nomenclature:** `types_compatible(from, to)` is the central coercion driver (`src/compiler/sema.cpp:1611-1760+`); also `types_equal` for strict identity (`sema.cpp:204`).

**Match verdict:** OK on the major coercion families; WARN on the bidirectional Never permissiveness and the slice-mut-bit conflation.

**Implementation pointer:**
- Core: `src/compiler/sema.cpp:1611-1760`.
- Coercion sites — implicit through call/let/return paths in `sema_expr.cpp` / `sema_stmt.cpp` (no named "coerce_at_site" function).

**Interactions check:**
- Subtyping — OK (Never).
- Reborrow — covered in category A audit.
- Deref coercion (`&T` → `&U` via `Deref`) — partial. Special-cased: `&Vec<T> → &[T]` at `sema.cpp:1722-1737`. General `Deref`-driven coercion: covered via auto-deref in method resolution but not as a coercion-site rule.
- Unsize (`T` → `[T]`, `T` → `dyn Trait`) — OK (`sema.cpp:1679-1686` for `&Struct → &dyn Trait`; `&[T;N] → &[T]` at `sema.cpp:1730`).
- Raw pointer coercions — OK (`sema.cpp:1708-1717`).
- Never type — OK (but see WARN above on bidirectional acceptance).
- Lifetime variance — partial (not enforced).
- Let-bindings — OK.
- Call args — OK.
- Method receivers — OK (auto-ref + auto-deref).
- Return expression — OK.

**Gaps / debt:**
- General `Deref` coercion is hard-coded for `Vec → slice` (`sema.cpp:1722`) and `Box` (Deref impl). A trait-driven coercion rule for arbitrary `impl Deref` types is absent.
- Never permissive in both directions (`sema.cpp:1619`) — should be one-way (Never → T only).
- Coercion-site recursion into array/tuple sub-exprs (the spec's `coerce.site.subexpr` propagation) isn't a named pass; element-wise compatibility at `sema.cpp:1650-1663` handles part of it but doesn't *coerce* — just checks compat.

---

## 16. Dynamically sized types (DST)

**Rust nomenclature:** DST — types without statically-known size; only behind a pointer; struct may have a DST as last field; `?Sized` opt-in (`dynamically-sized-types.md`).

**Logos nomenclature:** `is_dst()` on `SemaStructInfo` (`src/compiler/sema.cpp:3190, 3203`); `Kind::UnsizedSlice` / `Kind::UnsizedDyn` for bare DST forms (`sema.hpp:77-88`); `Kind::DstRef` for fat-pointer-to-custom-DST `{data, tail_len}` (`sema.hpp:89-97`); `?Sized` opt-in via grammar; custom-DST tail-slice (B2) closed 2026-05-29.

**Match verdict:** OK on naming and major surface.

**Implementation pointer:**
- Kinds: `include/logos/compiler/sema.hpp:77-97`.
- `is_dst` query: `src/compiler/sema.cpp:3190, 3203`.
- DstRef ctor / subst: `sema.cpp:3675, 3708, 3827, 4805-4847`.
- `Self: ?Sized` seeding: `sema_decl.cpp:1567-1582`.
- DIVERGENCES B2 closed (custom-DST + owning Box<Foo>).
- DIVERGENCES B3 closed (Box<[T]>).

**Interactions check:**
- `?Sized` bound — OK.
- References (fat ptr) — OK (Slice/TraitObject/DstRef all fat).
- Box (`Box<T: ?Sized>`) — OK (Box<dyn>, Box<[T]>, Box<CustomDst> done).
- Slice / str / `dyn` — OK on surface; str is `Slice<u8>` (see feature 6 WARN).
- Custom-DST (struct with `[T]` tail) — OK (B2).
- Drop (custom drop glue) — OK (per-element + buffer free, valgrind 0/0 per DIVERGENCES B2/B3).
- Type layout (tail align) — OK at the implementation level.
- Field access (only static-known prefix) — OK (`sema.cpp:4847-4847`).

**Gaps / debt:**
- DST trait-impl rule "Self: ?Sized is the default in trait definitions" — Logos's default is `Self: Sized`; relaxing requires explicit `?Sized`. Trait-side default not verified; could be a quiet source of object-safety mismatches.
- `&'a (T + 'b)` lifetime-on-DST not tested.

---

## Cross-category gaps

- **Send/Sync auto-traits absent** (touches features 10 Closure, 11 TraitObject) — category H is where these live but they leak directly into B (e.g. `dyn Trait + Send` parses but does nothing).
- **`#[repr(...)]` family absent** (touches B14, C structs/enums/unions, K unsafe `transmute`).
- **Union (`Kind::Union`) entirely missing** — type-system primitive in the next-door category C, but its absence forces Logos to encode any "union" idiom via metaprog. DIVERGENCES doesn't mention it.
- **Lifetime annotations parsed-and-dropped on Slice / TraitObject / FnPtr-HRTB** — same root cause as category A's Lifetimes WARN.
- **Const generics + Array size**: `arr_size_var` symbolic-N machinery exists (`sema.cpp:3659, 5165`); the const-generic surface (D-category) needs to round-trip cleanly.
- **`&raw const` / `&raw mut`** (touches B7 raw ptr + L attributes) — Rust 2024 syntax absent.

## Recommended next moves

Ordered roughly by impact-per-session-effort:

1. **B5 slice-mut-bit unify (B6 in DIVERGENCES, deferred but soundness)** — add a `mut` bit to `Kind::Slice`, reject `a[i]=v` through `&[T]`, and add `&[T]` ⊇ `&mut [T]` coercion. Ships one full Rust-conformance line; one session.
2. **B13 inferred type `_`** — grammar alt + `Kind::InferredType` + unification fallback at let-ascription. Enables `let x: Vec<_> = ...` which is everywhere in the imported corpus. One session.
3. **B8 fn-item type (`Kind::FnItem`)** — distinct ZST, coerces to FnPtr; fixes the `if cond { foo::<i32> } else { foo::<u32> }` corner. One session.
4. **B2 Never strictness** — make `to.kind() == Never` rejection (only `from.kind() == Never` should permit). Probably zero test fallout, real soundness gain. Half-session.
5. **B6 `Kind::Str` distinct from `Kind::Slice(u8)`** — open the door to `&str`-vs-`&[u8]` separation and UTF-8 invariant enforcement. One session; risk = high churn in stdlib `str` paths.
6. **B14 `#[repr(C)]` (only)** — attribute parse + plumbed into `layout_of`. Defer transparent/packed/align until a real test asks. One session if scoped.
7. **DIVERGENCES additions** — single-session doc pass to register the *blessed* divergences this audit surfaced: variadic tuple `(A...)`, `dyn Fn*(...) → Kind::Closure` collapse, `I24/U24/I56/U56` widths, `Void` kind, no `repr`, no Union.
