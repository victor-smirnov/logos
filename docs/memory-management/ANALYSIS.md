# Logos Memory Management — Full Analysis

*Originally synthesized 2026-05-26 from a 5-track code audit (allocation, drop,
move/Copy, borrow, stdlib). **Re-verified against the tree 2026-05-29** — the
P0/P1 leak inventory, all P2 soundness gaps, and the entire "missing features"
shelf (Rc/Arc Weak, supertrait upcasting, Box::into_raw, Box<[T]>, custom-DST,
RFC-2229 phases 1+2) are now closed; this revision reflects the current state.*

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
| Rc<T> / Arc<T> | ✅ block at rc0 | ✅ **T's destructor run** (`93dd38cf`; `drop_rc`/`drop_arc` move `inner.val` out → fires Drop before dealloc) | ✅ **Weak** (`d8499d1b`) — `{strong,weak,val}` control block (Rust scheme); val drops at strong→0, block at weak→0; cycle-breaking; Arc upgrade load-then-bump (full lock-free CAS = noted follow-up) |
| Box<T> | ✅ **`impl Drop`** (`d76ce7a7`): drop T + dealloc, move-only | ✅ | ✅ `Deref`/`DerefMut` (auto-deref `b.field`/`*b`); ✅ `into_raw`/`from_raw`/`leak` (`f3176745`); ✅ `Box<[T]>` (`8c49d59b`) heap-owned fat slice; ✅ `Box<Foo>` custom-DST owning (`ac85cb0e`); `Box<dyn>` via owning-TraitObject. `T:?Sized` accepted on the Box stdlib struct |
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

Runs on LIR via `borrow_check(LProgram, bool generic_templates_only)`
(`borrow_check.cpp:2201`). Two passes:

1. **Pre-mono on generic templates** (P2-10, `42998241`) — `prog.functions` with
   non-empty type-params, in **exclusivity-only mode** (`exclusivity_only_=true`):
   move-tracking is imprecise on TypeVars, so move-related diagnostics are
   suppressed; reference exclusivity / mut-binding / dropck still fire. This
   catches `&mut p + &mut p` etc. in a generic body even if it's never
   instantiated. Region inference is skipped for this pass (lifetime args are
   abstract on a template).
2. **Post-mono on concrete fns + specializations** — full mode. Two analyses
   run per fn: the structural `BorrowChecker` (this file) and the
   `RegionInferer` (`region_infer.cpp`).

**Enforced**:

- use-after-move / partial-move (sema + borrow_check trackers; structural
  `is_move_type` widened to tuple/enum/array — P2-12);
- `&mut` exclusivity vs `&` (whole-value);
- **field-path disjoint borrows** (B83) — `take_field_borrow` with
  path-overlap rule; sibling paths off the same root are disjoint;
- **RFC-2229 closure capture exclusivity** (P2-13 / RFC-2229 phase 1,
  `20c817d5`) — `||p.x` registers a borrow on the path `p.x`, disjoint
  `&mut p.y` allowed, conflicting `&mut p.x` rejected;
- **array/slice element borrow** (`b1ba5dd3`) — `&[mut] arr[i]` registers a
  whole-container borrow (Rust-conformant: `Index`/`IndexMut` take
  `&[mut] self`); `arr[i] = v` checks container borrow state;
- assign-while-borrowed (both whole-value and field-path);
- mut-binding requirement for `&mut` (params whitelisted via `param_names_`);
- **two-phase borrows** (B82) — `&mut` taken inside fn-call args is a
  reservation compatible with subsequent shared borrows in the same call;
- **NLL** (`release_dead_borrows` via per-holder `last_use_line_`) + region
  inference;
- **dropck** (B87) — `dropck_borrow_sources_` tracks which locals a
  Drop-having lifetime-parameterised binding borrows from, rejects when the
  borrow-source dies before the binding;
- **dangling-ref** — `prov_of` walks the expression's provenance; returning
  a ref whose root is a local/temp (`is_local=true`) is rejected;
- **cross-fn provenance through ref-returning calls** (`721a3780`) — `let r =
  f(&a, &b)` registers a borrow on each ref-arg held by `r` (sound upper
  bound via Rust's elision conservatism; may overshoot when an explicit
  lifetime ties the return to a specific input — refining = future work);
- **named lifetimes + elision + outlives** on returns (B66 outlives graph
  from `fn.lifetime_outlives`; B86 per-param inner-struct `lifetime_args`).

Raw-ptr roots bypass exclusivity (B93.2); `&mut`-roots also skip the
binding-mut requirement.

**NOT checked (gaps vs Rust)**: reborrows (not first-class — and entangled
with the sema peephole `&mut *r ≡ r`, which makes reborrow and rebind
LIR-indistinguishable; a sound fix needs preserving the `AddrOfTemp(Deref(…))`
shape or emitting a reborrow marker); self-referential structs; move-out-of-
deref. (Generic-fn-body borrow-check, closure-capture-mode exclusivity,
index/slice element borrows, and cross-function ref-return provenance —
formerly listed here — are now implemented per P2-10, P2-13/RFC-2229,
`b1ba5dd3`, and `721a3780` respectively. Refinement follow-up on element
borrows: compile-time-known disjoint indices like `split_at_mut`-style
two-step access without the helper.)

---

## 5. Feature × memory-management interaction matrix

| Feature | Allocation | Drop/RAII | Move/Copy | Borrow | Status |
|---|---|---|---|---|---|
| **enum** | ✅ stack `alloca` inline `{disc,payload}` (value-repr) | payload-recursive drop, no block to free | Copy-payload non-move; droppable-payload move (inline move-tracking) | classified Copy when payload Copy | ✅ Rust-like value-repr |
| **struct** | stack alloca | field-recursive drop; auto-Copy | move unless Copy; partial field moves | field-path borrows ✅ | ✅ |
| **tuple** | stack; **inline-by-value** in fields/arrays/nesting (G1 `81b28479`) | element-recursive drop | move iff any elem move | ok | ✅ |
| **array `[T;N]`** | stack | per-elem drop | move iff elem move; `let s = arr[i]` for droppable T rejected with **Rust-exact diagnostic** "cannot move out of type `[T; N]`, a non-copy array" (E0508 form); non-droppable non-Copy structs slip through via auto-Copy ([[ref_divergences_register]] — global policy, not array-specific) | ✅ element borrow tracked (`b1ba5dd3`): `&[mut] arr[i]` registers a whole-array borrow (Rust-conformant — `Index`/`IndexMut` take `&[mut] self`); `arr[i] = v` checks container borrow state | ✅ |
| **slice `&[T]`** | fat {ptr,len}; ✅ returned by value | Copy (no drop) | Copy | ✅ element borrow tracked (`b1ba5dd3`) — same whole-slice rule via `SliceIndex` chain; ✅ mut tracked on Slice (B6 closed, `c971c97f`) | ✅ |
| **Box<T>** | heap (`box_new`) | ✅ `impl Drop` (drop T + dealloc); move-only | move | ok | ✅ (accessors raw-ptr-self; `?Sized` later) |
| **Vec<T>** | heap, grow | ✅ Drop frees + drops elems | move; `into_iter` ptr-zero | IndexMut place-write ✅ | ✅ |
| **Rc/Arc** | heap inner `{strong,weak,val}` | ✅ block at rc0 (val drop at strong→0; block free at weak→0) | clone = refcount; move | ok | ✅ **Weak** + cycle handling (`d8499d1b`) |
| **String** | heap, grow | ✅ Drop | move | ok | ✅ |
| **closure** | env: stack (non-escaping) / heap (escaping); value `{fn,env}` 16-B fat; **narrow captures**: env field is FIELD-sized (escaping only) | ✅ escaping env freed via `__closure_drop__` glue; **drop-glue drops the FIELD type** for narrow captures (`e792341a`) | move captures (escaping `move` owns inline); **RFC-2229**: narrow path `p.x.y` records the leaf field type + path-precise move-tracking (escaping only) | ✅ **field-path borrow exclusivity** (`20c817d5`): `||p.x` registers a borrow on the path, disjoint `&mut p.y` allowed | ✅ **RFC-2229 phases 1 + 2** (`20c817d5`, `cda40eb2`, `e792341a`) — borrow-check field-path + move-precision env (single + multi-level, gated on escaping for owning-semantics) |
| **dyn trait** | inline 16-B fat (stack), static `.rodata` vtable | ✅ owning `Box<dyn>` drop = vtable[0]+free; `&dyn` Copy | Copy (`&dyn`); `Box<dyn>` move | ✅ object-safety (P2-15); ✅ supertrait method dispatch | ✅ uniform fat repr; ✅ **upcasting** `&dyn Sub→&dyn Super` (`527182b9`) via stored super-vtable pointers (diamonds work); ✅ `+ Send`/`+ Sync` auto-trait bound lists (`562b687d`) |
| **custom-DST** | fat `&Foo` `{data,len}` carrying tail length; owning `Box<Foo>` = owning DstRef (collapse, like `Box<[T]>`) | ✅ `Box<Foo>` drop = prefix fields + tail elements (runtime loop) + free | move (owning DstRef); construct via `dst_from_raw_parts::<Foo>(raw,n)` (+`as Box<Foo>` for owning) | unsafe (raw-pointer-shaped field access) | ✅ **B2 complete** (`1088b703` read + `ac85cb0e` owning) — `struct Foo { hdr:H, tail:[T] }` end-to-end |
| **assignment `x=y`** | — | ✅ drop-before-replace + Rust **drop elaboration** (static placement, flags only for maybe-init) | source moved (suppress double-free) | assign-while-borrowed ✅ | ✅ full Rust drop semantics |
| **match** | binds payload (may move scrutinee) | scrutinee-move avoids double-free; match-temp dropped | `mark_match_scrutinee_moved` (incl PLACE) | per-arm move union | ✅ |
| **generic fn** | per-mono | drop via mono re-mangle | move deferred to mono | ✅ body borrow-checked pre-mono in exclusivity-only mode (P2-10, `42998241`); concrete moves checked on specializations | ✅ (full generic-aware move analysis remains a refinement) |
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
13. ✅ closure mut-capture exclusivity (`4f36fd4d`) + ✅ **RFC-2229 phases 1 + 2**
    (`20c817d5`, `cda40eb2`, `e792341a`). The 4-layer plan recorded in the
    earlier revision is now LANDED:
    * **Phase 1 — capture analysis records PATHS + borrow-check field-path
      exclusivity** (`20c817d5`). The sema scanner builds per-capture dotted
      paths (`p.x.y`; FieldRead chain → root + relative path; LCA-widened when
      the body reads multiple paths off the same root). `EClosure.capture_paths`
      threads through the LIR; `lir_mirror`/`lir_view`/`mono_clone` ship it
      post-mono. `borrow_check`'s ClosureBox handler now splits the path into
      `(root, rel)` and calls `take_field_borrow` for narrow paths
      (`take_borrow` whole-value for `rel.empty()`) — `&mut p.y` next to `||p.x`
      is allowed; `&mut p.x` while `||p.x` is live is rejected with the precise
      path message; `closure_mut_capture_use` still rejects on whole-root.
    * **Phase 2 — move-precision env layout** (`cda40eb2` single-level, then
      multi-level + soundness gating in `e792341a`). `EClosure.capture_field_types`
      carries the leaf TypeRef for narrow paths (sema `field_type_for_path` walks
      nested Struct fields; null → whole-root). mlir-gen's `gep_field_chain`
      shared helper handles env-fill (source = leaf address in outer struct) and
      body-unpack (destination = leaf slot in a fake-root alloca whose other
      fields are untouched — LCA guarantees the body only reads the captured
      path). `inline_cap_type` sizes the env slot for aggregate fields. **Gating
      for soundness:** narrow owning-semantics fire only for `heap_env_pre &&
      v.is_move()` (escaping closures — env-glue actually fires on Box drop).
      Non-escaping narrow stays whole-root borrow-by-pointer; sema skips
      `mark_moved` entirely for that case (the outer root keeps ownership +
      drops the field at scope-exit, no leak). `emit_closure_drop_glue` now
      drops the FIELD type for narrow captures (root-typed drop was over-
      walking past the env field — caught while validating multi-level).
    * Remaining (deferred refinements, not soundness): full lock-free CAS in
      `Weak::upgrade` for Arc; exclusivity for inline (non-`let`-bound) closures;
      multi-supertrait narrow capture paths walking ZonedStruct boundaries.
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

**Missing features — ✅ ALL CLOSED (2026-05-29):**
* ✅ `Rc`/`Arc` `Weak` + cycles (`d8499d1b`) — `{strong, weak, val}` control
  block (Rust scheme); val drops at strong→0, block at weak→0; Arc weak is
  atomic. Residual: full lock-free CAS upgrade (load-then-bump today).
* ✅ Box `?Sized` / `Box<[T]>` (`8c49d59b`) — `Box<T:?Sized>` accepted at the
  type level; `Box<[T]>` collapses to an owning Slice (`OwningKind::Box` in
  the slice `const_val`), reusing slice layout/deref/index; construction
  `box_new::<[T;N]>(..) as Box<[T]>` (unsize coercion); `&b` deref-coerces to
  `&[T]`; drop = per-element drop (runtime loop) + buffer free. Verified
  valgrind 0/0.
* ✅ Box `Deref`/`DerefMut` — already landed in stdlib (`*b`, auto-deref
  `b.field`/`b.method()`).
* ✅ **Supertrait upcasting** + supertrait-method dispatch (`527182b9`) — full
  Rust design: stored super-vtable pointers; vtable layout is
  `[drop, size, align, transitive-method-slots, super-vtable-ptrs]`; supertrait
  methods get real slots (dispatch via `&dyn Sub`); upcast `&dyn Sub→&dyn Super`
  loads the stored super ptr — diamonds work. Single-sourced: sema
  `trait_vtable_layout` → `LTraitDef`, mlir-gen reads verbatim.
* ✅ `dyn Trait + Send`/`+ Sync` auto-trait bound lists (`562b687d`) — grammar
  accepts `+ IDENT`/`+ 'lifetime` after a trait object; vtable identical (auto
  traits add no methods). Diagnostic refinement (rejecting two non-auto traits,
  Rust E0225) recorded as a follow-up.
* ✅ `Box::into_raw`/`from_raw`/`leak` (`f3176745`) — Rust-faithful via
  `ManuallyDrop` suppression of the Box destructor; valgrind verifies the
  round-trip frees cleanly (and `leak` deliberately doesn't).
* ✅ Custom-DST tail-slice (B2) (`1088b703` read + `ac85cb0e` owning) —
  `struct Foo { hdr:H, tail:[T] }`: fat `&Foo` carries the tail length, prefix
  field access, tail-as-slice + `.len()` + indexing, `dst_from_raw_parts::<Foo>`
  construction over existing memory; **owning `Box<Foo>`** collapses to owning
  DstRef, construct via `dst_from_raw_parts(..) as Box<Foo>`, `&bb` deref-coerce
  to `&Foo`, drop = prefix-field drops + tail-element loop + free. HermesString
  stays Zone-hand-rolled (thin ptr + length-in-content + variable prefix — a
  different model, intentional Zone-side divergence).

**Raw `*dyn` escape:** `*const dyn`/`*mut dyn` is a 16-B fat pair like `&dyn`
(sema folds literal `*dyn`→bare TraitObject). There is no thin-handle heap
promotion (removed `282c5af3`). A raw dyn pointer that genuinely needs to outlive
its frame is the user's `unsafe` responsibility. The Box raw-ownership API
(`Box::into_raw`/`from_raw`/`leak`, `f3176745`) closes the explicit-give-up case
for `Box<T>`; for a bare dyn escape, widen via the existing pointer cast surface.

**Cleanups: ✅ DONE.** `is_move_type`'s aggregate recursion is single-sourced
(`move_classify.hpp` skeleton + per-phase callbacks, `54bb4f77`); `needs_drop` /
`has_droppable_fields` intentionally stay phase-specific (sema's is generic-aware
on live TypePool state, borrow_check's is the minimal post-mono form — merging
them would be net-negative). The enum heap-promotion band-aid was removed by the
enum value-repr landing (`51d2e29e`), and Vec's dual manual-free (`vec_free`) by
the container element-drop landing.

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

**2026-05-29 session — entire "missing features" shelf + RFC-2229, in order:**

- **Box::into_raw/from_raw/leak** (`f3176745`) — `ManuallyDrop` suppression of
  the Box destructor on ownership transfer; valgrind round-trip clean.
- **`dyn Trait + Send`/`+ Sync`** (`562b687d`) — grammar `dyn_auto_bounds`
  suffix; auto traits add no vtable slots so layout/dispatch unchanged.
- **Supertrait upcasting + supertrait-method dispatch** (`527182b9`) — vtable
  layout extended to `[drop, size, align, transitive method slots, stored
  super-vtable pointers]`. Sema `trait_vtable_layout` is the single source of
  truth (LTraitDef carries `vtable_method_order` + `upcast_supertraits`); mlir-
  gen reads them verbatim. Cast codegen loads the stored super-vtable ptr —
  diamonds verified. The post-lowering materialiser handles a new
  `__logos_vtref__<sym>` slot kind for the stored vtable references.
- **Rc/Arc Weak** (`d8499d1b`) — control block expanded to `{strong, weak, val}`;
  val drop at strong→0, block free at weak→0 (the Rust scheme). Two-i32 header
  shifts `val`'s offset → the Rc/Arc<dyn> unsize+drop codegen offset goes
  `round_up(4,…)` → `round_up(8,…)` (mlir_gen_expr + mlir_gen_stmt).
- **Box<[T]>** (`8c49d59b`) — `Box<T:?Sized>` accepted; `Box<[T]>` collapses to
  an OWNING Slice (kind `Slice` with `OwningKind::Box` in `const_val`, folded
  into TypeUID + equality). Unsize coercion `Box<[T;N]> as Box<[T]>` in the cast
  handler builds `{data, len=N}`; `&b` deref-coerce to `&[T]`; drop = per-element
  runtime loop + buffer free. Mirrors the `Box<dyn>` owning-trait-object collapse.
- **Custom-DST tail-slice (B2)** (`1088b703` read + `ac85cb0e` owning) — read
  side was already partial (DstRef machinery for `&Foo`, prefix field access,
  tail-as-slice, `dst_from_raw_parts`); locked in with an end-to-end test, then
  added **owning `Box<Foo>`** via the same collapse pattern: `Box<DstStruct>` →
  owning DstRef with `OwningKind::Box` in `const_val`. Construction via
  `dst_from_raw_parts(..) as Box<Foo>`; `&bb` deref-coerce to `&Foo` (var_ref
  load — the owning DstRef value IS a reference, and DstRef is an alloca
  binding); `gen_drop_owning_dst` drops prefix fields (gep_field) + tail
  elements (runtime loop, gep_field to tail base) + frees the block. Lesson
  baked into the memory file: HermesString stays Zone-hand-rolled (thin ptr,
  length-in-content, variable prefix — a different model from Rust fat-DST and
  intentionally NOT migrated).
- **RFC-2229 phase 1 — field-path capture exclusivity** (`20c817d5`) — sema
  scanner emits per-capture dotted paths + LCA widening; LIR + mirror + view +
  mono_clone thread the path post-mono; borrow_check splits the path into
  (root, rel) and uses `take_field_borrow` for narrow paths. `||p.x` next to
  `&mut p.y` is allowed; `&mut p.x` is precisely rejected.
- **RFC-2229 phase 2 — move-precision env layout** (`cda40eb2` single-level →
  `e792341a` multi-level + gating + drop-glue fix). `EClosure.capture_field_types`
  parallel array carries the leaf field TypeRef; sema `field_type_for_path`
  walks nested Struct fields; mlir-gen `gep_field_chain` handles env-fill
  source (leaf in outer struct) and body-unpack destination (leaf in fake-root
  alloca). `inline_cap_type` sizes the env slot for aggregate fields.
  Soundness gating: narrow owning-semantics fire only for `heap_env_pre &&
  v.is_move()` (escaping closures); non-escaping narrow stays whole-root
  borrow-by-pointer and sema skips `mark_moved` entirely so the outer root
  drops the field at scope-exit. Drop-glue widened to drop the FIELD type (not
  the root) — pre-fix it over-walked past the env field, caught when validating
  multi-level Noisy droppable elements.

- **Element-borrow gap closed** (`b1ba5dd3`) — `&[mut] arr[i]` and slice
  indexing now register a whole-container borrow (Rust-conformant via the
  `Index`/`IndexMut`-takes-`&[mut] self` rule). Three wired sites: the
  AddrOfTemp chain walk extended through `IndexRead`/`SliceIndex`; the
  `IndexWrite` / `FieldIndexWrite` stmt handlers mirror SAssign's exclusivity
  check on the container; `DerefWrite` (which the place-writer retirement
  lowers `arr[i] = v` to) walks the AddrOfTemp chain and does a transient
  conflict check on the leaf root. Cases now rejected: `&mut arr[0]; &mut
  arr[1]`, `&mut arr[0]; &mut arr`, dup `&mut arr[0]; &mut arr[0]`,
  `&arr[0]; arr[1] = …`. NLL release on inner-block scope exit unchanged.

**Gating discipline (still in force):** every step gates on the FULL suite
(`bash ../tests/logos/ctest-summary.sh`, currently **5283/5283**) and valgrind on
a representative droppable round-trip; rebuild clean (a stale `.o` once hid a
regression). Memory work is verified by *exact drop counts*, not just pass/fail.
