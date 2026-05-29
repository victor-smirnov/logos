# Logos Memory Management — Full Analysis

*Originally synthesized 2026-05-26 from a 5-track code audit (allocation, drop,
move/Copy, borrow, stdlib). **Re-verified against the tree 2026-05-28** — most of
the original P0/P1 leak inventory has since been fixed; this revision reflects the
current state.*

Canonical map of how `logosc` manages memory and how every language feature
interacts with it. Read this before touching allocation / drop / move / borrow /
Box / Vec / enum-repr. Target model is **Rust ownership + RAII** (see
`feedback_think_in_rust`); divergences are either the blessed **Zone/Hermes
region** set or **bugs/debt** (flagged ⚠️/🐞 below).

---

## 0. Mental model — two memory worlds

Logos has **two coexisting memory disciplines**:

1. **Normal program memory** — follows Rust: stack values + heap behind owning
   handles (Box/Vec/Rc/Arc/String/HashMap/…), reclaimed by `Drop`/RAII or move.
   As of 2026-05-28 this world is **substantially built** — the pervasive
   compiler-emitted leaks (enum heap block, fat-ptr promotions, vtable heap,
   escaping closure env) are fixed, and assignment now drops-before-replace with
   Rust-faithful drop elaboration.
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
(`mlir_gen_fn.cpp:31/40`). The C++-style `delete` statement (and `gen_delete`) was
**removed** (929daf5e); `call_free` is now emitted only from RAII drop paths:
**Box<dyn>/Box<T> drop**, **Rc/Arc drop at refcount 0**, and the **escaping-closure
drop glue**. Stdlib containers free their own buffers in their `impl Drop`.

The compiler-emitted heap allocations that the original audit flagged as leaks are
now reclaimed. Remaining `call_malloc` sites:

| # | Allocation | Site | Freed? |
|---|---|---|---|
| A1 | Enum `{tag,payload}` | — | ✅ **VALUE-REPR** (`51d2e29e`): enum is a stack `alloca` inline `{i32 disc,[N x i8]}` like a Struct — no heap, no leak. Nested enums inline-embedded. Killed the old leak + heap-promotion hack. |
| A2 | Enum-slot heap-promotion | — | ✅ removed with A1 (inline memcpy, no promote). |
| A3 | Returned slice fat `{ptr,len}` | — | ✅ **return-by-value** (`0e34fd63`): 16-B `{ptr,len}` returned in registers/spill like `&dyn`, no `malloc(16)`. |
| A4 | Returned `&dyn`/Box<dyn> fat | — | ✅ return-by-value (dyn sprint); owning `Box<dyn>` is drop-freed. |
| A5 | dyn vtable array | `build_inline_vtable` (`mlir_gen_dyn.cpp:909`) | ✅ **TRUE STATIC** (`ff7b5842`): one `constant [N x ptr]` global per (trait,type) in `.data.rel.ro` — interned, zero heap, zero runtime init, slot 0 = `drop_in_place`. |
| A6 | dyn fat storage `{data,vtable}` | `coerce_to_dyn` (`mlir_gen_dyn.cpp:1019`) | ✅ inline 16-B value fat-pair (stack `alloca`, `heap=false`) for `&dyn`/`&mut dyn`/`Box<dyn>`/`*dyn` (dyn-unification `77d85c8d`). Owning `Box<dyn>` data is drop-freed (vtable[0]). |
| A7 | Escaping closure environment | `mlir_gen_dyn.cpp:1831` | ✅ **freed** (`eb4e80c3`): heap env layout reserves field 0 = drop glue; `__closure_drop__<id>` drops owned captures + `call_free(env)`, driven via `Box<Closure>::drop`. |
| — | dyn fat pair (`&dyn`/`*dyn`/`Box<dyn>`) | `coerce_to_dyn` | ✅ STACK 16-B fat, no heap. The former "thin `Ptr<TraitObject>` handle to heap-copied fat" path was non-Rust + provably unreachable (5433-file sweep) and was removed (`282c5af3`). |
| — | Structs / tuples / arrays / slices / enums / closures / match spills | `create_entry_alloca` | ✅ STACK — reclaimed on frame exit; droppable *contents* handled by SDrop. |
| — | Container buffers (Vec/HashMap/Deque grow) | `call_malloc` in stdlib via `alloc` | ✅ freed by the container's `impl Drop`. |

**stdlib containers** (alloc via `logos.lang.mem` `alloc`/`dealloc`):

| Type | Auto-Drop frees buffer? | Drops elements/`T`? | Notes |
|---|---|---|---|
| String | ✅ | n/a | OOM/overflow-hardened (`string.logos:271`) |
| Vec<T> | ✅ (`vec.logos:393`) | ✅ per-element move-and-drop | manual `vec_free` REMOVED; `into_iter` transfers via ptr-zero; eager cap-8 |
| Rc<T> / Arc<T> | ✅ block at rc0 | ✅ **T's destructor run** (`93dd38cf`; `drop_rc`/`drop_arc` move `inner.val` out → fires Drop before dealloc) | no Weak |
| Box<T> | ✅ **`impl Drop`** (`d76ce7a7`): drop T + dealloc, move-only | ✅ | accessors still raw-ptr-self; no `?Sized` at type level |
| HashMap / VecDeque / HashSet | ✅ **`impl Drop`** (`b98f8e72`): drops each element (move-out + T's Drop) + frees buffers; manual `*_free` removed; `clear()` drops live entries | ✅ | Rust parity (HashSet via inner HashMap field) |

---

## 2. Drop / RAII pipeline

**Sema decides, mlir-gen executes.** Every drop is an explicit `SDrop` LIR node
inserted by sema; mlir-gen never spontaneously drops.

- **Type classification** (`sema.cpp`): `is_move_type`, `drop_fn_for`
  (package-guarded), `has_droppable_fields`, `needs_drop`, `compute_auto_copy_types`
  (auto-Copy: plain all-Copy-field non-Drop structs join `copy_types_`). Owning
  `Box<dyn>` / `TraitObject` carry an owning bit → drop = vtable[0] + free.
- **Insertion** (`sema_stmt.cpp` `lower_block`): scope-exit `collect_drops`
  (reverse decl order), `return` → hoist value to `__ret_tmp_` then
  `collect_all_drops` (stops at closure boundary), `break/continue` →
  `collect_drops_to_loop`. All skip `moved_vars_` unless in `closure_owned_drop_`.
  `make_drop_stmt` (`sema.cpp:2611`) self-recursion-guards a Drop body and gathers
  `moved_fields`; it emits the `__box_dyn__drop` sentinel for owning trait objects.
- **Mono** (`mono_clone.cpp`) rewrites `__typevar_pending__drop` and re-mangles
  generic drop symbols to concrete; preserves the `DROP_OLD` assign flag.
- **Codegen** (`mlir_gen_stmt.cpp`): `gen_stmt_kind(SDrop)` (654) calls user drop_fn
  + recurses fields/elems/variant-payload (skipping `moved_fields`, refs/ptrs);
  `gen_drop_value` (480) is the recursive workhorse. `call_free` lives in user
  `impl Drop` bodies (`String::drop`→`dealloc`), in the Box<dyn>/Rc/Arc handle drop
  (`gen_drop_owning_dyn_handle`), and the closure drop glue.
- **Assignment drop-before-replace + drop elaboration** (B8): `x = v` for a live
  droppable `x` drops the old value first (Rust order: RHS evaluated, then drop,
  then store). For a `let mut x: T;` declared WITHOUT an initializer, mlir-gen does
  **Rust-style drop elaboration** — a pre-scan (`prescan_uninit_flags`) marks the
  var flag-needed only if it has a conditional/loop assignment; otherwise drops are
  placed statically (the `uninit_static_`/`uninit_assigned_` codegen tracking).
  Flag-needed vars get a runtime i8 drop flag (set on assign, gated drop). Result:
  exact drop for conditional init (`if c {x=a;} x=b;` drops `a` iff c), no garbage
  drop on early-return-before-init, and **no flag** emitted for straight-line vars.
- **Skipped from drop**: Copy-payload enums (non-move), Copy structs, refs/ptrs,
  `#[no_auto_drop]`/`ManuallyDrop`, moved values, still-uninit vars (flag/static).

---

## 3. Move semantics & Copy

Two **independent** trackers:
- **sema** `moved_vars_` (+ `mark_moved*`, `sema_impl.hpp`) — *drop suppression*
  (so a moved source isn't double-freed). Drives codegen.
- **borrow_check** `VarState{moved,…}` — *diagnostics* (use-after-move). Soundness
  gate. Its `is_move_type` (`borrow_check.cpp:101`) is **Struct-only/narrower** than
  sema's (tuples/enums/arrays not use-after-move-checked there).

Move points (sema): let/assign RHS, all call paths' by-value move args, by-value
`self` (`track_recv_moved`), struct/tuple/StructLit construction, match scrutinee
(`mark_match_scrutinee_moved`, widened to PLACE scrutinees for inline enums),
return value, closure capture (`closure_owned_drop_` keeps the capture's drop
alive; an escaping `move` closure capturing a droppable owns it inline). Per-branch
`moved_vars_` snapshot+union merge for if/match.

Copy: primitives, `&T`/`&dyn`, `*const/*mut`, `&[T]`, fn-ptr, trait-object — all
Copy; **`&mut T` NOT Copy** (Rust parity). Auto-Copy for all-Copy plain structs.
By-value params/`self` **do** auto-drop at callee scope exit.

---

## 4. Borrow checker

Runs on LIR, **concrete fns only** (`borrow_check.cpp:2063` scans
`prog.specializations` — **generic fn bodies are NOT borrow-checked**; only their
monomorphizations). Two analyses: structural `BorrowChecker` + `RegionInferer`
(`region_infer.cpp`).

**Enforced**: use-after-move / partial-move; `&mut` exclusivity vs `&`;
assign-while-borrowed; mut-binding requirement for `&mut` (params whitelisted);
**field-path disjoint borrows** (B83); **two-phase borrows** (B82); **NLL** (last-use
release + region inference); **dropck** (B87); **dangling-ref** (return ref to
local/temp); **named lifetimes + elision + outlives** on returns. Raw-ptr roots
bypass exclusivity (B93.2); `&mut`-roots also skip the binding-mut requirement.

**NOT checked (gaps vs Rust)**: generic fn bodies; index/slice **element** borrows
(`arr[i]` aliasing/exclusivity not modeled); reborrows (not first-class);
closure-capture-mode (Fn/FnMut/FnOnce) exclusivity; cross-function lifetime
provenance (intra-procedural only); self-referential structs; move-out-of-deref.

---

## 5. Feature × memory-management interaction matrix

| Feature | Allocation | Drop/RAII | Move/Copy | Borrow | Status |
|---|---|---|---|---|---|
| **enum** | ✅ stack `alloca` inline `{disc,payload}` (value-repr) | payload-recursive drop, no block to free | Copy-payload non-move; droppable-payload move (inline move-tracking) | classified Copy when payload Copy | ✅ Rust-like value-repr |
| **struct** | stack alloca | field-recursive drop; auto-Copy | move unless Copy; partial field moves | field-path borrows ✅ | ✅ |
| **tuple** | stack; **inline-by-value** in fields/arrays/nesting (G1 `81b28479`) | element-recursive drop | move iff any elem move | ok | ✅ |
| **array `[T;N]`** | stack | per-elem drop | move iff elem move; ⚠️ move-out by index = clean reject | 🐞 no element-level borrow | ⚠️ element ops limited |
| **slice `&[T]`** | fat {ptr,len}; ✅ returned by value | Copy (no drop) | Copy | 🐞 no element borrow; ⚠️ `&[T]`/`&mut [T]` both `Kind::Slice` (mut not tracked, B6) | ⚠️ |
| **Box<T>** | heap (`box_new`) | ✅ `impl Drop` (drop T + dealloc); move-only | move | ok | ✅ (accessors raw-ptr-self; `?Sized` later) |
| **Vec<T>** | heap, grow | ✅ Drop frees + drops elems | move; `into_iter` ptr-zero | IndexMut place-write ✅ | ✅ |
| **Rc/Arc** | heap inner | ✅ block at rc0; ✅ T dropped | clone = refcount; move | ok | ⚠️ no Weak |
| **String** | heap, grow | ✅ Drop | move | ok | ✅ |
| **closure** | env: stack (non-escaping) / heap (escaping); value `{fn,env}` 16-B fat | ✅ escaping env freed via `__closure_drop__` glue; captures dropped | move captures (escaping `move` owns inline) | 🐞 capture-mode not enforced | ✅ (capture-mode gap only) |
| **dyn trait** | inline 16-B fat (stack), static `.rodata` vtable | ✅ owning `Box<dyn>` drop = vtable[0]+free; `&dyn` Copy | Copy (`&dyn`); `Box<dyn>` move | 🐞 no object-safety check (P2-15) | ✅ uniform fat repr (`&dyn`/`*dyn`/`Box<dyn>` all 16-B, no heap handle); ⚠️ no upcasting / `+Send` / object-safety gate |
| **assignment `x=y`** | — | ✅ drop-before-replace + Rust **drop elaboration** (static placement, flags only for maybe-init) | source moved (suppress double-free) | assign-while-borrowed ✅ | ✅ full Rust drop semantics |
| **match** | binds payload (may move scrutinee) | scrutinee-move avoids double-free; match-temp dropped | `mark_match_scrutinee_moved` (incl PLACE) | per-arm move union | ✅ |
| **generic fn** | per-mono | drop via mono re-mangle | move deferred to mono | 🐞 **body not borrow-checked** | ⚠️ |
| **Zone/Hermes** | arena bump (MemHolder) | region freed at rc0; no per-object drop | RelPtr/AnyVal offsets; manual retain/release | n/a (offsets) | ✅ blessed divergence |

---

## 6. Defect & debt inventory (current)

**P0 — pervasive correctness/leaks: ✅ ALL CLOSED.**
1. ✅ Enum heap-representation → **value-repr** (`51d2e29e`). Leak + promotion hack gone.
2. ✅ Assignment drop-before-replace (`b9cf9d81`) + **drop elaboration** (`3e7bb5df`).
3. ✅ Box<T> `impl Drop` (`d76ce7a7`).
4. ✅ Rc/Arc run T's destructor at rc0 (`93dd38cf`).

**P1 — compiler-emitted heap leaks: ✅ ALL CLOSED.**
5. ✅ dyn fat + vtable: inline 16-B fat (`77d85c8d`) + true static `.rodata` vtable,
   interned, slot-0 drop_in_place (`ff7b5842`, `9678b210`).
6. ✅ escaping closure env freed via drop glue (`eb4e80c3`); escaping move-capture
   owns inline (`b84989bb`).
7. ✅ returned slice / fat-ptr by value (`0e34fd63`).
8. ✅ Box<dyn> by-value param leak — threaded concrete type to drop glue (`62a64943`).
9. ✅ HashMap/VecDeque/HashSet element drop (`b98f8e72`); Vec element drop (`52c24b28`).

**P2 — soundness/diagnostic gaps: ✅ ALL CLOSED (residual precision refinements noted inline).**
10. ✅ Generic fn bodies borrow-checked (`42998241`). A PRE-mono pass checks
    generic templates directly (so a never-instantiated generic is still
    checked — mono drops it otherwise), in exclusivity-only mode (move tracking
    is imprecise on TypeVars → false-positived on stdlib generics; concrete moves
    are checked on the specializations post-mono). Duplicate template/spec diags
    de-duplicated. Remaining: full generic-aware move analysis (so generic move
    errors are caught pre-mono too, not only via specializations).
11. ✅ `&mut [T]` vs `&[T]` mutability type-tracked (B6) — DONE (`c971c97f`).
    Slice carries the `mut_ptr` bit; writing through a shared `&[T]` and passing
    `&[T]` where `&mut [T]` is expected are rejected; `&mut [T]`→`&[T]` downgrade
    allowed. ROOT CAUSE of the earlier failed attempt: the type-arena serializer
    persisted `mut_ptr` only for Ptr/DstRef (not Slice), so a fresh `&mut [T]`
    read back as shared — NOT a parser issue (the earlier note was a misdiagnosis;
    the fix threads mut through serialize + TypeUID + type-equality + subst +
    coercion + place-writability). Still open: index/slice ELEMENT-level borrow/
    exclusivity (`arr[i]` aliasing); `let m: &mut [T] = &mut arr;` array-ref→
    mut-slice coercion at a let binding (works as a call arg).
12. ✅ borrow-check `is_move_type` widened to tuple/enum/array (`4d909afe`):
    move-while-borrowed of a `(String,i64)` / `[String;N]` / move-payload enum is
    now rejected (was a dangling-ref gap; whole-value use-after-move was already
    caught by sema). Recurses structurally, gated on not-Copy.
13. ✅ closure mut-capture exclusivity (`4f36fd4d`). A `let`-bound closure that
    MUTATES a capture registers a `&mut` borrow held by the closure var (NLL
    release at its last use), so using/`&mut`-ing the variable while the closure
    is live is rejected. Shared (read) captures stay liveness-only — Logos
    captures a whole variable, so a whole-var shared borrow would wrongly block
    RFC-2229 disjoint sibling mutation. **Remaining = RFC-2229 (own session,
    2026-05-29 plan):** Logos closures capture WHOLE variables (sema_expr capture
    scanner walks `p.x`→root `p`; env layout, codegen unpack, borrow-check all
    whole-var). True disjoint capture is a 4-layer rewrite: (1) capture analysis
    records field PATHS `p.x` + per-path mode; (2) env layout stores the field
    not the whole struct (needed so `move ||p.x` leaves `p.y` usable — the
    move-precision part); (3) mlir-gen env-field access by path; (4) borrow-check
    field-path capture-borrow (reuse B83). For NON-`move` closures layers 2/3
    aren't needed for correctness (whole-`p`-by-ref + read `p.x` is runtime-
    equivalent) → a phase-1 "borrow-check exclusivity only" (layers 1+4) gives
    `||p.x` + `&mut p.x` rejection + disjoint `&mut p.y` soundly; risky
    move-disjointness is layers 2+3. b156 currently passes via the
    shared-capture-liveness-only choice, NOT field-path capture. Also: exclusivity
    for inline (non-`let`-bound) closures.
14. ✅ array element move-out of a **droppable** elem cleanly rejected (anti
    double-free guard); a non-droppable elem is accepted (sound — effectively a
    copy, no Drop). Minor: non-droppable move-out diagnostic differs from Rust.
15. ✅ **object-safety (E0038)** — DONE (`c4ba01aa` generic-method + `f3f163f6`
    rest). `check_trait_object_safe` (run when a `dyn Trait` fat type is resolved)
    rejects a trait coerced to a trait object when a method is generic / has no
    `self` receiver / returns `Self` / takes `Self` by value — skipping methods
    with `where Self: Sized` (excluded from the vtable). Prerequisite fixed: the
    trait-method `where_clause?` was parsed-then-dropped by the grammar (latent
    "`where Self: Sized` ignored" bug) — now mapped to `WHERE` on the body-less FN
    productions. Tests object_safety_{generic_method,returns_self,no_receiver}.

**Missing features (not bugs):** `Rc`/`Arc` `Weak` references + cycle handling;
Box `?Sized` / `Box<?Sized>` at the type level (`Box<dyn>` works via
owning-TraitObject collapse, not a generic `Box<?Sized>`); modern Deref/DerefMut
Box/Rc accessors (still raw-ptr-self in places); **supertrait upcasting** (`&dyn
Sub` → `&dyn Super` — verified rejected with a type mismatch; Rust stabilized
1.86, needs vtable upcast slots/prefix); **`dyn Trait + Send`/`+ Sync`**
auto-trait composition on trait objects (untested/unsupported); **`Box::into_raw`
/ `from_raw` / `leak`** raw-ownership API (don't exist).

**Raw `*dyn` escape:** `*const dyn`/`*mut dyn` is a 16-B fat pair like `&dyn`
(sema folds literal `*dyn`→bare TraitObject). There is no thin-handle heap
promotion (removed `282c5af3`). A raw dyn pointer that genuinely needs to outlive
its frame is the user's `unsafe` responsibility; if a bare escape handle is ever
wanted it would be a `*u8`/system-type widened via an intrinsic (no `Box::into_raw`
/`from_raw` exist yet — a future Box raw-ownership API, orthogonal to this repr).

**Cleanups:** two parallel `is_move_type`/`needs_drop` impls (sema vs borrow_check)
remain; worth unifying.

---

## 7. History (execution notes, condensed)

The memory-management initiative ran 2026-05-26 → 05-28. Landmark work, in order:

- **enum value-representation** (`51d2e29e`) — the biggest change: flipped tagged
  enums from a heap `{tag,payload}` block to a stack `alloca` inline value, like a
  Struct (one-level `&Enum`, inline-embedded in fields/tuple/array/nested-payload/
  Vec via sizeof+memcpy, returned by value). All-at-once flip across ~15 value/ptr
  reconciliation sites. The decisive enabler vs an earlier reverted attempt was
  **move/drop suppression for inline enums** (`mark_match_scrutinee_moved` widened
  to PLACE scrutinees), so a payload moved out of an inline parent marks the place
  moved and the parent's scope-exit drop skips it (the json_parse double-free).
- **inline-fields refactor** — Slice/Closure/`&dyn`/Tuple stored inline-by-value in
  struct fields, array/tuple elements (the same "value IS a pointer to its inline
  storage" convention as Struct). Tuple was the last holdout (G1, `81b28479`).
- **dyn unification** (`77d85c8d` + `6918718b`) — `&dyn`/`&mut dyn`/`*dyn`/`Box<dyn>`
  all one uniform 16-B `{data,vtable}` fat pointer; persistent B-tree rc rewritten
  to clean `clone_arc`/`Drop`. Box differs only by ownership (drop frees data).
- **dyn vtable → true `.rodata` static** (`ff7b5842`) + **drop_in_place vtable slot
  0** (`9678b210`) so `Box<dyn>` drops its concrete.
- **Box / Rc / Arc / container Drop** — `impl Drop` for Box (`d76ce7a7`), Rc/Arc
  T-drop (`93dd38cf`), Vec element drop (`52c24b28`), HashMap/VecDeque/HashSet
  element drop (`b98f8e72`).
- **Box<dyn> by-value param leak** (`62a64943`) — the implicit call-arg coercion
  passed an empty concrete type → empty drop_in_place glue → leaked the concrete's
  droppable fields. Fixed by threading the concrete type.
- **assignment B8** — drop-before-replace (`b9cf9d81`) → dynamic drop flags for
  conditional init (`33d12014`) → full **drop elaboration** (`3e7bb5df`): static
  drop placement primary, runtime flags only for the maybe-init residual, matching
  Rust's MIR drop elaboration. A systematic exact-drop-count test matrix
  (`b8_uninit_drop_matrix`) drove out an early-return-before-init UB along the way.

**Gating discipline (still in force):** every step gates on the FULL suite
(`bash ../tests/logos/ctest-summary.sh`, currently 5255/5255) and valgrind on a
representative droppable round-trip; rebuild clean (a stale `.o` once hid a
regression). Memory work is verified by *exact drop counts*, not just pass/fail.
