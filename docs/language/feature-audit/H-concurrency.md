# Category H — Concurrency (audit)

v2 — re-audited 2026-06-12. v1 generated 2026-05-30. Spec: rust-lang/reference (local `/home/victor/cxx/reference`).

Summary: 2 features — 1 WARN (`Send`/`Sync`), 1 blessed divergence (`async` → fibres, §A4; no parity owed). Since v1: Arc/Weak `unsafe impl Send/Sync` landed (eb894e80); `dyn Trait + Send/Sync` enforced at arg-coercion (fdae52fb); UnsafeCell-rooted `!Sync` derivation landed (5bccc7fc); `Pin<P>` + real auto-trait `Unpin` landed (6dabfe99, §A8). v2 headline residuals, probe-confirmed 2026-06-12: (a) **closure auto-trait check walks PARAMETER types, not capture types** — a closure capturing `*mut i32` passes `T: Send` (v1's conservative-false flipped to unsound-true); (b) `dyn + Send` is enforced only at call-arg coercion — `let r: &dyn Speak + Send = &not_send;` compiles; (c) static-item `Sync` constraint unenforced; (d) `thread_spawn` is still a raw-fn-ptr API with no `Send` bound.

---

## H.1 `Send` / `Sync`

### Spec anchor
`special-types-and-traits.md` `lang-types.{send,sync}`, `lang-types.auto-traits.{auto-impl,builtin-composite,fn-item-pointer,aggregate,closure,generic-impl,negative,trait-object-marker}`, `lang-types.sync.static-constraint`.

### Match verdict
**WARN — engine conformant for structs/enums/refs/containers; closure rule wrong-axis; enforcement-site coverage partial.**

Engine (`src/compiler/sema_auto_trait.cpp:24-256`): byte-identical Rust names (`pub auto trait Send/Sync`, `stdlib/lang/marker/marker.logos:17-18`); structural recursion with cycle guard; `&T: Send ⇔ T: Sync` (`:121-126`); `&[T]` checks `T: Sync` (Bug-2 fix); generic-impl bound honoring (`:58-84` shape); negative impls flip verdicts; field-precise diagnostics. Also hosts `Unpin` (default-true; `PhantomPinned`/`#[pinned]` → `!Unpin`; refs/ptrs always `Unpin` — `:101-126, 161-176`; commit 6dabfe99).

Closed since v1:
- ✅ **`Arc<T>`/`Weak<T>` Send/Sync (commit eb894e80).** `unsafe impl<T: ?Sized + Send + Sync> Send/Sync for Arc/Weak<T>` (`stdlib/mem/sync/arc.logos:60-63`). Probe: `need_send::<Arc<i32>>(arc_new(7))` compiles and runs.
- ✅ **`dyn Trait + Auto` enforced at arg-coercion (commit fdae52fb, §2.4c).** Grammar emits `AUTO_TRAIT_BOUND` nodes; TraitObject `const_val` bits 8/9 = `+Send`/`+Sync`, folded into TypeUID (so `&dyn T` ≠ `&dyn T + Send`); `check_dyn_auto_bounds_at_coercion` (`src/compiler/sema_expr.cpp:12174-12226`) walks the source pointee via `is_auto_trait_satisfied`. Fail-test `tests/logos/fail/core_2_4c_dyn_send_violation.logos`.
- ✅ **`UnsafeCell<T>: !Sync` derivation (commit 5bccc7fc, §2.2)** — closes v1's "Cell/RefCell incidentally Sync"; probe-rejected with field-precise diagnostic (see Category G.1).
- ✅ **`RwLock` Sync bound corrected**: `unsafe impl<T: Send + Sync> Sync for RwLock<T>` (`stdlib/std/sync/sync.logos:153`; v1 had `T: Send` only).

Open gaps:
1. **UNSOUND (v2 probe): closure capture walk checks the wrong list.** `sema_auto_trait.cpp:242-246` (`Kind::Closure`) iterates `tv.closure_params()` — which are the closure's *parameter* types (`make_closure_type(param_types, ret_type)`, `sema_expr.cpp:14203`; "FnPtr-style", `sema.hpp:120`), not its captures. Probe: `let bad = || unsafe { *p };` capturing `p: *mut i32` **passes** `need_send::<F>(bad)`; `need_send(p)` directly is correctly rejected. Spec `lang-types.auto-traits.closure` requires capture-type propagation. The §2.4(a) scoreboard row ("closures walk capture types") is wrong — v1's conservative-false became permissive-true, trading a usability bug for a soundness bug. Fix: thread the capture-type list into the Closure TypeRef (or resolve captures at the bound-check site) and walk that; captures-by-`&` must check `&T`'s rule per spec.
2. **`dyn + Send` enforcement is arg-coercion-only.** `check_dyn_auto_bounds_at_coercion` is called solely from the arg-coercion path (`sema_expr.cpp:12171`). Probe: `let r: &dyn Speak + Send = &not_send;` (source holds `*mut u8`) compiles silently; return-position and struct-field-init coercions presumably likewise. Fix: invoke the same check from every unsize site (let-coerce, return-coerce, field init) — the helper is site-agnostic already.
3. **Static-item `Sync` unenforced** (`lang-types.sync.static-constraint`). Probe: `static G: NotSync` (struct with `*mut u8`) accepted. Now actionable — statics are real items (G.3); add the `is_auto_trait_satisfied(ty,"Sync")` check at static collection. Blocked-on/with the S25 static-storage fix (G.3) for full semantics.
4. **`thread_spawn(start: *const u8, arg: *mut u8)`** (`stdlib/std/thread/thread.logos:59`) — unchanged raw-fn-ptr API, no `F: FnOnce() + Send + 'static` shape, so the auto-trait engine never fires at the spawn boundary. Catch-up, gated on Fn-family traits; gap 1 is its prerequisite (otherwise the bound is wrongly satisfiable).
5. Negative impls accepted anywhere (Rust: stdlib-only, `lang-types.auto-traits.negative`) — more permissive, recorded not blocking.
6. **Tests missing**: closure-with-!Send-capture rejection (would have caught gap 1); dyn+Send at let/return sites; `Mutex<NotSend>` rejection; arc_send pass-test (probe-only today).

### Pointers
Engine `src/compiler/sema_auto_trait.cpp`; mono mirror `mono_clone.cpp` (~4382, ~5248); bound firing `sema_collect.cpp` (~776, ~1024); dyn-bound carry `sema.cpp:4243-4244, 4999`; coercion check `sema_expr.cpp:12174`. Stdlib: `marker.logos:17-25`, `arc.logos:60-63`, `sync.logos:85-86, 152-153`, `thread.logos:59`, `future.logos:23-65`. Tests: `tests/logos/pass/auto_trait_*.logos`, `dyn_auto_trait_bounds.logos`, `fail/auto_trait_*.logos`, `fail/core_2_4c_dyn_send_violation.logos`.

---

## H.2 `async fn` / `async` block

### Spec anchor
`items/functions.md` `items.fn.async.*`, `expressions/await-expr.md` `expr.await.*`.

### Match verdict
**BLESSED DIVERGENCE §A4 — capability replaced by stackful green fibres + reactor; no parity owed.** Unchanged disposition from v1; the supporting surface materially improved:

- ✅ **`Pin<P>` + `Unpin` are now REAL (commit 6dabfe99, §A8).** v1's "parsing shims with no consumer" is obsolete. `logos.lang.pin.Pin<P>` (`stdlib/lang/pin/pin.logos`): `new`/`into_inner` safe only for `T: Unpin`, unsafe `new_unchecked`/`get_unchecked_mut`, `get_ref`/`get_mut`/`as_ref`/`as_mut`, `box_pin`, `Deref` + conditional `DerefMut where T: Unpin`, `Copy` only when `P: Copy` (the blanket-Copy double-free was fixed). `Unpin` is a real auto trait (structural; `PhantomPinned`/`#[pinned]` → `!Unpin`; refs/ptrs always Unpin; negative impls honored) — `sema_auto_trait.cpp:101-176`. Tests: pin_basic, pin_box, pin_unpin_auto, fail/pin_unpin_phantom, fail/pin_get_mut_not_unpin. DIVERGENCES §A8 records the two pinning models coexisting (`#[pinned]` birth-anchored arena types have no safe `Pin::new` path by design).
- `KW_ASYNC`/`KW_AWAIT` remain reserved, productionless (`tools/peg_gen/grammars/logos.peg:368-371`, wasm32/64 stackless-coroutine reservation note). Still no §A4 cross-ref at the grammar site — v1 doc-hygiene item open.
- No `Future`/`IntoFuture`/`Poll` traits (intentional). Replacement runtime unchanged: fibres + reactor (`stdlib/std/rt/fiber/{fiber,reactor,sync,thread}.logos`, `src/reactor/*`), one-shot `FutureSlot<T: Send>` (`future.logos:23-65`) bounding cross-thread completion values by `Send` — the H.1 engine enforces it (subject to H.1 gap 1 for closure-typed `T`).
- `tests/imported/WHY-WE-SKIP.md:17` — `async-await` (461 files) permanently skipped; rationale intact.
- Borrow-across-suspend: still sound via the no-cross-thread-fibre-migration invariant, not a type-level check; re-flag if migration ever lands.

### Residual (doc-hygiene only)
- Cross-ref §A4 from the keyword reservation comment (`logos.peg:368-371`).
- `marker.logos:21-24` comment still says "Logos doesn't ship a Pin<T> ergonomics layer yet" — stale since 6dabfe99.
- `FutureSlot<*mut i32>` (non-Send `T`) rejection has no test.

---

## Cross-category gaps

- **Closure captures in auto-traits (B ↔ H)** — gap 1 needs capture metadata reachable from the closure TypeRef; Category B owns the closure-type structure. Highest-priority H item (soundness).
- **Unsize-coercion site coverage (B ↔ H)** — dyn+Auto check exists; replicate at let/return/field-init coercion chokepoints.
- **Static items (G ↔ H)** — static-Sync enforcement lands with the G.3 static collection pass; storage identity (S25) makes it observable.
- **`'static` + closure-spawn (A ↔ H)** — `thread::spawn<F: FnOnce() + Send + 'static>` awaits Fn-family + named-lifetime bounds; unchanged.
- **`Termination` trait (`lang-types.termination`, v1 missed)** — absent; `fn main() -> i32` is the fixed entry shape. Surface divergence, not blocking; classify as catch-up if `Result`-returning main is ever wanted.
- **`UnwindSafe`/`RefUnwindSafe`** — empty markers, no enforcement; covered by §A7 abort-only panic model (see O-other-panic-divergence.md).

## Recommended next moves

1. **(Soundness, ~1 session) Fix closure auto-trait axis.** Walk capture types (by-ref captures via the `&T` rule), not `closure_params`. Add fail-test closure-captures-`*mut`-rejected + pass-test copy-capture closure. Corrects the §2.4(a) scoreboard row.
2. **(Soundness, ~½ session) dyn+Auto at all unsize sites.** Call `check_dyn_auto_bounds_at_coercion` from let-coerce/return-coerce/field-init paths; add `fail/dyn_send_let_binding.logos`.
3. **(Conformance, rides G.3 S25) static-Sync constraint** at static collection.
4. **(Hygiene, ~½ session)** §A4 cross-ref at `logos.peg:368`; FutureSlot non-Send fail-test; arc_send/Mutex-NotSend tests.
5. **(Deferred-with-prereq)** closure-spawn `thread::spawn` API once Fn-family lands; prereq = (1).
