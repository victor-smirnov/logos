# Rust language features — direct-interaction map

> Working table for the Logos parity cascade. Source: `/home/victor/cxx/reference/src/*.md` (rust-lang/reference mdBook). Spec section refs are `reference/src/<path>.md`.

## What "direct interaction" means

Two features X and Y directly interact when ANY of these hold:

- **Syntactic containment** — X's surface syntax admits Y inside it (`struct S<'a, T>` ⇒ struct ↔ lifetime params + type params).
- **Semantic reference** — X's rules cite Y (`match` cites refutability; `trait` cites associated items).
- **Constraint imposition** — X constrains Y (`Sized` constrains generic params; `'static` constrains trait objects).
- **Dedicated coercion / desugaring** — X has compiler rules special-cased on Y (`?` desugars via `Try`; `for` desugars via `IntoIterator`).

Transitive interactions (X → Y → Z) are **not** edges. Only first-hop, so the table stays readable and each edge is a concrete invariant.

Format: each feature lists its direct neighbours. Edges are bidirectional but listed once per feature for compactness — read the other side to see both endpoints.

---

## A. Ownership model

### Move
*Spec:* `destructors.md`, `expressions.md` §value vs place.
Default for non-`Copy` types. Source becomes inaccessible after the move.

**Interacts with:** Copy · Drop · Borrow · `let` / assignment · Closure (capture modes) · Match (scrutinee move) · Function call (by-value args) · Return · Struct/Tuple construction (field move) · Pattern bindings · Partial moves (FieldRead) · `unsafe` (raw ptr semantics).

### Copy
*Spec:* `special-types-and-traits.md` §`Copy`, `types.md`.
Marker trait. Copying replaces moving where it applies.

**Interacts with:** Move · Drop (mutually exclusive — `Copy` requires `!Drop`) · Auto-traits · Generics (`T: Copy`) · `#[derive(Copy)]` · Primitives · References (`&T` is Copy; `&mut T` is NOT) · Function pointers · Closures (auto-Copy if all captures Copy).

### Drop / RAII
*Spec:* `destructors.md`.
Run when a value goes out of scope (or is overwritten).

**Interacts with:** Move (suppresses Drop on the source) · Copy (mutually exclusive) · Variables/scopes · Struct/Enum (per-field/payload recursive drop) · Trait `Drop` · `mem::ManuallyDrop` (suppression) · Pinning · Assignment (drop-before-replace + drop elaboration) · Panic (drop on unwind) · Const eval (no `Drop` in const fns) · `?Sized` / DST (drop glue via vtable for `Box<dyn>`).

### Borrow `&T` / `&mut T`
*Spec:* `types/pointer.md` §Shared/Mutable references, `expressions/operator-expr.md` §Borrow.
Non-owning references; exclusivity ensures safety.

**Interacts with:** Lifetimes · Reborrow · Mutability of bindings · Borrow checker · Variance (`&T` covariant, `&mut T` invariant) · Patterns (`ref`/`ref mut`) · Coercions (`&mut → &`, `&T → *const T`) · Method receivers · DST (fat refs to `[T]`, `str`, `dyn`) · Closures (capture-by-ref) · Two-phase borrows · Field-path borrows · NLL.

### Lifetimes
*Spec:* `types.md` §References, `lifetime-elision.md`, `trait-bounds.md`.
Static scope inference; an annotation on references and types.

**Interacts with:** Borrow · References · Generics (lifetime params) · Lifetime elision · HRTB (`for<'a>`) · Trait objects (`+ 'a` bound) · Subtyping (`'a: 'b` outlives) · Where-clauses (outlives) · Structs (`struct S<'a>`) · Impls (`impl<'a> T for S<'a>`) · `'static` · Dropck / region inference · Async (borrow-across-await).

### Reborrow
*Spec:* `types/pointer.md` §Mutable references (implicit), `type-coercions.md`.
Implicit re-take of a `&mut T` (or downgrade to `&T`) at coercion sites.

**Interacts with:** Borrow · Method receivers (auto-reborrow) · Call args (auto-reborrow) · Let-binding type ascription · Two-phase borrows · NLL release · Coercions.

### Variance & subtyping
*Spec:* `subtyping.md`.
Lifetime/type subtyping rules; `&'long T <: &'short T`.

**Interacts with:** Lifetimes · References · Generics (param variance) · Trait objects · Function pointers · HRTB · PhantomData · DST coercions.

---

## B. Type system primitives

### Primitive types (`bool`, integer, float, `char`, unit)
*Spec:* `types/boolean.md`, `types/numeric.md`, `types/char.md`.

**Interacts with:** Copy (all) · Coercions (integer widening) · Pattern literals · Operator overloading (primitives bypass via built-ins) · Const eval · Numeric suffixes (lexer) · Repr (size/align fixed).

### Never type `!`
*Spec:* `types/never.md`, `divergence.md`.
Empty type; subtypes every type.

**Interacts with:** Subtyping · Divergence (`return`/`break`/`panic`) · `match` exhaustiveness · Variance · `loop {}` (infinite) · Function return types · Type inference.

### Tuple
*Spec:* `types/tuple.md`, `expressions/tuple-expr.md`, `patterns.md`.

**Interacts with:** Struct (similar memory) · Patterns (tuple-pat) · Field access (`.0`) · Construction expr · Drop (per-element) · Generics (`(A, B, ...)`) · Variance (per-element).

### Array `[T; N]`
*Spec:* `types/array.md`, `expressions/array-expr.md`.

**Interacts with:** Const params (`N`) · Slice (decay + `&[T;N] → &[T]`) · Patterns (array-pat) · Indexing (`Index`/`IndexMut`) · Drop (per-element) · Move (element-by-element; cannot move out of array of non-Copy) · `Copy` (auto if elem `Copy`) · Repr / layout (contiguous).

### Slice `[T]`
*Spec:* `types/slice.md`.
DST; only behind a fat pointer.

**Interacts with:** DST / `?Sized` · References (fat `&[T]`) · Box (`Box<[T]>` owning fat) · Indexing · Patterns (slice-pat) · `IntoIterator` · Lifetimes · `str` (special slice of `u8`).

### `str`
*Spec:* `types/str.md`.

**Interacts with:** DST · Slice (parallel design) · `&str` literals · UTF-8 invariants · Patterns (string-literal) · `String` (heap-owning) · Box (`Box<str>`).

### Raw pointer `*const T` / `*mut T`
*Spec:* `types/pointer.md` §Raw pointers.

**Interacts with:** `unsafe` (deref requires) · Coercions (`&T → *const T`, `*mut → *const`) · Move (Copy regardless of `T`) · FFI · `transmute` · NULL / pointer arithmetic · Variance (invariant in `*mut`, covariant in `*const`).

### Function-item types
*Spec:* `types/function-item.md`.
Zero-sized type per fn item.

**Interacts with:** Function pointers (coercion) · Generics (each instantiation is its own type) · Closure (compatible coercion when non-capturing) · Const eval.

### Function pointers `fn(T) -> U`
*Spec:* `types/function-pointer.md`.

**Interacts with:** Function items (coercion) · Closures (non-capturing → fn ptr) · ABI / extern · Variance (contra in args, co in return) · HRTB (`for<'a> fn(&'a T)`) · `unsafe fn(...)` · FFI.

### Closure types
*Spec:* `types/closure.md`, `expressions/closure-expr.md`.
Anonymous types implementing `Fn`/`FnMut`/`FnOnce`.

**Interacts with:** `Fn`/`FnMut`/`FnOnce` traits · Capture modes (`move`, by-ref) · RFC-2229 field-precise capture · Drop (captured drops) · Lifetimes (captures borrow) · Trait objects (`dyn FnMut(...)`) · Send/Sync · Generics (closures via `impl Fn`) · Async (async closures) · Function pointer coercion (non-capturing only) · `mem::take` interplay.

### Trait objects `dyn Trait`
*Spec:* `types/trait-object.md`.
Type-erased fat pointer `{data, vtable}`.

**Interacts with:** Traits (object-safety) · DST · References / Box (`&dyn`, `Box<dyn>`) · Lifetimes (`+ 'a` bound) · Supertraits (upcasting + vtable) · Auto traits (`+ Send`, `+ Sync`) · Coercions (unsize `T → dyn`) · Generics (`?Sized` to accept dyn) · Method dispatch (vtable) · Drop (via vtable slot 0).

### `impl Trait`
*Spec:* `types/impl-trait.md`.
Existential (return) / universal (arg) position.

**Interacts with:** Traits · Generics (sugar for type params in arg pos) · Lifetimes (capture rules) · Closure types · Async (return-position-impl-trait for async fn) · `dyn Trait` (sibling but distinct) · Type inference.

### Inferred type `_`
*Spec:* `types/inferred.md`.

**Interacts with:** Type inference · Generics (turbofish) · Patterns (`_` wildcard distinct from type) · Const params (`_` inference).

### Type layout / `#[repr(...)]`
*Spec:* `type-layout.md`.

**Interacts with:** Struct/Enum/Union (memory layout) · Primitives (size/align) · DST (tail rule) · FFI (`repr(C)`) · Niche optimization (Enum) · `transmute` (size match) · `Sized` · Alignment (slice indexing).

### Type coercions
*Spec:* `type-coercions.md`.
Implicit conversions at "coercion sites".

**Interacts with:** Subtyping · Reborrow · Deref coercion (`&T` → `&U` via `Deref`) · Unsize (`T` → `[T]`, `T` → `dyn Trait`) · Raw pointer coercions · Never type · Lifetime variance · Let-bindings · Call args · Method receivers · Return expression.

### Dynamically sized types (DST)
*Spec:* `dynamically-sized-types.md`.

**Interacts with:** `?Sized` bound · References (fat ptr) · Box (`Box<T: ?Sized>`) · Slice / str / `dyn` · Custom-DST (struct with `[T]` tail) · Drop (custom drop glue) · Type layout (tail align) · Field access (only static-known prefix).

---

## C. Items

### Function (`fn`)
*Spec:* `items/functions.md`.

**Interacts with:** Generics · Lifetimes · Where-clauses · `unsafe fn` · `async fn` · `const fn` · `extern "ABI" fn` · Function-item type · Patterns (param destructure) · Return type · Diverging (`-> !`) · Attributes (`#[inline]`, etc.).

### Struct (named / tuple / unit)
*Spec:* `items/structs.md`, `types/struct.md`.

**Interacts with:** Generics (`<T, 'a>`) · Visibility (per-field) · Drop · Copy · Move (field-wise) · Repr · DST (last-field unsized) · Construction expr · Pattern destructure · Field access · Lifetime params · Methods (via `impl`) · Variance (per-field).

### Enum
*Spec:* `items/enumerations.md`, `types/enum.md`.

**Interacts with:** Variants (carry payload like tuple/struct) · Discriminant (explicit `=`) · Match exhaustiveness · Patterns (variant pat) · Drop (payload-recursive) · Niche optimization · `#[repr(uN)]` · Generics · Methods · `Never` (empty enum) · `Option`/`Result` (stdlib special role).

### Union
*Spec:* `items/unions.md`, `types/union.md`.

**Interacts with:** Field access (`unsafe` read) · Drop (manual; no auto drop of fields) · Repr · FFI · `Copy` (all fields must be Copy) · Type layout · Generics (allowed but ManuallyDrop common).

### Const item / Static item
*Spec:* `items/constant-items.md`, `items/static-items.md`.

**Interacts with:** Const eval · Type inference (no bare `_`) · Mutability (`static mut` is `unsafe`) · `Sync` bound (statics) · Visibility · Modules · Generics (const generics distinct) · Linkage (`#[no_mangle]`).

### Type alias
*Spec:* `items/type-aliases.md`.

**Interacts with:** Generics (lifetime+type params) · Visibility · Modules · Trait associated types (parallel) · Inference (type aliases are NOT new types).

### Trait
*Spec:* `items/traits.md`.

**Interacts with:** Associated items (fn/type/const) · Supertraits · `Self` · Generics · Where-clauses · Trait objects (object-safety) · Default methods · Marker traits · Auto-traits (`Send`/`Sync`) · `impl Trait for Type` (impl block) · Coherence/orphan rules · Method dispatch (static + dynamic) · Bounds.

### Impl block (inherent / trait)
*Spec:* `items/implementations.md`.

**Interacts with:** Trait · Struct/Enum/Union (target) · Generics · Lifetimes · Coherence rules · `where` clauses · Associated items · Method receivers (`self`, `&self`, `&mut self`, `self: Box<Self>`, ...) · Orphan rule · Blanket impls.

### Module
*Spec:* `items/modules.md`.

**Interacts with:** Visibility · Paths · Use declarations · Name resolution · Item nesting · `extern crate` · Inline `mod x { ... }` vs file `mod x;` · Preludes · Crate root.

### Use declaration
*Spec:* `items/use-declarations.md`.

**Interacts with:** Paths · Visibility (`pub use` re-export) · Name resolution · Glob (`*`) · Aliasing (`as`) · Modules · Preludes.

### External block (`extern`)
*Spec:* `items/external-blocks.md`.

**Interacts with:** FFI · ABI · `unsafe` (extern fn calls) · Static (`static FOO: T`) · Linkage attributes (`#[link]`) · Function-pointer types (matched signature).

### Associated items (assoc fn / type / const)
*Spec:* `items/associated-items.md`.

**Interacts with:** Trait · Impl · Generics · `Self::Item` paths · Generic associated types (GATs) · Default values · `where` bounds on assoc types · Trait object compatibility (object-safety bars certain shapes).

---

## D. Generics & bounds

### Type parameters
*Spec:* `items/generics.md`, `types/parameters.md`.

**Interacts with:** Trait bounds · Lifetime params · Const params · Where-clauses · `?Sized` opt-out · Variance · Monomorphization · Inference · HRTB · Defaults.

### Lifetime parameters
*Spec:* `items/generics.md`, `lifetime-elision.md`.

**Interacts with:** Lifetimes · References · Outlives (`'a: 'b`) · Elision rules · Structs (`struct S<'a>`) · Impl blocks · HRTB · Trait objects (`+ 'a`) · `'static` · Generic associated types.

### Const parameters
*Spec:* `items/generics.md`.

**Interacts with:** Array types (`[T; N]`) · Const eval · Type parameters · Where-clauses · Inference (limited) · `#[derive]` interplay · Trait bounds (const bounds, unstable).

### Where-clauses
*Spec:* `items/generics.md` §Where clauses.

**Interacts with:** Generics · Trait bounds · Lifetime bounds · Assoc-type equality · `Self: Sized` (for opt-in object methods) · Method/Impl declarations · HRTB · GATs.

### Trait bounds
*Spec:* `trait-bounds.md`.

**Interacts with:** Generics · Where-clauses · `dyn Trait` (object-safety) · Supertraits · HRTB · Auto-traits · Lifetime bounds · Associated-type bounds (`T: Trait<Item = U>`).

### HRTB `for<'a>`
*Spec:* `trait-bounds.md` §Higher-ranked.

**Interacts with:** Lifetimes · Function pointers (`for<'a> fn(&'a T)`) · Trait bounds (`for<'a> Fn(&'a T) -> &'a U`) · Closure traits · Variance.

### `Sized` / `?Sized`
*Spec:* `special-types-and-traits.md` §`Sized`.
Default bound on generic params.

**Interacts with:** Generics (implicit bound) · DST (`?Sized` opts in) · Trait objects · References (fat vs thin) · Box (`Box<T: ?Sized>`) · Method receivers · Struct (last-field-unsized rule).

### Generic associated types (GATs)
*Spec:* `items/associated-items.md` §Type Aliases (assoc context).

**Interacts with:** Trait associated types · Generics · Lifetimes (lifetime-GATs) · Where-clauses · HRTB · Trait object compatibility.

---

## E. Expressions & control flow

### `let` (incl. `let-else`)
*Spec:* `statements.md`, `expressions/match-expr.md` §let-else.

**Interacts with:** Patterns · Type ascription (coercion site) · Inference · Move/Borrow (RHS consumed/borrowed) · Drop scope · `let-else` (diverging else) · Reborrow (let-coerce site) · Variables (mutability).

### Block `{ ... }`
*Spec:* `expressions/block-expr.md`.

**Interacts with:** Statements · Tail expression · Drop scope (block end) · `unsafe` block · `async` block · `const` block · Diverging tail (`!`).

### `if` / `if let`
*Spec:* `expressions/if-expr.md`.

**Interacts with:** Boolean type · Patterns (`if let`) · Refutability · Block · Type unification (both branches) · Coercion to common type · `else` branch · CFG divergence (early-return).

### `match`
*Spec:* `expressions/match-expr.md`.

**Interacts with:** Patterns · Exhaustiveness checking · Refutability · Or-patterns · Guards (`if ...`) · Scrutinee place (move/borrow rules) · Binding modes (default binding `ref`/`ref mut`) · Never type (uninhabited arms) · Drop (scrutinee/match-temp) · Enum (variant patterns).

### Loops (`loop`/`while`/`for`)
*Spec:* `expressions/loop-expr.md`.

**Interacts with:** `break` (with value, in `loop`) · `continue` · Patterns (`while let`, `for x in ...`) · `IntoIterator` (`for` desugaring) · Diverging `loop {}` (`!`) · Labels (`'label:`) · Drop scope · Block · CFG divergence.

### Closure
*Spec:* `expressions/closure-expr.md`, `types/closure.md`.

**Interacts with:** Closure types · `Fn`/`FnMut`/`FnOnce` · `move` keyword · Capture analysis (RFC-2229) · Lifetimes · Generics (closures-as-impl-Trait) · Trait objects (`dyn FnMut`) · Send/Sync · Async closures.

### Try `?`
*Spec:* `expressions/operator-expr.md` §`?`.
Desugars via `Try` trait.

**Interacts with:** `Try` trait (`Try::branch`, `FromResidual`) · Functions returning `Result`/`Option` · Type inference · Error conversion (`From`) · Diverging on `Err`/`None` · `try` blocks (unstable).

### Async / await
*Spec:* `expressions/await-expr.md`.

**Interacts with:** Futures · `async fn` (return-position impl Future) · `await` (suspend points) · Send/Sync · Lifetimes (borrow-across-await) · `Pin` · Drop · `impl Trait` (return type) · State machines (codegen).

### `return`
*Spec:* `expressions/return-expr.md`.

**Interacts with:** Function return type (coercion site) · Diverging (`!`) · Drop (scope unwind) · Closures (closure return) · CFG divergence (if-merge).

### Field access / Method call / Call
*Spec:* `expressions/field-expr.md`, `expressions/method-call-expr.md`, `expressions/call-expr.md`.

**Interacts with:** Struct/Enum/Tuple/Union (field-access kinds) · Trait methods · Autoref (`self`/`&self`/`&mut self`) · Auto-deref (Deref/DerefMut) · Method resolution (inherent → trait) · UFCS (`Trait::method`) · Generics (turbofish) · Visibility · Closures (call as Fn) · Operator overloading (`+` → `Add::add`).

### Operator overloading
*Spec:* `expressions/operator-expr.md`.

**Interacts with:** Operator traits (`Add`, `Index`, `Deref`, `Drop`, `PartialEq`, `Ord`, ...) · Auto-deref · Coercions · Patterns (`==` in match guards) · Const eval (some ops in const).

### Range `a..b`
*Spec:* `expressions/range-expr.md`.

**Interacts with:** `Range`/`RangeInclusive`/`RangeFrom`/`RangeTo`/`RangeFull` types · `for` loop (IntoIterator) · Slice indexing (`&v[a..b]`) · Patterns (range-pat).

### Cast `as`
*Spec:* `expressions/operator-expr.md` §Type cast.

**Interacts with:** Numeric coercions (truncation/extension) · Pointer casts · `unsafe` (some ptr casts) · Coercions (interplay) · Const eval.

---

## F. Patterns

### Pattern kinds
*Spec:* `patterns.md`.
Literal, ident, range, ref, tuple, struct, enum-variant, slice, rest (`..`), wildcard (`_`), or-pattern.

**Interacts with:** Match (most uses) · Let-bindings · Fn-params · If-let / while-let / for · Refutability rules · Binding modes (`ref`/`ref mut`/default-binding ergonomics) · Move vs borrow (depends on binding kind) · Type inference · Const generics (const pattern).

### Refutability
*Spec:* `patterns.md` §Refutability.

**Interacts with:** Match (all patterns refutable) · `let` (must be irrefutable) · `if let` / `while let` (must be refutable) · Fn-params (irrefutable).

### Binding modes / default bindings
*Spec:* `patterns.md` §Binding modes.

**Interacts with:** Move/Borrow · `&` patterns auto-deref · `mut` binding · `ref`/`ref mut` (explicit borrow binding) · Match ergonomics.

---

## G. Memory / safety

### Interior mutability
*Spec:* `interior-mutability.md`.

**Interacts with:** `UnsafeCell` (foundation) · `Cell` / `RefCell` / `Mutex` / `RwLock` / atomics · `&T` (mutate through shared) · Sync/Send · Coercions · Drop (interior mut drops in shared ref).

### Memory model / atomics
*Spec:* `memory-model.md`.

**Interacts with:** `UnsafeCell` · Atomics (`std::sync::atomic`) · Send/Sync · `unsafe` (raw access) · Drop ordering.

### Variables (mutability, scope)
*Spec:* `variables.md`.

**Interacts with:** `let` · Drop scope · Move/Borrow · `mut` binding · Reborrow (let-coerce site) · Shadowing.

---

## H. Concurrency

### `Send` / `Sync`
*Spec:* `special-types-and-traits.md` §Send/Sync.
Auto-traits.

**Interacts with:** Auto-trait propagation · `unsafe impl` · `Arc<T: Send + Sync>` · `Mutex<T: Send>` · `dyn Trait + Send` · Closures (auto) · Threads · Async (Send futures).

### `async fn` / `async` block
*Spec:* `expressions/await-expr.md`.

**Interacts with:** Future trait · `await` · `impl Future` return · Pin · State machine (codegen) · Lifetimes (borrow-across-await) · Send (Send futures).

---

## I. Modules, names, visibility

### Paths (`a::b::c`, `<T as Trait>::item`)
*Spec:* `paths.md`.

**Interacts with:** Modules · Use declarations · Crate roots · `super`/`self`/`crate` · Generics (turbofish `::<>`) · Associated items · Qualified paths (UFCS).

### Visibility & privacy
*Spec:* `visibility-and-privacy.md`.

**Interacts with:** Modules · Items (per-item visibility) · Struct fields (per-field) · Use declarations · Trait items (no per-method modifiers) · `pub(crate)` / `pub(super)` / `pub(in path)` · Crate boundary.

### Name resolution / preludes / namespaces
*Spec:* `names/name-resolution.md`, `names/namespaces.md`, `names/preludes.md`.

**Interacts with:** Paths · Use declarations · Modules · Macros (separate namespace) · Type vs value namespace · Lifetime/label namespaces · Hygiene (macros).

---

## J. Macros / metaprogramming

### Macros by example (`macro_rules!`)
*Spec:* `macros-by-example.md`.

**Interacts with:** Tokens (fragment specifiers `$x:ident`, `$x:expr`, ...) · Hygiene · Modules (declaration scope) · `use` (macro-use) · Tokens (output) · Repetition (`$(...)*`).

### Procedural macros (derive, attr, fn-like)
*Spec:* `procedural-macros.md`.

**Interacts with:** Tokens (input) · Items (output) · Attributes (`#[derive(X)]`, `#[attr]`) · Crate type (`proc-macro` crate kind) · Diagnostics · Source spans.

---

## K. Unsafe

### `unsafe fn` / `unsafe` block
*Spec:* `unsafe-keyword.md`, `unsafety.md`.

**Interacts with:** Raw pointers (deref) · `transmute` · Union field access · Calling unsafe fns · Implementing unsafe traits (`Send`/`Sync` manually) · FFI · Inline asm.

### UB list
*Spec:* `behavior-considered-undefined.md`.

**Interacts with:** Memory model · References (aliasing) · `transmute` (size/init) · Unaligned access · Mutability violations · Atomics misuse · Raw pointer rules.

---

## L. Attributes

### Built-in attributes (selected)
*Spec:* `attributes.md`, `attributes/derive.md`, `attributes/codegen.md`, `attributes/type_system.md`.

**Interacts with:** Items (most) · Derive (`#[derive(Debug, Clone, ...)]` ↔ traits) · `#[repr(...)]` ↔ layout · `#[inline]` ↔ codegen · `#[cfg(...)]` ↔ conditional compilation · `#[deprecated]` · `#[must_use]` ↔ unused-result lint · `#[non_exhaustive]` ↔ pattern exhaustiveness · `#[allow]`/`#[deny]` ↔ lints.

### Conditional compilation `#[cfg]` / `cfg!()`
*Spec:* `conditional-compilation.md`.

**Interacts with:** Attributes · `cfg!` macro · Build features (`cargo features`) · Items (gating).

---

## M. Const evaluation

### Const eval / `const fn` / `const { ... }`
*Spec:* `const_eval.md`.

**Interacts with:** Const items / static items · Const generics (`[T; N]`) · `const fn` (restricted ops) · Const-context expressions · Pattern literals · Static init · Trait `const` (limited).

---

## N. FFI / linkage / ABI

### `extern "ABI" fn` / blocks
*Spec:* `items/external-blocks.md`, `abi.md`.

**Interacts with:** Function pointers (ABI-tagged) · `unsafe` · Linkage attributes · Calling conventions · Repr (`repr(C)`) · Raw pointers (FFI types) · `Drop` (panics across FFI = UB).

### Inline assembly
*Spec:* `inline-assembly.md`.

**Interacts with:** `unsafe` · Raw pointers · ABI · Registers · `extern "C"` calling.

---

## O. Other

### Panic
*Spec:* `panic.md`.

**Interacts with:** Drop (unwinding runs destructors) · `?` (some shapes) · `Send`/`Sync` (`UnwindSafe`) · `catch_unwind` · `Result` (alternative to panic) · `abort` panic-strategy · ABI (panic across FFI = UB).

### Divergence (`!`)
*Spec:* `divergence.md`.

**Interacts with:** Never type · `return` · `break` (with value) · `continue` · `panic!` · `loop {}` · Match arms (uninhabited).

---

## How to use this table

1. **Pick a feature** that has a known Logos gap (or a feature you want to verify parity on).
2. **Enumerate its direct neighbours** from the table.
3. For each neighbour, ask:
   - Does Logos handle the **composition** of these two correctly?
   - Is there a test that exercises this specific intersection?
4. If a test doesn't exist, **the intersection is a coverage gap** — write a test that exercises it. If the test fails, that's where the next implementation work happens.
5. Coverage is local: closing one feature's neighbour-set doesn't require touching the full graph.

This is intentionally a graph view rather than a checklist — the order in which to attack edges depends on where Logos currently lives in implementation maturity, not on the spec's chapter order.
