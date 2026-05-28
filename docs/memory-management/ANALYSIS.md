# Logos Memory Management — Full Analysis (2026-05-26)

Canonical map of how `logosc` manages memory and how every language feature
interacts with it. Read this before touching allocation / drop / move / borrow /
Box / Vec / enum-repr. Target model is **Rust ownership + RAII** (see
`feedback_think_in_rust`); divergences are either the blessed **Zone/Hermes
region** set or **bugs/debt** (flagged ⚠️/🐞 below).

Synthesized from a 5-track code audit (allocation, drop, move/Copy, borrow,
stdlib). File:line refs are to the audited tree.

---

## 0. Mental model — two memory worlds

Logos has **two coexisting memory disciplines**:

1. **Normal program memory** — should follow Rust: stack values + heap behind
   owning handles (Box/Vec/Rc/Arc/String), reclaimed by `Drop`/RAII or move. This
   world is **half-built** (see defects).
2. **Zone / region memory (Hermes)** — the BLESSED divergence. Arena/bump
   allocation; objects inside a zone use relative offsets (`RelPtr`, `AnyVal`,
   `#[zoned]` structs), have **no per-object ownership or destructors**, and the
   whole region is freed in one shot when its `MemHolder` refcount hits 0. The
   boundary is the **fat handle held outside the zone** (`Zone<M>`, `DataRef<T>`,
   `DataOwn<T>`) which carries `*mut MemHolder` and is manually refcounted. Inside
   the zone = pure offset-addressed bytes. This is intentional and internally
   consistent — NOT a staleness issue.

Everything below concerns world (1) unless it says Zone.

---

## 1. Allocation sites (generated code) and their free story

The compiler has exactly **one** `call_malloc` and **one** `call_free` helper
(`mlir_gen_fn.cpp:31/40`). **`call_free` is emitted from generated code in exactly
ONE place: `gen_delete` (`mlir_gen_stmt.cpp:4115`)** — i.e. explicit `delete`. The
automatic Drop machinery NEVER calls `call_free`; it only runs user `impl Drop`
bodies and recurses owned sub-values. So every heap block the *compiler itself*
mallocs leaks unless a user `delete` or a stdlib free fn reaches it.

| # | Allocation | Site | Freed? |
|---|---|---|---|
| A1 | **Enum `{tag,payload}` block** (every enum value — heap-ptr repr) | `mlir_gen_expr.cpp:411` (payload-less), `:439` (with payload) | 🐞 **NEVER** — `gen_drop_value` drops the *payload*, never deallocs the block. **Pervasive leak** (every `Option`/`Result`/user enum). |
| A2 | Enum-slot heap-promotion (store enum into field/`*p`) | `mlir_gen_stmt.cpp:586` (SDerefWrite), `:1813` (gen_field_write) | 🐞 NEVER (same class as A1) — band-aid for dangling, trades bug→leak |
| A3 | Returned `Box<dyn>`/`&dyn` fat `{data,vtable}` | `mlir_gen_stmt.cpp:1159` | ⚠️ only via `Box<dyn>` manual `box_free`; raw `&dyn`/`*mut dyn` leak |
| A4 | Returned slice fat `{ptr,len}` | `mlir_gen_stmt.cpp:1185` | 🐞 NEVER (16 B per slice-returning call) |
| A5 | **dyn vtable array** | `mlir_gen_dyn.cpp:889` (`build_inline_vtable`) | 🐞 NEVER, and **re-mallocs per `as &dyn` site** (not interned) |
| A6 | dyn fat storage `{data,vtable}` | `mlir_gen_dyn.cpp:933` (`coerce_to_dyn`) | ⚠️ only via `Box<dyn>`; raw coercions leak |
| A7 | **Escaping closure environment** (captures, `escapes()`) | `mlir_gen_dyn.cpp:1624` | 🐞 NEVER (every boxed/returned capturing closure) |
| A8 | Class `new` / `delete` | — | ✅ REMOVED (2026-05-27, 929daf5e): the C++-style `new Type{}` expression + `delete x` statement were a non-Rust divergence with ZERO usages — deleted entirely (grammar new-expr/delete-stmt productions, LIR ENew/SDelete, sema lowering, mlir-gen gen_delete/new-codegen, orphaned `var_class_`/class-capture). `fn new()` method-name convention untouched. Heap = `Box` (Rust). No leak vector remains. |
| — | Structs / tuples / arrays / slices / match spills | `create_entry_alloca` | ✅ STACK — reclaimed on frame exit (their droppable *contents* handled by SDrop) |

**stdlib containers** (alloc via `logos.lang.mem` `alloc`/`dealloc`):

| Type | Auto-Drop frees? | Drops elements/`T`? | Notes |
|---|---|---|---|
| String | ✅ | n/a | most modern; OOM/overflow-hardened (`string.logos:271`) |
| Vec<T> | ✅ (`vec.logos:399`) | ✅ element drop-glue | + manual `vec_free`; `into_iter` ptr-zero hack; eager cap-8 |
| Rc<T> / Arc<T> | ✅ block at refcount 0 | ✅ **T's destructor IS run** at the last ref (`93dd38cf`; `drop_rc`/`drop_arc` move `inner.val` out → fires T's Drop before dealloc) | no Weak |
| **Box<T>** | 🐞 **NO** (manual `box_free` only) | no | **stalest**; raw-ptr-self accessors; no `?Sized`/dyn |
| HashMap, Deque | 🐞 NO (manual free only) | — | leak unless freed |

---

## 2. Drop / RAII pipeline

**Sema decides, mlir-gen executes; mlir-gen never auto-drops.** Every drop is an
explicit `SDrop` LIR node inserted by sema.

- **Type classification** (`sema.cpp`): `is_move_type` (2231), `drop_fn_for`
  (2296, package-guarded), `has_droppable_fields` (2374), `needs_drop`
  (=`!drop_fn||droppable_fields`), `compute_auto_copy_types` (2463 — **auto-Copy
  IS implemented**: plain all-Copy-field non-Drop structs join `copy_types_`).
- **Insertion** (`sema_stmt.cpp` `lower_block` ~587): scope-exit `collect_drops`
  (reverse decl order), `return` → hoist value to `__ret_tmp_` then
  `collect_all_drops` (stops at closure boundary), `break/continue` →
  `collect_drops_to_loop`. All skip `moved_vars_` unless in `closure_owned_drop_`.
  `make_drop_stmt` (`sema.cpp:2583`) self-recursion-guards a Drop body and gathers
  `moved_fields`.
- **Mono** (`mono_clone.cpp:3898`) rewrites `__typevar_pending__drop` and re-mangles
  generic drop symbols to concrete.
- **Codegen** (`mlir_gen_stmt.cpp`): `gen_stmt_kind(SDrop)` (415) calls user drop_fn
  + recurses fields/elems/variant-payload (skipping `moved_fields`, refs/ptrs);
  `gen_drop_value` (291) is the recursive workhorse. **Frees nothing itself** —
  dealloc lives only in user `impl Drop` bodies (e.g. `String::drop`→`dealloc`).
  Only compiler-emitted free is `gen_delete` (4098).
- **Skipped from drop**: Copy-payload enums (non-move, `sema.cpp:2271`), Copy
  structs, refs/ptrs, `#[no_auto_drop]`/`ManuallyDrop`, moved values.

---

## 3. Move semantics & Copy

Two **independent** trackers:
- **sema** `moved_vars_` (+ `mark_moved*`, `sema_impl.hpp:1577…`) — *drop
  suppression* (so a moved source isn't double-freed). Drives codegen.
- **borrow_check** `VarState{moved,…}` — *diagnostics* (use-after-move). Soundness
  gate. Its `is_move_type` (`borrow_check.cpp:101`) is **Struct-only/narrower**
  than sema's (tuples/enums/arrays not use-after-move-checked there).

Move points (sema): let/assign RHS, all 4 call paths' by-value move args, by-value
`self` (`track_recv_moved`), struct/tuple/StructLit construction, match scrutinee
(`mark_match_scrutinee_moved`), return value, closure capture
(`closure_owned_drop_` keeps the capture's drop alive). Per-branch `moved_vars_`
snapshot+union merge for if/match.

Copy: primitives, `&T`/`&dyn`, `*const/*mut`, `&[T]`, fn-ptr, trait-object — all
Copy; **`&mut T` NOT Copy** (Rust parity). Auto-Copy for all-Copy plain structs.
By-value params/`self` **do** auto-drop at callee scope exit (older "no param drop"
note is stale).

---

## 4. Borrow checker (more complete than the stale subsystem note)

Runs on LIR, **concrete fns only** (`borrow_check.cpp:2040` — **generic fn bodies
are NOT borrow-checked**; only their specializations). Two analyses: structural
`BorrowChecker` + `RegionInferer` (`region_infer.cpp`).

**Enforced**: use-after-move / partial-move; `&mut` exclusivity vs `&`;
assign-while-borrowed; mut-binding requirement for `&mut` (params whitelisted);
**field-path disjoint borrows** (B83); **two-phase borrows** (B82);
**NLL** (last-use release + region inference); **dropck** (B87); **dangling-ref**
(return ref to local/temp); **named lifetimes + elision + outlives** on returns.
Raw-ptr roots bypass exclusivity (B93.2); `&mut`-roots now also skip the
binding-mut requirement (writing *through* `&mut` needs no `mut`).

**NOT checked (gaps vs Rust)**: generic fn bodies; index/slice **element** borrows
(`arr[i]` aliasing/exclusivity not modeled); reborrows (not first-class);
closure-capture-mode (Fn/FnMut/FnOnce) exclusivity; cross-function lifetime
provenance (intra-procedural only); self-referential structs; move-out-of-deref.

---

## 5. Feature × memory-management interaction matrix

How each language feature touches memory, and its current health.

| Feature | Allocation | Drop/RAII | Move/Copy | Borrow | Status |
|---|---|---|---|---|---|
| **enum** | 🐞 heap `{tag,payload}` per value (A1) | payload dropped, block leaked | Copy-payload enums non-move; droppable-payload move | enums classified Copy in borrow-check (no use-after-move) | 🐞 **heap-repr is the root defect** → leak + promotion hack; target value-repr |
| **struct** | stack alloca | field-recursive drop; auto-Copy | move unless Copy; partial field moves | field-path borrows ✅ | ✅ mostly Rust-like |
| **tuple** | stack | element-recursive drop | move iff any elem move | ok | ✅ |
| **array `[T;N]`** | stack | per-elem drop | move iff elem move; ⚠️ **move-out by index = clean reject** | 🐞 no element-level borrow/exclusivity | ⚠️ element ops limited |
| **slice `&[T]`** | fat {ptr,len}; 🐞 leak when returned (A4) | Copy (no drop) | Copy | 🐞 no element borrow; ⚠️ `&[T]`/`&mut [T]` both `Kind::Slice` (mut not tracked, B6) | ⚠️ |
| **Box<T>** | heap (`box_new`) | 🐞 **no auto-Drop** (manual `box_free`); no `T`-drop | move | ok | 🐞 **stale — modernize** |
| **Vec<T>** | heap, grow | ✅ Drop frees + drops elems | move; `into_iter` ptr-zero hack | IndexMut place-write ✅ | ⚠️ partially modern |
| **Rc/Arc** | heap inner | ✅ block at rc0; ✅ **T dropped** (`93dd38cf`); no Weak | clone = refcount; move | ok | ⚠️ no Weak |
| **String** | heap, grow | ✅ Drop | move | ok | ✅ modern |
| **closure** | env: stack (non-escaping) / 🐞 heap leak (escaping, A7); value `{fn,env}` 16B fat | capture drop via `closure_owned_drop_`; 🐞 env block not freed | move captures; fat value | 🐞 capture-mode not enforced | ⚠️ env leak + capture-mode gap |
| **dyn trait** | 🐞 fat+vtable heap, leak unless Box (A5/A6); vtable not interned | via Box only | Copy (`&dyn`) | ok | 🐞 leaks + vtable dup |
| **class `new`/`delete`** | heap (A8) | ✅ via explicit `delete`; 🐞 leak if not deleted | — | — | manual (intentional?) |
| **assignment `x=y`** | — | 🐞 **old LHS value NOT dropped before overwrite → leak** | source moved (suppress double-free) | assign-while-borrowed ✅ | 🐞 **drop-before-replace missing** |
| **match** | binds payload (may move scrutinee) | scrutinee-move avoids double-free | `mark_match_scrutinee_moved` | per-arm move union | ✅ |
| **generic fn** | per-mono | drop via mono re-mangle | move deferred to mono | 🐞 **body not borrow-checked** | ⚠️ |
| **Zone/Hermes** | arena bump (MemHolder) | region freed at rc0 via destroyer; no per-object drop | RelPtr/AnyVal offsets; manual retain/release | n/a (offsets) | ✅ blessed divergence |

---

## 6. Defect & debt inventory (prioritized)

**P0 — pervasive correctness/leaks (drive the initiative):**
1. 🐞 **Enum heap-representation** (A1): every enum value heaps a `{tag,payload}`
   block that is never freed. Root cause of A1/A2 leaks + the heap-promotion hack.
   **Fix = value-representation (inline, like Rust)** → kills the leak and the hack
   together. `feedback_enum_value_repr_debt`. (Large mono+mlir-gen change.)
2. 🐞 **Assignment doesn't drop the old value** (`x = y` for a live move-type `x`
   leaks `x`'s prior contents — `mlir_gen_stmt.cpp:1032` / `lower_assign`). Rust
   drops-before-replace. Not in DIVERGENCES.md.
3. 🐞 **Box<T> has no auto-Drop** and no `T`-drop / `?Sized` — stale. Modernize to
   Rust ownership (`impl Drop` freeing + dropping `T`; deref accessors; fat-ptr).
4. ✅ **Rc/Arc run `T`'s destructor** at refcount 0 (`93dd38cf`, test `rc_drop_inner`) — DONE.

**P1 — leaks in compiler-emitted heap (need an owner/free path):**
5. dyn fat+vtable (A5/A6) leak for raw `&dyn`/`*mut dyn`; vtable not interned.
6. escaping closure env (A7) never freed.
7. ✅ FIXED (0e34fd63) returned slice / fat-ptr promotions (A3/A4): slices/str now return BY VALUE (16-byte {ptr,len}, like the &dyn fat-pair) instead of a malloc(16) heap-promote in gen_return — `llvm_fn_ret_type`/`fn_call_ret_llvm_type`/`make_fn_type`/`gen_return` all treat Slice like TraitObject; `spill_slice_call_result` re-spills call results to a stack slot for by-pointer consumers. p_slice 16→0; 5235/5235.
8. class `new` without `delete` leaks (decide: auto-Drop classes or keep manual).
9. HashMap/Deque: no `impl Drop`.

**P2 — soundness/diagnostic gaps:**
10. Generic fn bodies not borrow-checked.
11. No index/slice element-level borrow/exclusivity; `&mut [T]` mutability not
    type-tracked (B6).
12. borrow-check `is_move_type` Struct-only → missing use-after-move diagnostics
    for moved tuples/enums.
13. closure-capture-mode (Fn/FnMut/FnOnce) exclusivity not enforced.
14. array element move-out unsupported (clean reject).

**Cleanups:** two parallel `is_move_type`/`needs_drop` impls (sema vs
borrow_check); enum-promotion band-aid (remove after #1); dual manual-free + Drop
in Vec.

---

## 7. Initiative roadmap (this work)

Order chosen so each step is independently green-gateable and de-risks the next:

1. **enum value-representation** (#1) — biggest win; removes A1/A2 leaks + promotion
   hack. Touches enum layout, lower_enum_lit, match/bind, by-value pass/return,
   `&Enum` two-level convention (`ref_enum_two_level_convention`). Do as a focused
   sprint with disasm-verification, full-suite gating each step.
2. **assignment drop-before-replace** (#2) — bounded; re-land the reverted
   SAssign-drop, narrowly (drop old LHS iff live move-type & not moved).
3. **Box<T> modernization** (#3) — `impl Drop` (free + drop `T`), deref accessors;
   `?Sized`/dyn later.
4. **Rc/Arc drop `T`** (#4).
5. **P1 compiler-emitted leaks** — give vtables interning + an owner; closure-env
   and returned-fat-ptr ownership; class auto-Drop decision.
6. **P2 borrow/diagnostic** gaps as a later pass.

Every step gates on the FULL suite (`bash ../tests/logos/ctest-summary.sh`); a
"green" gate on a stale `.o` once hid a regression — rebuild clean.

---

## 8. Progress log + execution plans

### LANDED (2026-05-26)
- **#3 Box<T> `impl Drop`** (`d76ce7a7`) — RAII: move inner out (drop T) + dealloc,
  null-guarded vs `box_free`; Box now move-only (Rust parity). Test `box_drop_raii`.
  *Follow-up:* accessor modernization (Deref/DerefMut instead of raw-ptr `self`;
  `?Sized`/`Box<dyn>` at the type level) — not done.
- **#4 Rc/Arc drop T** (`93dd38cf`) — `drop_rc`/`drop_arc` move `inner.val` out
  (fire T's Drop) before dealloc at refcount 0. Test `rc_drop_inner`. *Follow-up:*
  Weak refs / cycle handling absent.

### ⛔ WALL — #2 assignment drop-before-replace (deferred)
Rust spec `expr.assign.drop-target`: `x = y` drops the old value at the place
**unless it is uninitialized**. Logos has **NO definite-assignment analysis**
(confirmed `sema_stmt.cpp:1670` — "Full definite-assignment analysis is a separate
pass"), so it cannot tell at the assignment point whether `x` is initialized. Doing
drop-before-replace without that is **UB on conditional-init paths** (`let x:T; if
c {x=a;} x=b;`) — which is exactly why an earlier attempt was reverted as "too
aggressive." **Blocked on a prerequisite feature: definite-assignment analysis (or
dynamic drop-flags).** That feature (flow-merged init state, dual of the existing
`moved_vars_` snapshot/union machinery, or per-local drop flags in codegen) is the
real next dependency. Until then, reassigning a live owner leaks the old value
(real Rust divergence; add to DIVERGENCES as a known catch-up).

### #1 enum value-representation — ✅ LANDED 2026-05-26 (commit 51d2e29e)
Done: tagged enums flipped to value-repr (ptr-to-inline-stack-storage, like Struct);
`alloca` not `malloc`; inline-embedded in struct fields / tuple / array / nested enum
payloads / Vec buffers (sizeof+memcpy); one-level `&Enum`; returned-by-value-via-load.
Killed A1/A2 leak + heap-promotion hack. The decisive fix vs the reverted attempt 1 was
**move/drop suppression for inline enums** — `mark_match_scrutinee_moved` widened from
VarRef-only to PLACE scrutinees (`match s.o` / `match a.1`) so a payload moved out of an
inline parent marks the place moved and the parent's scope-exit Drop skips it (the
issue-19367 double-free). Load-bearing non-obvious fixes: `logos_abi_byte_size(Enum)`
resolves the CONCRETE instantiation; a payload-size FIXPOINT in mlir_gen.cpp then sets each
identified `enum.NAME` body ONCE (set-once LLVM structs, order-independent); aligned
variant-payload layout (naive field-sum undersized multi-field variants → overlapping
allocas). 5235/5235 green; valgrind-clean on json_parse. Residual (separate P1, NOT
enum-specific): `Vec` doesn't drop its elements (~22-byte leak on `Vec<Enum-with-String>`).
The original plan below is kept for historical reference.

### #1 enum value-representation — EXECUTION PLAN (original, now COMPLETE)
**Goal:** enum value = pointer-to-**inline stack storage** (`enum.NAME =
{i32 disc,[N x i8]}`), exactly like a Struct — NOT a heap-malloc'd block. Kills the
A1/A2 leak and the heap-promotion hack. **Sema + Mono need NO changes**
(representation-agnostic; recursive-enum indirection already enforced by
`check_recursive_value_types`, `sema_impl.hpp:1261` — no new infinite-size hazard).
All work is in **mlir-gen**, and is mostly **removing enum special-cases**:

Touch-points (file:line, from scoping audit):
1. **Construction** `mlir_gen_expr.cpp:402-423` (EEnumLit) / `:425-501` (EEnumLitData):
   `call_malloc`→stack `alloca enum.NAME` (or in-place into destination); same
   disc/payload store GEPs.
2. **&Enum two-level → one-level**: `EAddrOf` `mlir_gen_expr.cpp:1262-1272`,
   `EAddrOfTemp` `:1510-1531`; delete `var_tagged_enum_` vs `var_tagged_enum_ptr_`
   distinction (`mlir_gen_impl.hpp:238,241`) — treat like Struct.
3. **Match** `mlir_gen_stmt.cpp:2694-2765` (gen_match via_ref), `:2380-2422`
   (pat_test), `:2533-2573` (pat_bind), `:13-130` (bind_enum_payload), match-expr
   `mlir_gen_expr.cpp:3015+`: drop the extra `via_ref` load (one-level deref).
4. **Field/deref write heap-promotion — DELETE**: `gen_field_write`
   `mlir_gen_stmt.cpp:1792-1821`, `SDerefWrite` `:572-592` → memcpy inline.
5. **gen_assign** `mlir_gen_stmt.cpp:1032-1048`: memcpy `4+payload` bytes (like the
   struct/array rebind paths just below) instead of storing a new ptr.
6. **Drop** `mlir_gen_stmt.cpp:353-401` + `child_value_ptr` `:300-301` + SDrop
   `:473-503`: GEP inline (drop the heap-ptr loads).
7. **Type/layout**: `logos_to_mlir(Enum)` `mlir_gen_types.cpp:60-64` stays `ptr`
   (enum = ptr-to-storage); enum struct FIELDS embed `4+payload` inline
   `:159-188`; `logos_abi_byte_size`/`sizeof::<Enum>()` already inline-correct.
8. **Pass/return**: params already pass enums as ptr (`mlir_gen_fn.cpp:85-97`) — align
   to the Struct path (pass-by-ptr, return-by-value-via-load).

**THE ATOMICITY CRUX (confirmed by code study 2026-05-26):** the heap allocation is
not incidental — it exists *specifically so the enum pointer can escape*. Two escapes
the heap leak silently masks:
- **Nested enums** (`Some(Some(x))`): the inner enum is stored into the outer's payload
  blob **as a pointer** (`mlir_gen_expr.cpp:402-501` comment: "stored into another
  enum's payload slot as a pointer"). Return-by-value copies the outer `{tag,payload}`
  struct *including that inner ptr* → with stack storage the inner would dangle.
- Therefore **construction→alloca ALONE is unsafe** (no safe incremental slice). The
  load-bearing change is making the **payload blob embed nested enums INLINE** (the
  union size must recursively account for nested-enum full `{tag,payload}` footprint,
  `register_tagged_enum` payload_bytes at `mlir_gen_types.cpp:388-422`), and
  construction/match/drop must memcpy/GEP the inner enum inline (not store/load a ptr).
  Recursion terminates because by-value self-reference is already rejected
  (`check_recursive_value_types`). This recursive inline-payload layout is the hard
  core; everything else (alloca, one-level `&`, delete heap-promotion) follows from it.
**Hard / risky:** the ~15 value/ptr reconciliation sites (`val.getType()!=ptr_type()`
spill checks at gen_let 740, gen_match 2744, EAddrOfTemp 1520, EIf 2985/2999,
gen_assign 1046, call results 1769/1976/2217, …) must ALL agree on the new
invariant ("enum = ptr-to-stack-storage, like Struct") simultaneously — a missed
site silently reads the disc at the wrong indirection level (the recurring
G152-9/G160-8 bug class). Nested enum-in-payload inline offsets are the most
error-prone. **This is an all-at-once flip, full-suite (L4) gated; do as a focused
session, disasm-verify a minimal `Option<i64>` round-trip first.** Recommended
framing: make enum a variant of the Struct repr and DELETE the special-casing.

**ATTEMPT 1 (2026-05-26) — step A (inline nested payload) tried in isolation, REVERTED.**
Implemented the recursive inline-payload layout (the "hard core" above) + adapted the
by-ref nested bindings (two-level `&Enum` ref made correct: alloca-holds-`fp`, bound
bare so `gen_expr` returns the slot, matching both intra-fn match AND cross-fn `&Enum`
params) + the `?` Ok-payload inline extraction. Got 4 of 5 regressions green
(nested-by-ref, match-mut-ref-writeback, question-nested-result-opt, stdlib_option_result_eq).
The 5th (`json_parse`) exposed the **deeper, decisive wall**:

- **Step A is NOT a safe incremental slice — it breaks MOVE/DROP soundness.** Once a
  nested enum is inline, "moving" it out of a scrutinee (match-extract or `?`) yields an
  **alias into the parent's storage** (`fp`), not a pointer transfer. The parent (e.g. a
  `match parse()` temporary `Result<Json,E>`) still owns that storage, so BOTH the
  extracted binding and the parent's drop-glue free the same heap contents → double-free
  (`Vec<JsonField>__drop` → `String__drop` → double `free`). The pre-inline heap model is
  internally CONSISTENT (enum = heap ptr everywhere, moved by ptr-transfer, move-tracking
  suppresses the source); inlining SOME enums while the rest of the model stores enums
  **by-pointer** breaks that consistency.
- **CORRECTED scope of the blocker (verified by disasm + repros, 2026-05-26):** my first
  read ("Vec uses a pointer-element convention → a separate large prerequisite") was
  WRONG. `Vec` stores aggregates **inline by `sizeof::<T>()` + memcpy** already — disasm of
  `Vec<P>::push` (P={i64,i64}) shows a ×16 element stride + memcpy `call`; `Vec<Struct>` is
  a true inline value container. Enums land in a Vec **by-pointer today ONLY because
  `sizeof::<Enum>()`=8 (ptr) and construction heap-allocates** — once the repr flips
  (`sizeof` returns the inline `4+payload`, construction→alloca, value=inline bytes), the
  generic Vec/HashMap machinery stores enums inline **automatically** (it already memcpy's
  `sizeof` bytes). So the container model is NOT a separate wall. The line-2426
  "pointer element convention" comment is narrower than it reads (user-level `v[i]`
  IndexRead stride, not the buffer storage).
- **The actual hard part is MOVE/DROP suppression for inline enums.** Repro: a
  struct-with-`String` moved into a `Vec` via `?` TWICE does NOT double-free — inline
  STRUCT moves already suppress the source's drop correctly. Step A's json_parse
  double-free is the SAME mechanism MISSING for inline enums: extracting/`?`-moving an
  inline enum must mark the source slot moved so the parent's drop-glue (and the `?`-source
  temporary) skip it. This is a fixable gap (wire enum move-tracking like struct
  move-tracking), not an architectural wall.

**Revised conclusion:** the enum value-repr flip is still genuinely all-or-nothing (every
indirection-level site must agree at once), but it is MORE tractable than the "container
wall" framing — no separate container-model rewrite is needed. The coordinated change for
the next focused session:
  1. `sizeof::<Enum>()` returns inline `4+payload` (verify the `sizeof` intrinsic uses
     `logos_abi_byte_size`, which is already inline-correct).
  2. Construction → alloca; one-level `&Enum` (delete `var_tagged_enum_ptr_`); inline
     payload; memcpy stores; delete heap-promotion (the §8 #1 touch-point list).
  3. Inline-embed enums in struct fields + tuple elements (Category C — mirror the nested-
     struct inline-embed in `register_struct` / `tuple_llvm_type`).
  4. **Wire enum move/drop suppression to match inline structs** (the json_parse lesson):
     match-extract / `?` / let-destructure of an inline enum marks the source moved so the
     parent drop-glue skips it. Gate on a `Vec<Json>`-shaped recursive-droppable round-trip.
The attempt-1 mlir-gen diffs were reverted to the green baseline (commit 2ffa009a); the
4-of-5 fixes (two-level `&Enum` ref binding, `?` inline Ok-payload) are reconstructable
from commit 51133352's parent reflog if useful as a starting reference.
