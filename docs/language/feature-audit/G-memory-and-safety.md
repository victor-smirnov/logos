# Category G — Memory and safety (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`).

Summary: 3 features audited — 0 fully OK, 2 WARN (interior mutability, variables), 1 GAP (memory model / atomics: API present but no compiler-side memory model). Headline gaps: (a) borrow checker does NOT know `UnsafeCell<T>` is the only legal way to mutate through a shared reference — interior mutability "works" only because every Cell/RefCell method bodies are wrapped in `unsafe { … }` that the BC trusts; (b) `static mut` is intentionally absent and there is no `'static` lifetime distinction for true statics (module-level `static NAME: T = expr;` collapses to `CONST_DEF`); (c) atomics ship as a stdlib API but the compiler has no atomic intrinsic / memory model — all five Rust `Ordering` variants lower to `seq_cst` on x86 today, with no codegen pathway for weaker targets; (d) no `&UnsafeCell<T>` carve-out in variance / aliasing rules → variance-cell-of-cell imported tests pass for the wrong reason.

---

## G.1 Interior mutability

### Rust nomenclature
`UnsafeCell<T>` (foundation), `Cell<T>`, `RefCell<T>`, `Mutex<T>`, `RwLock<T>`, `OnceCell<T>`, atomics; the language-level concept "interior mutability" (`/home/victor/cxx/reference/src/interior-mutability.md`). Key spec rules:
- `interior-mut.unsafe-cell`: `UnsafeCell<T>` is the **only** type for which `&UnsafeCell<T>` may legitimately hand out a writable view.
- `interior-mut.mut-unsafe-cell`: multiple `&mut UnsafeCell<T>` is still UB.
- `interior-mut.abstraction`: other interior-mut types are built `UnsafeCell<T>`-as-field.
- Cited by variance (`Cell<T>` is invariant in `T`), Send/Sync (`Cell` is `!Sync`), drop ordering.

### Logos nomenclature
Logos uses the **identical** Rust spelling at the stdlib level:
- `UnsafeCell<T>` at `stdlib/lang/cell/cell.logos:36` (declared `#[repr(transparent)]`, single `value: T` field) with `new` / `into_inner` / `get(&self) -> *mut T` / `get_mut(&mut self) -> &mut T` / `raw_get(this: *const Self) -> *mut T`.
- `Cell<T>` (Copy-bound safe wrapper) at `stdlib/lang/cell/cell.logos:102`.
- `RefCell<T>` + `Ref<T>` / `RefMut<T>` guards at `stdlib/lang/cell/cell.logos:202-341`, with runtime borrow counter (i32 state: 0 / n>0 shared / -1 exclusive), `try_borrow` / `borrow` / `try_borrow_mut` / `borrow_mut`, drop-based release of the counter.
- `OnceCell<T>` (`stdlib/lang/cell/cell.logos:432`) with `set` / `get` / `get_or_init`.
- `LazyCell<T>` (`stdlib/lang/cell/cell.logos:509`) — fn-ptr (not closure) initializer.
- `Mutex<T>` / `RwLock<T>` at `stdlib/std/sync/sync.logos:46, 100` (pthread-backed; `unsafe fn lock(self: *mut Mutex<T>) -> *mut T`).
- Atomics at `stdlib/lang/atomic/atomic.logos` — see G.2.

The compiler-side has **no** dedicated handling: grep `interior\|UnsafeCell` across `src/compiler/` returns zero matches outside test fixtures. The borrow checker is unaware of the `UnsafeCell` foundation.

### Match verdict
**WARN — surface conformant, semantics lax.**

Naming is byte-identical to Rust (no renames needed). What diverges:

1. **Borrow checker has no `UnsafeCell` rule.** Rust's borrow checker carves out `UnsafeCell<T>` so that `&UnsafeCell<T>` does not forbid writes; Logos's BC achieves the same outcome by trusting whatever lives inside `unsafe { … }` blocks (every Cell/RefCell method body is `unsafe`). The user observable behaviour matches, but the rule is enforced by hand-rolled escape hatches in stdlib instead of a typed BC carve-out. Misuse of a hand-rolled `UnsafeCell`-equivalent struct (e.g. `struct MyHack { value: T }` with `unsafe { (&self.value as *const T as *mut T) }`) compiles indistinguishably from `UnsafeCell` — there is no soundness boundary.

2. **Variance / invariance not modeled.** Rust requires `Cell<T>` to be invariant in `T`. The imported tests `tests/imported/fail/variance/variance-cell-is-invariant.logos` and `variance-cell-of-cell.logos` exist; verify they actually fail for the right reason — the `.expected` files exist but Logos's variance pass (B-ts subsystem) does not consult `UnsafeCell`. Likely they fail incidentally rather than via "Cell is invariant".

3. **Drop in interior-mutable contexts.** Rust allows `RefCell<T>` to drop its `T` even when shared `&RefCell` exists. Logos's drop elaboration uses field-recursive drop; this works for `RefCell::into_inner` (consuming) but the `replace` / `swap` paths do raw-pointer moves of `T` without registering a drop on the displaced value — see `cell.logos:291-308`. Confirm via valgrind on a non-Copy `T`.

### Implementation pointer
- Stdlib: `stdlib/lang/cell/cell.logos:36` (`UnsafeCell`), `:102` (`Cell`), `:202` (`RefCell`), `:432` (`OnceCell`), `:509` (`LazyCell`).
- Sync stdlib: `stdlib/std/sync/sync.logos:46` (`Mutex`), `:100` (`RwLock`).
- Compiler treatment of `unsafe { ... }`: `src/compiler/sema_stmt.cpp:447, 515` (`is_mut_ref && !inside_unsafe_` check on raw-ptr deref); `src/compiler/sema_decl.cpp:662` (`fi_ptr->is_unsafe` toggles `inside_unsafe_`).
- Borrow checker: zero `UnsafeCell`-specific code (grep `UnsafeCell\|interior` across `src/compiler/` returns 0 outside this audit).
- Tests: `tests/logos/pass/cell_unsafe_basic.logos`, `cell_unsafe_methods.logos`, `cell_unsafe_shared_mut.logos`, `cell_basic.logos`, `refcell_basic.logos`, `oncecell_basic.logos`, `lazycell_basic.logos`, `ref_map_basic.logos`; imported `tests/imported/pass/cell/test_harness_coretest_unsafe_cell.logos`.

### Interactions check
Direct neighbours from the table for "Interior mutability":

- **`UnsafeCell` (foundation) — WARN.** Type exists at `stdlib/lang/cell/cell.logos:36`; the compiler does NOT treat it as a `#[lang_item]`. The cell module comment (`cell.logos:14-21`) explicitly notes: "no compiler magic, no `#[lang_item]` registration. … Logos's borrow checker doesn't yet track interior-mutability divergence; matching Rust's exact rules is a separate workstream." This is a known divergence. The blessed-divergence register (`docs/DIVERGENCES.md`) does NOT list it, so per the "everything-else is a TODO" rule it is a catch-up gap.
- **`Cell` / `RefCell` / `Mutex` / `RwLock` / atomics — OK (presence)**, **WARN (semantics)**. All ship as named structs with Rust-shape APIs. Mutex/RwLock guard methods are `unsafe fn` (`stdlib/std/sync/sync.logos:62, 77, 116, 131`) — Rust's `lock()` is safe; the divergence (raw `*mut T` instead of RAII `MutexGuard<'_, T>`) is documented (`stdlib/std/sync/sync.logos` header comment) and matches the "no Fn-family + no lifetimes-in-stdlib-guards" gap. There is no `AtomicUsize` / `AtomicIsize` (size-polymorphic atomic) but all width-specialised forms exist.
- **`&T` (mutate through shared) — WARN.** Works only because every Cell/RefCell method that performs the mutation wraps the cast in `unsafe { ... }`. The borrow checker permits the write because it's inside `unsafe`, not because the receiver is `&UnsafeCell<T>`. A program that takes `&u32` and casts it to `*mut u32` inside `unsafe { ... }` compiles identically — Logos has no notion of "freeze-on-shared-ref" outside the BC's exclusivity check.
- **Sync/Send — OK (manually declared).** `unsafe impl<T: Send> Send/Sync for Mutex<T>` at `stdlib/std/sync/sync.logos:85-86`; `unsafe impl<T: Send> Send for RwLock<T>` `:152-153`. Auto-trait propagation is wired (see Category H audit), but Logos does NOT auto-derive `!Sync` for `Cell<T>` / `RefCell<T>` — there is no negative-impl machinery. Rust derives `!Sync` automatically because `UnsafeCell<T>` is `!Sync`. Logos would need either (a) an `UnsafeCell` lang-item carrying `!Sync`, or (b) explicit `unsafe impl !Sync for Cell<T>` in stdlib (negative impls — grammar gap). Today `Cell<i32>` is technically `Sync` in Logos, which is UB-permissive.
- **Coercions — n/a.** `&UnsafeCell<T> → *mut T` is via the inherent `.get()` method, not a built-in coercion.
- **Drop (interior mut drops in shared ref) — WARN.** `RefCell::replace` (`cell.logos:291-298`) does `*g.value = t;` after reading old — the read uses dereference of a `*mut T` to a value-typed `T`. For a non-Copy `T`, the displaced old is moved out cleanly and `t` is dropped into the slot (no double-free) only if the compiler's destination-passing of `*g.value = t;` reliably uses drop-before-replace. The `cell.logos:303-308` `swap` path does a 3-step `tmp = *a; *a = *b; *b = tmp` — for non-Copy `T` this is two moves that need drop suppression to be sound; today the compiler's `*p = v` assignment lowering at `src/compiler/sema_stmt.cpp` does drop-before-replace, but moves via dereferenced raw pointer are not exercised by the BC's drop-elaboration. Soundness here relies on the `unsafe` author writing only Copy `T` swaps in practice.

### Gaps / debt
- Add `UnsafeCell` lang-item recognition (file:line target: `src/compiler/sema_collect.cpp` lang-item table). Mark its single field as the "interior-mutability source"; downstream variance / Sync / freeze rules consult it.
- Variance pass should treat `UnsafeCell<T>` as invariant in `T` (so `Cell<T>` / `RefCell<T>` inherit invariance via the field). Verify `tests/imported/fail/variance/variance-cell-is-invariant.logos` actually fails for the right reason — currently passes-or-fails depend on the variance pass that does not look up `UnsafeCell` membership.
- Negative-impl surface (`unsafe impl !Sync for Cell<T>`) or an auto-`!Sync` rule for any struct containing `UnsafeCell`. Today `Cell` / `RefCell` are unsoundly `Sync` per the auto-trait propagation.
- BC carve-out: `&UnsafeCell<T>::get() -> *mut T` should be the ONLY way to obtain a writable pointer through a shared reference; today any `unsafe { &x as *const T as *mut T }` works.
- Mutex/RwLock should return RAII `MutexGuard<'a, T>` once lifetimes-in-stdlib-guards / Fn-family land; tracked as a catch-up under "Fn-family + named lifetimes".
- `try_borrow` / `try_borrow_mut` use i32 state where Rust uses `Cell<BorrowFlag>`. Cosmetic, but means `RefCell::borrow()` from multiple threads (which would be unsound anyway since `RefCell: !Sync` is unrecognised) has racy state updates not flagged by the type system.
- Document the divergence: add a §B row to `docs/DIVERGENCES.md` titled "Interior mutability — BC has no `UnsafeCell` carve-out" so it is not lost to the catch-up register.

---

## G.2 Memory model / atomics

### Rust nomenclature
"Memory model" (`/home/victor/cxx/reference/src/memory-model.md`) — currently a stub citing bytes with initialized-or-uninitialized state and optional provenance. Atomics: `core::sync::atomic::{AtomicBool, AtomicI8..I64, AtomicU8..U64, AtomicIsize, AtomicUsize, AtomicPtr<T>}` and the `Ordering` enum (`Relaxed`, `Acquire`, `Release`, `AcqRel`, `SeqCst`).

### Logos nomenclature
- `Ordering` enum at `stdlib/lang/atomic/atomic.logos:39` with the five Rust variants (`Relaxed`, `Acquire`, `Release`, `AcqRel`, `SeqCst`).
- Atomic types at `stdlib/lang/atomic/atomic.logos`:
  - `AtomicI32` / `AtomicU32` (`:67, 112`)
  - `AtomicI64` / `AtomicU64` (`:155, 196`)
  - `AtomicI8` / `AtomicI16` / `AtomicU8` / `AtomicU16` (`:319, 355, 391, 427`) — backed by i32 storage with widening
  - `AtomicBool` (`:237`) — backed by i32 (0/1)
  - `AtomicPtr<T>` (`:279`) — backed by i64 bits
- Primitives via x86-64 hand-asm: `logos_atomic_load32`, `logos_atomic_store32`, `logos_atomic_fetch_add32`, `logos_atomic_cas32` and the 64-bit twins, all declared `extern fn` (`atomic.logos:48-56`). Backing assembly is in `stdlib/rt/fiber_ctx.S` per the header comment (`atomic.logos:3`).
- API methods: `load`, `store`, `fetch_add`, `fetch_sub`, `compare_exchange`, plus `_ordered` variants taking an `Ordering` arg.

The compiler has **no** atomic-aware lowering; grep `atomic` across `src/compiler/` returns zero matches (compared to e.g. `Box`, `Drop`, `Send`). Atomic ops are ordinary `extern fn` calls into x86 LOCK-prefixed asm stubs.

### Match verdict
**GAP — runtime present, language-level memory model absent.**

The API mirrors Rust closely (same `Ordering` variants, same method names, same width-specialised types). What is missing:

1. **No compiler intrinsic — every ordering currently lowers to seq-cst.** Header comment (`atomic.logos:13-21`) is candid: "On the x86-64 backend every ordering currently lowers to the same seq-cst primitive (TSO + LOCK-prefixed RMW already provides acquire / release semantics for plain loads / stores; LOCK-RMW provides full SeqCst). On weaker-memory targets (AArch64, RISC-V) the ordering would route to dmb / lr-sc variants — that's a backend codegen change, not an API change." On x86 this is observationally correct; off-x86 it is unsound.
2. **Ordering arg is unused (`_ord: Ordering`).** Every `_ordered` method signature accepts the enum but the body discards it. There is no `match ord { Relaxed => …; Acquire => …; }` lowering — see `atomic.logos:92-107`. So even on x86, Logos can never weaken to `Relaxed` for performance.
3. **No `AtomicUsize` / `AtomicIsize`** — size-polymorphic atomic widths are stdlib gaps (forces user code to pick `AtomicU64` on 64-bit hosts). Not in any imported divergence list.
4. **No language memory model document.** Rust's spec is itself a stub; Logos matches that by saying nothing. But Logos's `unsafe` is also under-specified vs Rust's UB list (`behavior-considered-undefined.md`).
5. **Provenance: not modeled.** Spec `memory.bytes.init` mentions optional provenance; Logos's borrow checker has a `provenance` notion at `src/compiler/borrow_check.cpp:173, 312` but it's about lifetime-region tracking, not byte-level pointer provenance. Raw ptr ↔ integer round-trips through `as` casts work freely (see Box::from_raw / Mutex receivers).
6. **Drop ordering** — Logos's drop elaboration runs reverse-declaration order, matching Rust. Drop of `Atomic*` is a no-op (no destructor) — same as Rust.

### Implementation pointer
- Atomics surface: `stdlib/lang/atomic/atomic.logos` (462 lines).
- ASM stubs: `stdlib/rt/fiber_ctx.S` (per header reference; not inspected here, file:line not memorized).
- Tests: `tests/logos/pass/atomic_basic.logos`, `tests/logos/pass/atomic_narrow.logos`, `tests/logos/pass/atomic_ordering.logos`.
- Compiler: zero atomic-specific code paths (grep `atomic\|Atomic` in `src/compiler/` returns hits only in unrelated identifier substrings).

### Interactions check
Direct neighbours from the table for "Memory model / atomics":

- **`UnsafeCell` — WARN.** Atomic types do NOT use `UnsafeCell<T>` internally — they wrap a raw value (`AtomicI32 { val: i32 }`, `atomic.logos:67`). Rust's atomics wrap `UnsafeCell<T>` (so they're `!Send`/conditional-`Send` per `T`). Logos's atomics skip the wrap and rely on the asm stubs being the only mutation path. Result: aliasing through `&AtomicI32` and `*mut AtomicI32` is not type-distinguished. For the atomic API used as intended, identical observable behaviour; for hand-rolled mixed-mode (read raw `val` field directly), Logos cannot diagnose.
- **Atomics (`std::sync::atomic`) — OK (presence)**, **GAP (Ordering plumbing).** Eight atomic widths, Ordering enum, `_ordered` variants — all present. Ordering arg discarded at lowering site: `atomic.logos:92-107` (`_ord: Ordering` underscore-prefixed).
- **Send/Sync — PARTIAL.** No `unsafe impl Send/Sync for AtomicI32` block in stdlib — relies on auto-trait propagation finding `val: i32`, which IS Send+Sync (primitives auto-impl both). So atomics are Send+Sync by happy accident. Rust derives them explicitly. Cosmetic gap.
- **`unsafe` (raw access) — OK.** Every method body opens an `unsafe { … }` to call the extern asm fn. Atomic methods themselves are NOT `unsafe fn` (matching Rust). The atomic-load/store/cas asm calls cross extern-FFI but that's safe per the well-formed-extern-fn rule (G-FFI handles).
- **Drop ordering — n/a.** No nontrivial destructor on atomic types; Drop is auto-empty.

### Gaps / debt
- **Compiler intrinsic for atomics.** Replace the `extern fn logos_atomic_*` stubs with MLIR `memref.atomic_*` or LLVM `__atomic_*` intrinsics so backend can lower per-target. Wire the `Ordering` arg through. Estimated single-session sprint.
- **Atomic widths.** Add `AtomicUsize` / `AtomicIsize` (target-pointer-width).
- **`unsafe impl Send/Sync`** for every atomic type (explicit not incidental).
- **Wrap atomic storage in `UnsafeCell<T>` once `UnsafeCell` is a lang-item** (G.1 prerequisite). Today `AtomicI32 { val: i32 }` raw-stores; should be `AtomicI32 { val: UnsafeCell<i32> }`.
- **Drop ordering across atomics interaction with Arc weak count**: `stdlib/mem/sync/arc.logos` uses atomic refcount via the same asm primitives; weak-count drop and strong-count drop order matters for "last drop frees inner". Not separately tested.
- **Document memory model.** Even a one-screen `docs/MEMORY-MODEL.md` mirroring Rust's bytes-and-provenance stub would unblock atomic intrinsic design.
- **Add `'static + Send + Sync` requirement for `static FOO: AtomicI32 = …` once `static` items get true static storage** (G.3 prerequisite).

---

## G.3 Variables (mutability, scope)

### Rust nomenclature
Spec `/home/victor/cxx/reference/src/variables.md`:
- `variable.local` — stack-allocated local; lives in the stack frame.
- `variable.local-mut` — immutable unless `let mut`.
- `variable.param-mut` — fn / closure params immutable unless `mut`-bound.
- `variable.init` — frame is uninitialized on entry; uses must follow assignment on all reachable paths.

Related spec sections (interaction edges): `statements.md` (let), `destructors.md` (drop scope), `expressions.md` §temporaries.

### Logos nomenclature
- Local variable: introduced via `KW_LET IDENT …` (`tools/peg_gen/grammars/logos.peg:2026`) — grammar code `LET = 21`.
- Mutability flag: AST schema slot `IS_MUT = 25` (`logos.peg:45`).
- `let mut` grammar: `logos.peg:2026` (`KW_LET KW_MUT IDENT COLON type_ref ASSIGN expr SEMI`), `:2028` (declare-without-init form).
- Declare-without-init: `let x: T;` permitted (`logos.peg:2028`), tracked in sema by `decl_uninit_vars_` set (`src/compiler/sema_stmt.cpp:1684-1687`); a later `x = …` does NOT drop-before-replace.
- Mutable fn param: `mut x: T` (`logos.peg:1215`) sets `IS_MUT: true` on PARAM. Sema desugars at `src/compiler/sema_decl.cpp:610-626` into an immutable synth param `__mutparam_<mangled>_<i>` + prologue `let mut x = synth;`.
- Mutable closure param: same shape (`|mut x|` / `|mut x: T|`), landed 2026-05-22 per `DIVERGENCES.md:76`.
- Binding table: `define(name, type, is_mut)` in sema_impl scope (`src/compiler/sema_decl.cpp:623, 628`, `sema_stmt.cpp:1509`); `lookup_is_mut(name)` queries (`sema_stmt.cpp:785, 1997, 2112, 2234, 6215, 6336, 6403`).
- Borrow checker tracks `is_mut_binding` per local (`src/compiler/borrow_check.cpp:157, 530, 578, 1541`).
- Shadowing: handled implicitly at `define()` — later `let x = …` overrides the same slot; sema clears `decl_uninit_vars_.erase(name)` on re-declaration with value (`sema_stmt.cpp:1664`). Closure-capture snapshot/restore at `src/compiler/mlir_gen_expr.cpp:4726-4746`.
- Static items: `static NAME: T = expr;` (`logos.peg:670`) currently produces a `CONST_DEF` AST node — i.e. routed identically to `const NAME: T = expr;`. `static mut` is explicitly NOT in the grammar (`logos.peg:667`: "`static mut` (true mutable global storage) is a distinct mechanism, still rejected, so it is intentionally NOT matched here").

### Match verdict
**WARN — local-variable surface matches Rust; static items collapse to const; no `static mut`.**

Concretely:
1. `let mut` / `mut x: T` params / re-declaration shadowing — all work and pass tests.
2. `let x: T;` declare-without-init is allowed but **definite-assignment analysis is partial**. Comment at `sema_stmt.cpp:1682-1683`: "Full definite-assignment analysis is a separate pass; for now we trust user code or rely on later use-checks." Spec rule `variable.init` ("can be used only after initialized through all reachable control flow paths") is not enforced; an unreachable-path use surfaces as a runtime UB or mlir-gen error ("use of uninitialised slot"), not a compile error with a clean diagnostic.
3. `static NAME: T = expr;` lowers to `CONST_DEF` — same immutable-global storage model. No distinct backing storage; can't take a stable address.
4. `static mut NAME: T = …` not in grammar (intentional). Rust marks reads/writes as `unsafe`. Logos has no replacement story; cross-thread shared mutable state must go via `Mutex<T>` / atomics in a stack-or-heap allocated container, not a top-level `static mut`.

### Implementation pointer
- Grammar: `tools/peg_gen/grammars/logos.peg:2015-2034` (`let_stmt`, all `let mut` variants), `:1199-1215` (PARAM `IS_MUT`), `:660-671` (`CONST_DEF` covers both `let` and `static`).
- Schema: `logos.peg:45` (`IS_MUT = 25`).
- Sema scope tracking: `src/compiler/sema_decl.cpp:610-626` (mut fn param desugar), `src/compiler/sema_stmt.cpp:1509-1687` (let binding); `define()` / `lookup_is_mut()` in `sema_impl.hpp`.
- Borrow check: `src/compiler/borrow_check.cpp:157` (`is_mut_binding` field), `:523-578` (`is_mut_binding`-gated reborrow checks), `:1541` (`is_mut_binding` set on bind).
- Declare-uninit tracking: `src/compiler/sema_stmt.cpp:1684-1687` (`decl_uninit_vars_` set), `:1664` (clear on re-let-with-value), `:2412` (drop-suppression for declared-uninit on reassign).
- Closure-shadow snapshot: `src/compiler/mlir_gen_expr.cpp:4726-4746`.

### Interactions check
Direct neighbours from the table for "Variables":

- **`let` — OK.** Three grammar forms (with init, declare-only, `let mut`); sema handles each (`lower_let` at `sema_stmt.cpp` around the `LET` code-switch).
- **Drop scope — OK.** Drop-on-block-end is handled by collect_drops in mlir-gen, scope-stack tracks bindings; mutability not relevant to drop. Shadowing semantics — when `let x = …; let x = …;` rebinds, the old `x` is dropped at end of the enclosing block, not at the shadow point (matches Rust's "shadowed but still live until scope ends" rule). Verify with valgrind; today implicit per `mlir_gen_expr.cpp:4726-4746` snapshot.
- **Move/Borrow — OK.** `lookup_is_mut(name)` gates `name = …` reassignment (sema_stmt.cpp:785, 1997, 6215); `is_mut_binding` gates `&mut name` (`borrow_check.cpp:530, 578`). Pattern bindings inherit mut-ness via `current_pat_mut_names_`.
- **`mut` binding — OK.** Surface `let mut x`, `mut x: T`, `|mut x|` all produce IS_MUT=1; sema/borrow-check consult it.
- **Reborrow (let-coerce site) — OK.** Implicit reborrow at `let y: &T = &mut_ref_expr;` handled by coercion rules; `is_mut_binding` of the LHS is irrelevant for the *type* but matters for downstream `&mut y` (which would re-fail).
- **Shadowing — OK / DIVERGENT (minor).** Logos allows shadowing in the same scope, matching Rust. A subtle divergence: when a `let x: T;` is re-shadowed later by `let x = v;`, the *uninit-tracker* `decl_uninit_vars_` is cleared (`sema_stmt.cpp:1664`), which means the first slot's drop-suppression carries over. Likely sound but tested only by `tests/logos/pass/*` shadow shapes; no targeted shadow-with-uninit-redeclare test.
- **`'static` for true statics — GAP.** Spec `alloc.static`: items have static storage with whole-program lifetime. Logos's `static NAME: T = expr;` collapses to `CONST_DEF` which is treated as an inlined value — no stable address means `&NAME` cannot be returned past the const expression's eval. Lifetime resolver treats `'static` as a token name (`sema_decl.cpp:26-1008` — multiple `lt == "'static" || lt == "static"` checks) but there is no actual program-lifetime storage class.

### Gaps / debt
- **Definite-assignment analysis.** `sema_stmt.cpp:1682-1683` explicitly defers it. Implement it (Rust does this in MIR; Logos can do it on LIR or in mlir-gen pre-codegen). Required for spec `variable.init` conformance.
- **True `static` items.** Today `static NAME: T = …` is `CONST_DEF` — implies inlining at use site; `&NAME` returning past scope is not guaranteed (the `'static` lifetime fix-up is name-based, not storage-based). Promote to a distinct `STATIC_DEF` AST code with module-global storage and a stable symbol.
- **`static mut`.** Intentionally absent — confirm in `docs/DIVERGENCES.md` (currently not listed as §A divergence). If the Rust catch-up policy says "must converge", flag as a B-row gap with the workaround "use `static FOO: Mutex<T>` once true `static` is implemented".
- **Shadow rules: drop-at-shadow vs drop-at-scope-end.** Test that `let x = String::new(); let x = 5;` drops the original `String` at the enclosing block end (Rust behaviour), NOT at the second `let`. Currently no targeted test.
- **`#[no_mangle]` / linkage attrs.** Spec edge from `static` ↔ Linkage. Logos has no `#[no_mangle]` analogue for statics (it has for fns — `attribute.no_mangle` per items/static-items.md edge).
- **Drop of shadowed value with the same name** — confirm `current_pat_mut_names_` doesn't lose the prior-binding mut bit when shadowed by a pattern destructure.

---

## Cross-category gaps

- **`'static` lifetime as a true storage class** — owned jointly by G.3 (this audit) and Category A (Lifetimes). Today resolved by string-match (`lt == "'static" || lt == "static"`). Fix needs an outlives-graph anchor.
- **Negative impls (`impl !Sync for Cell<T>`)** — owned jointly by G.1 (this audit) and Category H (Send/Sync). The whole interior-mutability `!Sync` story collapses without it.
- **Variance pass consults `UnsafeCell`** — owned jointly by G.1 and Category A (Variance & subtyping). `tests/imported/fail/variance/variance-cell-*.logos` exists but tests the wrong axis until variance pass acquires the carve-out.
- **Pointer provenance** — owned jointly by G.2 and Category K (Unsafe / UB list). Atomic-aware codegen will need a provenance story for `AtomicPtr<T>`.
- **Atomic ordering plumbing** — owned jointly by G.2 and Category B (Type system primitives — pattern-match dispatch on `Ordering` variants) and Category N (FFI / extern fn) for the asm-stub replacement by LLVM intrinsics.
- **Pthread guards return RAII guard structs** — touches G.1 (Mutex/RwLock surface) and the unlanded "Fn-family + lifetime-bound stdlib guards" workstream.

---

## Recommended next moves

Sized for a single-session sprint each. Pick one per session.

1. **(High impact, ~1 session) Atomic intrinsic + Ordering plumbing.** Replace `logos_atomic_load32`/`store32`/`fetch_add32`/`cas32` extern fns with MLIR `atomic_*` ops driven by the `Ordering` arg. Wire `Ordering::Relaxed` → `monotonic`, `Acquire` → `acquire`, etc. Tests already exist (`tests/logos/pass/atomic_basic.logos`, `atomic_ordering.logos`); add a Relaxed-vs-SeqCst codegen-divergence check (disassemble to verify Relaxed lowers to a plain load on x86, not a `MFENCE`-bracketed sequence). Unblocks the AArch64 / RISC-V backend port.

2. **(Soundness, ~1 session) `UnsafeCell` lang-item + variance carve-out.** Add `lang_item: UnsafeCell` recognition in `src/compiler/sema_collect.cpp` lang-item table; mark its inner field; teach the variance pass to treat any struct with an `UnsafeCell<T>` field as invariant in T. Verify `tests/imported/fail/variance/variance-cell-of-cell.logos` and `variance-cell-is-invariant.logos` fail for the *right* reason (capture diagnostic text). Pre-req for the `!Sync` derivation work.

3. **(Surface conformance, ~½ session) `static NAME: T` distinct AST code.** Split `CONST_DEF` consumption of `KW_STATIC` into `STATIC_DEF` (new schema code), with module-global storage. Keep semantics equivalent for the immutable case; gate `&NAME` taking-address through the new `'static`-storage anchor. Sets up the future `static mut` (or its `Mutex<T>` replacement) without breaking `'static` lifetime resolution.

4. **(Diagnostics, ~1 session) Definite-assignment analysis pass.** Implement a CFG walk that proves "every read of x is dominated by an assignment of x". Today's `decl_uninit_vars_` set is a one-shot marker; replace with proper join-on-merge flow. Spec rule `variable.init` becomes enforced; the half-init example in the spec produces a clean error instead of an mlir-gen "use of uninitialised slot" panic.

5. **(Doc, ~½ session) Add G-row to `docs/DIVERGENCES.md`** for: "BC has no UnsafeCell carve-out", "Cell/RefCell are incidentally Sync", "atomics ignore Ordering on x86 (sound) and everywhere else (unsound)". Mirroring B-row format; these are catch-up TODOs, not §A blessed divergences. Prevents drift.
