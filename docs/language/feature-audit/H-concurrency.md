# Category H — Concurrency (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`).

Summary: 2 features audited — 1 WARN (`Send`/`Sync` — auto-trait machinery present and largely conformant, several soundness gaps), 1 GAP (`async fn` / `async` block — keywords reserved but no grammar productions, no `Future` trait, no state-machine codegen; replaced by the green-fibre design model per blessed divergence A4). Headline gaps: (a) `dyn Trait + Send` parses but is **informational only** — sema does NOT verify that an unsized-to-`dyn Trait + Send` coercion has a `Send` underlying type (grammar comment at `tools/peg_gen/grammars/logos.peg:1350-1353` says so explicitly); (b) `Arc<T>` stdlib lacks `unsafe impl<T> Send/Sync for Arc<T>` so the auto-trait engine will reject `Arc<i32>: Send` (it carries `*mut ArcInner<T>` and structurally fails the raw-pointer check); (c) `thread_spawn(start: *const u8, arg: *mut u8)` exposes no `T: Send` bound — the spawn API is raw-fn-ptr, not Rust's `F: FnOnce() + Send + 'static`; (d) no negative-impl carve-out for `Cell<T>` / `RefCell<T>` (they leak `Sync` per Category G); (e) static-item `Sync` requirement (`lang-types.sync.static-constraint`) is not enforced.

---

## H.1 `Send` / `Sync`

### Rust nomenclature
`Send` and `Sync` auto-traits — `/home/victor/cxx/reference/src/special-types-and-traits.md` §`lang-types.send`, §`lang-types.sync`, §`lang-types.auto-traits`. Key spec rules:
- Auto-impl is structural — implemented if all fields/captures/payloads implement it (`lang-types.auto-traits.aggregate`, `lang-types.auto-traits.closure`).
- Built-in composite rules: `&T`, `&mut T`, `*const T`, `*mut T`, `[T; n]`, `[T]` propagate, function items/pointers are unconditionally Send+Sync (`lang-types.auto-traits.builtin-composite`, `.fn-item-pointer`).
- Generic refinement — `&T: Send` requires `T: Sync` (`lang-types.auto-traits.generic-impl`).
- Negative impls — `*mut T: !Send` stdlib-only (`lang-types.auto-traits.negative`).
- Trait-object marker — auto-traits may appear on a `dyn Trait` as additional bounds (`lang-types.auto-traits.trait-object-marker`).
- Static items must be `Sync` (`lang-types.sync.static-constraint`).

### Logos nomenclature
Logos uses the byte-identical Rust names:
- `pub auto trait Send {}` / `pub auto trait Sync {}` at `stdlib/lang/marker/marker.logos:17-18`.
- Compiler flag: `SemaTraitInfo::is_auto` (`include/logos/compiler/lir.hpp:910`, `src/compiler/sema_impl.hpp:2011`).
- Grammar keyword: `KW_AUTO` — auto-trait declaration form parses `auto trait IDENT { }`.
- Surface auto-impl engine: `src/compiler/sema_auto_trait.cpp:24` `SemaChecker::is_auto_trait_satisfied(...)` — structural recursion through `LogosType::Kind` cases (Scalar/Ptr/Ref/MutRef/TypeVar/Struct/ZonedStruct/Enum/Array/Slice/Tuple/default).
- Bound-driven invocation: `src/compiler/sema_collect.cpp:776-797` (parameter bound at call site), `:1024-1029` (generic impl propagation).
- `unsafe impl` storage: `SemaImplInfo::is_unsafe` (`src/compiler/sema_impl.hpp:2027`); `is_negative` (`:2028`) for `impl !Trait for T {}`.
- Grammar surface for negative impl: `impl !Trait for X {}` accepted; gating at `src/compiler/sema_collect.cpp:3008-3024` (`unsafe impl` required for unsafe trait; negative impl uses `IS_NEGATIVE` flag).

Diagnostic strings (`sema_collect.cpp:783-794`): `"'X': type 'T' does not satisfy auto trait 'Send' (field 'f' of type '*mut i32' is not Send)"` — clear, field-precise.

### Match verdict
**WARN — naming OK, propagation engine OK, several real semantic gaps.**

The auto-trait machinery is the most Rust-conformant piece of the concurrency layer: same names, same auto-derive structural rule, working `unsafe impl` opt-in, working negative-impl rejection, working `&T: Send ⇔ T: Sync`. What diverges:

1. **`dyn Trait + Send` is informational.** Grammar `dyn_auto_bounds` (`tools/peg_gen/grammars/logos.peg:1346-1370`) parses `+ Send`/`+ Sync`/`+ 'a` after a trait-object principal trait, but the explicit comment says "auto traits add no vtable methods, so the trait object is structurally identical to bare `dyn Trait` — sema records the markers (for future Send/Sync-of-dyn checking) but the vtable/layout are unchanged. Accepted-and-currently-informational." No code in `src/compiler/` consumes the markers to enforce that the unsizing source satisfies `+ Send`. Test `tests/logos/pass/dyn_auto_trait_bounds.logos` confirms this — it parses and runs `let r: &dyn Speak + Send = &d;` but never verifies that `Dog: Send`.
2. **Static-item Sync constraint absent.** Rust requires immutable `static` to be `Sync` (`lang-types.sync.static-constraint`). Grep `static.*Sync\|require_sync\|static_must_be_sync` across `src/compiler/sema_*.cpp` returns 0 matches. Logos has no separate `static` item (module-level `static NAME` collapses to `CONST_DEF` per Category G audit), so the rule has nowhere to fire; will need to be re-added when true `static` items land.
3. **Closure auto-trait propagation conservatively false.** `sema_auto_trait.cpp:16, :199-201` returns `false` for `Kind::Closure` / `Kind::TraitObject` etc. Rust's rule `lang-types.auto-traits.closure` requires Send/Sync iff all captures' types do — Logos's conservative false makes any `T: Send` bound reject all closure-typed values. This blocks Rust's standard `thread::spawn(|| ...)` shape. Mitigated only because there is no `thread::spawn<F: FnOnce() + Send>` API (see neighbour check) — but blocks the general case.
4. **`Arc<T>` is structurally `!Send`/`!Sync`.** `stdlib/mem/sync/arc.logos:43-45` declares `pub struct Arc<T> { inner: *mut ArcInner<T> }` — a raw `*mut` field. The auto-trait engine returns `false` for `Ptr` unless an explicit unsafe impl exists (`sema_auto_trait.cpp:101-106`). Grep `unsafe impl.*Send.*Arc\|unsafe impl.*Sync.*Arc` in `stdlib/mem/sync/arc.logos` returns **0 matches**. So `Arc<i32>` will fail `T: Send` checks. Rust ships `unsafe impl<T: Send + Sync> Send for Arc<T>` / `unsafe impl<T: Send + Sync> Sync for Arc<T>`. Logos's `Mutex<T>` / `RwLock<T>` got this right (`stdlib/std/sync/sync.logos:85-86, 152-153`) but `Arc` was missed.
5. **No `Cell<T>` / `RefCell<T>: !Sync` derivation.** See Category G — these end up auto-`Sync` because `UnsafeCell` is not a lang-item carrying `!Sync`.
6. **Negative-impl write-side** `impl !Send for Foo {}` works in tests (`tests/logos/fail/auto_trait_negative_impl.logos`); negative impls do flip the verdict in `check_impl_for_struct` (`sema_auto_trait.cpp:43-54`). Rust restricts negative impls to stdlib-only (`lang-types.auto-traits.negative`: "no stable way to specify additional negative implementations"). Logos accepts them anywhere — minor surface divergence that is in fact more permissive than Rust; not a bug, but worth recording.

### Implementation pointer
- Auto-trait engine: `src/compiler/sema_auto_trait.cpp:24-202`.
- `is_auto` flag set on trait decl: `src/compiler/sema_collect.cpp:1878-1893`.
- Bound check fires auto-impl: `src/compiler/sema_collect.cpp:776-797` (generic-arg position), `:1024-1029` (transitive call-site propagation).
- Generic-target conditional impl resolver (`unsafe impl<T: Send> Send for Wrap<T>`): `src/compiler/sema_auto_trait.cpp:58-84`.
- Mono-side mirror (`mono_impl.hpp:713` comment, `mono_clone.cpp:4382-4385`, `:5248-5253`): same rule executed during monomorphisation.
- Stdlib: `stdlib/lang/marker/marker.logos:17-18`; `stdlib/std/sync/sync.logos:85-86, 152-153`; `stdlib/mem/sync/arc.logos:43-45`; `stdlib/std/thread/thread.logos:59-68`; `stdlib/std/rt/fiber/future.logos:23, 30, 42, 65` (`<T: Send>` bounds on FutureSlot APIs).
- Diagnostic / unsafe-trait coherence: `src/compiler/sema_collect.cpp:3008-3024`.
- Tests: `tests/logos/pass/auto_trait_send_scalar.logos`, `auto_trait_send_struct.logos`, `auto_trait_send_array.logos`, `auto_trait_send_ref_*.logos`, `auto_trait_generic_propagation.logos`, `auto_trait_unsafe_impl.logos`, `auto_trait_negative_impl.logos`, `auto_trait_extra_bounds.logos`, `dyn_auto_trait_bounds.logos`; failure cases at `tests/logos/fail/auto_trait_*.logos` and `relaxed_non_sized_bound_rejected.logos`.

### Interactions check
Direct neighbours from `docs/language/feature-interactions.md` H §`Send`/`Sync`:

- **Auto-trait propagation — OK.** Structural recursion implemented (`sema_auto_trait.cpp`), covers struct/enum/array/slice/tuple/ref/mutref/typevar/scalar/fnptr. Generic substitution in struct fields done at `:142-148`. Cycle guard at `:33-35`. Matches Rust `lang-types.auto-traits.{aggregate,builtin-composite,fn-item-pointer}`.
- **`unsafe impl` — OK.** `unsafe impl Send for MyBuf {}` works (`tests/logos/pass/auto_trait_unsafe_impl.logos`); conditional `unsafe impl<T: Send> Send for Wrap<T>` works with bound propagation (`sema_auto_trait.cpp:58-84`); soundness gating at `sema_collect.cpp:3008-3024`. Mirrors `lang-types.auto-traits.generic-impl`.
- **`Arc<T: Send + Sync>` — WARN (bug, NOT a blessed divergence).** Arc has raw `*mut` field, no `unsafe impl Send/Sync for Arc<T>` written; engine will mark `Arc<i32>` as `!Send` even though the i32 inner is Send. No test exercises `fn need_send<T: Send>(_: T) { } fn main() { let a: Arc<i32> = arc_new(0); need_send(a); }` — predict failure. Compare to `Mutex<T>` which correctly ships `unsafe impl<T: Send> Send for Mutex<T>` (`stdlib/std/sync/sync.logos:85`). This is a stdlib gap, not an engine gap.
- **`Mutex<T: Send>` — OK.** Bound declared (`stdlib/std/sync/sync.logos:51, 60`); manual `unsafe impl<T: Send> Send/Sync for Mutex<T>` shipped (`:85-86`); RwLock follows same pattern (`:152-153`). Engine respects (`sema_auto_trait.cpp:58-84`). Surface divergence: lock returns raw `*mut T` not `MutexGuard<'_, T>` — tracked in Category G.
- **`dyn Trait + Send` — WARN (informational only).** Parsed but never verified — grammar comment explicitly says "for future Send/Sync-of-dyn checking" (`tools/peg_gen/grammars/logos.peg:1350-1353`). A `&dyn Speak + Send = &dog` coercion does not require `Dog: Send`. **NOT** a blessed divergence — it is a soundness gap; the §A4 blessing covers `async`/`Future`/`Pin`, not auto-trait-of-dyn enforcement.
- **Closures (auto) — GAP.** `sema_auto_trait.cpp:199-201` conservative default returns `false` for `Kind::Closure`. Rust requires Send/Sync iff all captures' types are Send/Sync (`lang-types.auto-traits.closure`). Untested intersection: a non-capturing closure passed where `F: Send` expected. Blocks Rust-shaped `thread::spawn(|| { ... })`.
- **Threads — WARN.** `stdlib/std/thread/thread.logos:59-68` `thread_spawn(start: *const u8, arg: *mut u8) -> JoinHandle` exposes no generic `F: FnOnce() + Send + 'static`; caller manually casts a Logos fn item to `*const u8`. This is a stdlib design divergence (no closure-spawn ergonomics yet); not blessed in `docs/DIVERGENCES.md`. The Send/Sync auto-trait machinery would activate if spawn took `F: Send` — currently it cannot because there is no generic spawn shape.
- **Async (Send futures) — n/a — feature absent.** The fibre design model (§A4) makes every fn implicitly suspendable; there is no `impl Future` and no future-Send check (see H.2).

### Gaps / debt
- **Add `unsafe impl<T: Send + Sync> Send/Sync for Arc<T>`** in `stdlib/mem/sync/arc.logos:45` — symmetric to Mutex/RwLock. Without it, `Arc<i32>` fails `T: Send`; high-impact bug, single-line stdlib fix.
- **Enforce `dyn_auto_bounds` at coercion.** Grammar already parses `+ Send`; sema should consult them at the unsize-coercion site so that `coerce_to(&dyn Trait + Send)` rejects when the source struct is not `Send`. Pointer: `src/compiler/sema_expr.cpp` `coerce_*` family near line 3024 + the LIR carry path. Currently silent acceptance — soundness gap.
- **Closure auto-trait propagation.** Walk the captures' types in `sema_auto_trait.cpp` default branch (replace the `false` default for `Kind::Closure`). Needs the capture-list to be reachable from `TypeRef` for a closure type. Without it, no closure can satisfy `T: Send`/`T: Sync`; blocks future closure-spawning API.
- **Static-item Sync requirement.** Once true `static` items land (Category G open work), the static-item collection pass must emit a `T: Sync` bound; the auto-trait engine will then handle it. Today no enforcement.
- **`Cell<T>` / `RefCell<T>: !Sync`.** Requires either an `UnsafeCell` lang-item with a `!Sync` carry, or stdlib `unsafe impl !Sync for Cell<T> {}`. Cross-cuts with Category G interior-mutability fix.
- **Closure-spawn API.** `thread::spawn<F: FnOnce() + Send + 'static>(f: F) -> JoinHandle` once Fn-family + lifetimes-in-stdlib-signatures land. Currently exposed as raw fn-pointer with no Send check (`stdlib/std/thread/thread.logos:59`).
- **Document `Arc` Send/Sync omission as §B catch-up in `docs/DIVERGENCES.md`** OR fix it immediately (1-line stdlib edit — preferred per `feedback_no_defer_fix_now_generalize`).
- **Tests missing**: `Arc<T>` + thread; closure + Send bound; `&dyn Trait + Send` rejection of non-Send source; `Mutex<NotSend>` rejection; `Cell<T>: Sync` (current false positive).

---

## H.2 `async fn` / `async` block

### Rust nomenclature
`async fn` (`/home/victor/cxx/reference/src/items/functions.md` §`items.fn.async`, lines 253-300) and `async` block (`reference/src/expressions/block-expr.md` §async-blocks, transitively from `await-expr.md`). Key spec rules:
- `items.fn.async.future`: an `async fn` returns a future and does no work when called; the body executes when the future is polled.
- `items.fn.async.desugar-brief`: roughly `fn foo() -> impl Future<Output=...> { async move { ... } }`.
- `items.fn.async.lifetime-capture` / `param-capture`: the returned future captures every parameter and lifetime.
- `items.fn.async.safety`: `async unsafe fn` legal; awaiting an async-unsafe-fn's future does NOT require an `unsafe` context (the unsafety is on the call, not the poll).
- `expr.await.{intro,construct,allowed-positions,effects}` (`reference/src/expressions/await-expr.md`): `expr.await` desugars to `IntoFuture::into_future` + `Pin::new_unchecked` + `loop { match Future::poll(...) { Ready(r) => break r, Pending => yield } }`. Only legal in async context.
- `expr.await.edition2018`: only available from Rust 2018.

### Logos nomenclature
- Grammar tokens: `KW_ASYNC = "async"` (`tools/peg_gen/grammars/logos.peg:358`), `KW_AWAIT = "await"` (`:359`) — both **reserved with no grammar production** ("Reserved (no grammar use yet) — kept for stackless-coroutine path on wasm32/64 where threads/context-switch aren't available", `:356-357`).
- No `Future`/`IntoFuture`/`Poll` trait in stdlib: `grep -rn "trait Future\|trait IntoFuture\|trait Poll" stdlib/` returns no matches in any language-level location.
- No `Pin<P>` type: grep returns 0 in `src/compiler/`; `Unpin`/`PhantomPinned` placeholder shells exist at `stdlib/lang/marker/marker.logos:25, 31` for "forward-compatible" parsing only.
- Replacement model: **green fibres + reactor** under `stdlib/std/rt/fiber/`:
  - `Fiber`/`FiberCtx`/`Scheduler` (`stdlib/std/rt/fiber/fiber.logos`).
  - `FutureSlot<T: Send>` one-shot intra/inter-thread future (`stdlib/std/rt/fiber/future.logos:23-97`) — `future_slot_new` / `future_complete` / `future_get`. Note: this `FutureSlot` is **not** the Rust `Future` trait — it is a producer-consumer slot with a single wait point.
  - `Latch` / `Chan<T>` sync primitives (`stdlib/std/rt/fiber/sync.logos`).
  - `Reactor` (`stdlib/std/rt/fiber/reactor.logos`) — io_uring-backed; per-thread; fibres park on submit, resume on CQE.
  - Per-fiber stacks via `mmap` (`stdlib/std/rt/fiber/fiber.logos:24-34`), context switch via `fiber_switch.S` (`src/reactor/fiber_switch.S`).
- Compiler hits zero `async`/`await`/`Future`/`Pin` in semantic analysis: grep `'async'\|\bawait\b\|Future\b\|Pin<' src/compiler/*.cpp src/compiler/*.hpp` — all hits are unrelated (variable names like "future work" comments). Confirmed by tests imported skip register at `tests/imported/WHY-WE-SKIP.md:17-31`: `async-await` permanently skipped (461 test files), reason "the whole `tests/ui/async-await/` directory exercises the colour mechanism — it doesn't translate."

### Match verdict
**GAP / blessed divergence A4 — feature intentionally absent; capability replaced by green-fibre model.**

`docs/DIVERGENCES.md` §A4 records this as a deliberate design model (not a catch-up TODO): "async / await / `Future` / `Pin` … green fibres + reactor; every fn is implicitly suspendable, no colour. The *capability* exists (write the sync form; the fibre runtime makes it non-blocking). The colour mechanism itself is intentionally absent." So Logos does **not** owe Rust parity here; the surface `async`/`await` keywords remain reserved (`:356-359`) for a potential future wasm32/64 stackless-coroutine path where the fibre model can't run.

Two things are mis-aligned with the §A blessing template that should be tightened:
1. The grammar comment at `logos.peg:356-357` says "Reserved … kept for stackless-coroutine path on wasm32/64". The blessing in `DIVERGENCES.md:42` doesn't carry this reservation note — readers may wonder why the keywords exist at all. Either (a) drop them (no in-tree use) or (b) cross-reference the blessing.
2. `Unpin` / `PhantomPinned` (`stdlib/lang/marker/marker.logos:21-31`) live as parsing shims with no compiler-side semantics. They are forward-compat placeholders for "self-referential generators (e.g. async)". With the blessing being "design model — colour mechanism intentionally absent", the `Pin`-related shims have no consumer; either keep them as opaque marker types (current state) or remove. No bug; cosmetic clarification.

### Implementation pointer
- Keywords reserved, no productions: `tools/peg_gen/grammars/logos.peg:356-359`.
- Replacement runtime (Logos side):
  - Fibres: `stdlib/std/rt/fiber/fiber.logos`, `src/reactor/fiber.cpp`, `src/reactor/fiber_switch.S`.
  - Reactor: `stdlib/std/rt/fiber/reactor.logos`, `src/reactor/reactor.cpp`, `src/reactor/reactor_engine.cpp`.
  - Scheduler: `src/reactor/scheduler.cpp`, `stack_chain.cpp`, `stack_pool.cpp`, `stack_allocator.cpp`, `morestack.S`.
  - One-shot future: `stdlib/std/rt/fiber/future.logos:23-97`.
  - Sync primitives: `stdlib/std/rt/fiber/sync.logos`.
  - Thread bridge: `stdlib/std/rt/fiber/thread.logos`, `stdlib/std/thread/thread.logos`.
- Skip register: `tests/imported/WHY-WE-SKIP.md:17-31` documents the 461-file `async-await` permanent skip with rationale.
- Pin/Unpin shims: `stdlib/lang/marker/marker.logos:21-31, 38` (declared, unused).

### Interactions check
Direct neighbours from `docs/language/feature-interactions.md` H §`async fn` / `async` block:

- **Future trait — n/a — feature absent.** No `Future` trait; the replacement `FutureSlot<T>` (`stdlib/std/rt/fiber/future.logos:23`) is a one-shot producer-consumer slot, not a polled state machine. Equivalent capability achieved via blocking-style `future_get(slot)` that internally fibre-suspends.
- **`await` — n/a — feature absent.** No `.await` syntax; all I/O calls block fibre-locally via reactor parking (`reactor_submit_and_park` in `stdlib/std/rt/fiber/reactor.logos`).
- **`impl Future` return — n/a — feature absent.** No `impl Trait` in return position for any future-shaped trait; functions return concrete values, and suspendability is a runtime property of every fn.
- **Pin — n/a — feature absent.** No `Pin<P>` type. `Unpin` / `PhantomPinned` shells exist at `stdlib/lang/marker/marker.logos:21-31` but with no Pin to project from, they have no semantic role. Self-referential generators are not expressible.
- **State machine (codegen) — n/a — feature absent.** No async-to-state-machine MIR transform. Each fn compiles to a normal function; suspension is fibre context-switch (`logos_fiber_switch` in `fiber_switch.S`), not generator resume.
- **Lifetimes (borrow-across-await) — n/a — feature absent.** Because there is no await point, there is no "borrow across await" check; ordinary borrow-check (`src/compiler/borrow_check.cpp`) applies. Note that fibre context switch can in fact happen at any I/O syscall, so a `&mut` held across a fibre-suspending I/O call is **always** alive on the same OS thread — soundness is provided by the single-OS-thread-per-fibre invariant, not by a type-level check. Cross-thread fibre migration is not currently supported (would change this analysis).
- **Send (Send futures) — partial.** `FutureSlot<T: Send>` (`stdlib/std/rt/fiber/future.logos:23, 30, 42, 65`) places `T: Send` on the value type for cross-thread completion (waiter_thr at `:9, 26`). This is the equivalent of "Send futures": the value crossing threads is bounded by `Send`, not the future-as-state-machine itself. The bound is enforced by the H.1 auto-trait engine.

### Gaps / debt
- **Not a parity item.** Blessing §A4 covers this. The remaining work is documentation hygiene, not implementation.
- Reserved-keyword cleanup: either (a) cross-reference §A4 from `logos.peg:356-359` to make the reservation rationale auditable, or (b) demote `KW_ASYNC` / `KW_AWAIT` to non-keywords (they have no grammar use). Tracked under doc hygiene only.
- Pin/Unpin/PhantomPinned shells: confirm they have a real port target (any imported test that names them) or remove. Currently in `stdlib/lang/marker/marker.logos:21-31` with the comment "self-referential generators (e.g. async)" — but with async blessed away, the rationale weakens.
- Cross-thread fibre migration would change the borrow-across-suspend story; not on the roadmap, but flag for the day it lands.
- `FutureSlot::T: Send` is the only place the Send auto-trait composes with the fibre runtime; verify by test that `FutureSlot<*mut i32>` (non-Send T) is rejected at call site — likely no test today (`grep "FutureSlot.*Send\|future_slot_new.*[!]Send" tests/` returns 0 matches outside the source file itself).

---

## Cross-category gaps

- **Auto-trait + interior mutability (G ↔ H).** `Cell<T>` / `RefCell<T>` should be `!Sync` per Rust (`UnsafeCell<T>: !Sync` auto-propagates). Logos has no `UnsafeCell` lang-item carrying `!Sync`, so today `Cell<i32>` is incorrectly `Sync`. Fix lives in Category G (`UnsafeCell` carve-out) and Category H (negative-impl propagation) jointly.
- **Auto-trait + closures (B ↔ H).** Closure types must propagate Send/Sync from their captures (`lang-types.auto-traits.closure`). Today `sema_auto_trait.cpp:199-201` returns conservative false. The fix needs closure-type capture metadata reachable from `TypeRef` — Category B (closure-types audit) holds the structure; Category H consumes it.
- **Auto-trait + dyn Trait (B ↔ H).** Trait-object bound enforcement at coercion site (`+ Send`/`+ Sync` on `dyn`). Grammar already accepts (Category B); sema enforcement is missing (Category H).
- **`'static` lifetime + thread spawn (A ↔ H).** Rust's `thread::spawn<F: FnOnce() + Send + 'static>(f)` requires `'static`; Logos has no named `'static` distinction in the bound surface that bites here, plus no closure-spawn API. Cross-cuts the lifetimes audit (A).
- **Const eval + `Send`/`Sync` (M ↔ H).** Not exercised in current stdlib; future const-evaluable atomic init might need `Sync` checks at const time. No work item today, flagged for the day const evaluation gains memory access.

## Recommended next moves

Ordered by impact / effort:

1. **Add `unsafe impl<T: Send + Sync> Send/Sync for Arc<T>` in `stdlib/mem/sync/arc.logos:45`.** One-line stdlib edit. Without it, `Arc<i32>` fails `T: Send`. Mirror Mutex/RwLock at `stdlib/std/sync/sync.logos:85-86, 152-153`. Add a `tests/logos/pass/arc_send.logos` and `tests/logos/fail/arc_not_send_when_t_not_send.logos`. **Single-session.**
2. **Enforce `dyn_auto_bounds` at unsize coercion.** Grammar already parses `+ Send`. Plumb the marker list through `DYN_TYPE` resolution in `src/compiler/sema_decl.cpp:1200` into the TraitObject TypeRef, then check at the coercion site in `src/compiler/sema_expr.cpp` near the existing UnsizedDyn handling (`:3024-3028`). Reuse `is_auto_trait_satisfied` (`sema_auto_trait.cpp:24`). Add `tests/logos/fail/dyn_send_rejected_for_non_send_source.logos`. **Single-session, soundness fix.**
3. **Closure auto-trait propagation.** Replace `sema_auto_trait.cpp:199-201` conservative-false for `Kind::Closure` with a structural walk over the captured types. Requires the closure capture list to be reachable from `TypeRef` (likely via the existing closure environment record). Add `tests/logos/pass/closure_send_when_captures_send.logos` and the fail counterpart. **Single-session, unblocks future closure-spawn API.**
4. **Document the `Arc` Send/Sync omission** in `docs/DIVERGENCES.md` §B as the immediate stopgap if (1) isn't done same-day. Per the project rule (`feedback_no_defer_fix_now_generalize`), prefer (1) over (4).
5. **Doc hygiene for §A4:** in `tools/peg_gen/grammars/logos.peg:356-359` add a cross-ref to `DIVERGENCES.md` §A4 so the keyword reservation is auditable. Confirm or remove `Unpin` / `PhantomPinned` placeholders at `stdlib/lang/marker/marker.logos:21-31` based on whether any imported test names them.
6. **(Deferred-with-rationale, not "indefinite")** Closure-spawn API `thread::spawn<F: FnOnce() + Send + 'static>(f: F)` once Fn-family traits land. Pre-requisite is the closure auto-trait propagation in (3) — without it the bound is unsatisfiable.
