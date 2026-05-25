# B168 — Adversarial Depth-Probe: generic containers × non-scalar elements

> STATUS 2026-05-24: census only (no fixes yet). 10 probes (g1–g10). 5 PASS,
> 5 surface gaps in **two deep architecturally-rooted clusters** (G168-A, G168-B),
> both high-impact. Repros in `b168-repros/`. This axis was chosen as the
> highest-impact Tier-1 adversarial target (generalizing the B167 G167-7 fix).

Provenance: hand-written adversarial probes (rustc-idiom-equivalent), targeting
the boundary where a NON-scalar element/payload type flows through a GENERIC
container or enum. Compiler used as-is: `build/bin/logosc`.

Legend: **MISCOMPILE** = links + wrong result; **CRASH** = SIGSEGV/SIGABRT/MLIR-gen
fail on legitimate code; **REJECT** = sema/parse rejection of code that should work.

---

## PASS — intersections that already work (banked candidates)
- **g1** `Vec<&dyn Sh>` — push `&concrete`, loop-dispatch `area()`. ✓ (fat REF element works; contrast g5/g6 which use `Box<dyn>`)
- **g3** `Vec<(i64, String)>` — push tuples with a move-type (`String`) element, read both fields. ✓ (=8)
- **g7** `Vec<Guard>` where `Guard: Drop` — push 2, Vec dropped at scope exit fires Drop exactly twice (counter via `*mut i64`). ✓ (Drop glue over Vec elements is correct for a plain Drop struct)
- **g8** `HashMap<i64, Point>` — struct value, insert + get + field read. ✓ (=7)
- **g9** `Vec<Option<i64>>` — enum element, push Some/None/Some, match-sum. ✓ (=30)

---

## G168-A — unsize coercion through a generic container/enum payload doesn't FATTEN the value

**Root (high-impact, architectural).** When a concrete `Box<Concrete>` / `&Concrete`
flows into a slot whose type is `Box<dyn Trait>` / `&dyn Trait` *via a generic
param or enum payload*, sema RELABELS the value's type to the trait object but
never emits the unsize *fattening* (build the `{data, vtable}` fat handle). Only
the **fn-argument** consumer fattens (the B167 G167-7 mlir-gen arg-coercion). So a
THIN `Box<Concrete>` (8-byte ptr, no vtable half) is stored, and later dispatch
reads a garbage vtable → crash. Confirmed: coercing to `dyn` BEFORE the
container op (g5a: `let b: Box<dyn> = ..; Some(b)`) works → the defect is purely
the in-place relabel-without-fatten.

- **g5** `Option<Box<dyn Sh>>` ← `Some(box_new(Sq{..}))` — ✅ FIXED 2026-05-24.
  `Option::Some(..)` lowers via `lower_enum_lit_data_from_static`; its TypeVar
  inference now records the enum's type-arg as the DYN type from the hint
  (projected into `pre_subst`) when the arg is a concrete coercible value, while
  leaving the payload expr concrete — so mlir-gen's enum-payload store
  unsize-fattens (`coerce_value_to_dyn_if_needed`) the concrete `Box<Sq>` into the
  `Box<dyn Sh>` slot instead of storing a thin handle. Test:
  traits/option-box-dyn-dispatch-b168.
- **g2** `HashMap<i64, Box<dyn Sh>>` — bisected 2026-05-24: `insert` ✓ and `get` ✓ both work
  (the dyn coercion at insert is fine). The crash is the **dispatch** on the retrieved element,
  in two facets — both about dispatching a trait object reached through EXTRA indirection:
  - `p[0].area()` (raw-ptr INDEX of `*const Box<dyn>`, what `m.get` returns) → MLIR-gen
    verify failure (`getelementptr operand #0 … got i32`): a raw-ptr-index-of-TraitObject
    codegen bug.
  - `let b = *p; b.area()` (explicit deref) → COMPILES but **SIGSEGVs at runtime** — the SAME
    receiver-indirection root as **g6**: `gen_dyn_dispatch` materialises the handle at the wrong
    indirection level for a deref/VarRef receiver (an inline-expr receiver works).
  PARTIALLY FIXED 2026-05-24 — see g6 below: borrowed-trait-object dispatch now works for
  inline + let-bound receivers; the `*p`/`p[0]` raw-ptr forms still need work (g2 facets).
- **g6/dispatch-indirection** — ✅ PARTIALLY FIXED 2026-05-24. ROOT: `mlir_gen_stmt.cpp` gen_let's
  "already-a-fat-pointer" check peeled only `Ptr`, not `Ref`/`MutRef` — so `let rd: &Box<dyn> =
  v.borrow(0)` mis-REBUILT a bogus `{ptr-to-handle, garbage-vtable}` slot instead of storing the
  borrowed handle, and every dispatch combo read garbage. Fix: peel Ref/MutRef there; accept a
  `Ref<TraitObject>` receiver in sema `try_method_on_dyn`, the dispatch gate, and load the handle
  once in `gen_dyn_dispatch`. Now **borrowed-trait-object dispatch works** for inline
  (`v.borrow(i).area()` = 13) AND let-bound (`let rd = v.borrow(0); rd.area()` = 4) receivers.
  Test: traits/borrowed-dyn-dispatch-b168. Full suite 5162/5162.
  ✅ for-each FIXED 2026-05-24 (bird's-eye root): the real bug was NOT the dispatch nor the
  for-each binding — it was a **mis-fired dyn→dyn pointer-cast coercion**. `Vec<&dyn T>::as_slice`
  (and `VecIter`) do `self.ptr as *const T`; with `T` a trait object the `*X as *const dyn`
  cast branch ran `coerce_to_dyn` even though the SOURCE pointee was already a TraitObject —
  building a bogus fat slot and so storing `slice.data` at a 2-level indirection (`&(buffer)`),
  which is why every downstream read landed one level off. Fix: gate both `as *dyn` / `as &dyn`
  coercion branches on the source pointee being CONCRETE (a dyn→dyn reinterpret is a no-op).
  Now `for b in &v` AND `for b in v.iter()` over `Vec<Box<dyn>>` dispatch correctly. Test:
  traits/foreach-vec-box-dyn-b168. This also explains the earlier "2-level for-each storage"
  red herring — the storage was fine; the slice data pointer was corrupt.
  STILL OPEN (one ARCHITECTURAL root — the `*const dyn` representation overload):
  - **g2 HashMap<_, Box<dyn>>::get** — `*m.get(&k)` SIGSEGVs at dispatch. ISOLATED 2026-05-25:
    `get` returns `*const V` = a pointer to the value slot holding the 8-byte dyn HANDLE, so
    `*p` must LOAD the handle. But `EDeref` of a `TraitObject` is a NO-OP — because the SAME
    `*const dyn`/`Ptr<TraitObject>` type is ALSO produced by `&concrete as *const dyn`
    (dst-raw-trait-object-b158), where the value IS the handle (a fatslot ptr) and `*z` must be
    a no-op. The two meanings — "handle" vs "pointer-to-handle" — are INDISTINGUISHABLE by type
    (`Box<dyn>`/`dyn`/`&dyn` all collapse to `TraitObject`; `*const X` of either is
    `Ptr<TraitObject>`). Local fixes trade one for the other: making `EDeref(TraitObject)` LOAD
    fixes HashMap (g2x/g2v ✓) but regresses dst (and a raw `&bx`→`*const Box<dyn>` probe);
    spilling the `as *const dyn` cast to a ptr-to-handle (to keep both) regresses the container
    cases. The proper fix is a CONSISTENT representation — make `*const/*mut dyn` UNIFORMLY a
    pointer-to-handle (the cast spills the handle; all `*p` load) — a focused but multi-site
    architectural change touching `coerce_to_dyn` cast sites + every `*const/*mut dyn` user
    (persistent/NodeARC). Deferred as its own sprint. Workaround: dispatch through a value
    binding obtained another way, or `&dyn`/`Box<dyn>` by value (`for`/`get`/`borrow`), all of
    which now work.
  - (the cast fix DID make a raw `let p: *const Box<dyn>=&bx; (*p).area()` work for a stack
    handle; the regression surfaces specifically when `*const dyn` must mean ptr-to-handle.)
  - **g2 `p[0].area()`** raw-ptr INDEX of `*const Box<dyn>` — MLIR-gen GEP crash; `*p` works.
  (was) **for-each** — superseded by the entry above. Original note kept:
  - ~~for-each over `Vec<Box<dyn>>` still SIGSEGVs~~ — gdb-isolated 2026-05-24: the for-each
    slice-binding stores `scope_[b]` at a **2-level** indirection (`&(&buffer[i])`) for a
    dyn element while typing `b` as `Ref<TraitObject>` (one level), so the single dispatch
    load lands on the buffer base, not the handle (gdb: `self=&buffer[0]`, `vtable=null`). The
    let-bound (`var_dyn_trait_`, 1-level) and inline (expr) forms are 1-level and work; the
    for-each (`var_subscript_`/`ref_param_names_`) path needs its element-storage indirection
    reconciled with its declared type — a focused for-each-binding fix, distinct from the
    (now-fixed) dispatch site. `for b in v.iter()` (VecIter) is yet another binding path.
    Workaround: `while i<len { v.get(i).area() }` (banked vec-box-dyn-dispatch-b167) or
    `while … v.borrow(i).area()` (banked borrowed-dyn-dispatch-b168).
  - **g2 `p[0].area()`** (raw-ptr INDEX of `*const Box<dyn>`) — MLIR-gen verify crash, a
    raw-ptr-index-of-TraitObject codegen bug; `*p` deref now works via the dispatch fix.
- (was) **g6** `for b in &Vec<Box<dyn Sh>>` — **REJECT** (clean) — superseded by the entry above.
  for-by-ref yields `&Element` = `&Box<dyn>` which erases to `&&dyn Sh` (`Ref<TraitObject>`);
  the dispatcher only accepted a bare `TraitObject` receiver. Plumbed acceptance of a
  `Ref<TraitObject>` receiver through sema (`try_method_on_dyn`), the dispatch gate
  (`gen_expr_kind(EMethodCallView)`), and `gen_dyn_dispatch` (load the handle once before the
  {data,vtable} GEP). RESULT: the **inline-expr** form `v.borrow(i).area()` then dispatches
  CORRECTLY (=13). But ANY **VarRef** receiver of `Ref<TraitObject>` type — a `let rd =
  v.borrow(0); rd.area()` binding OR the `for b in &v` loop var — SIGSEGVs: the var's storage
  in `scope_` carries a different indirection level than the inline-expr value, and none of the
  four {scope_-shortcut, gen_expr} × {load, no-load} combinations is correct for both paths at
  once. Root = how a `&`(pointer-repr) local is stored/materialised (scope_ alloca vs value)
  for VarRef vs expression receivers — a focused mlir-gen var-indirection sub-sprint, NOT a
  dispatch-site patch. Reverted (a clean reject beats a silent crash). Workaround: the
  `while i<len { v.get(i).area() }` form (get returns the handle by value — `Box<dyn>` is
  non-Drop so the G168-B guard allows it) — banked & passing as vec-box-dyn-dispatch-b167.

**Fix direction (one root, multiple sites):** make unsize coercion *fatten the value*
wherever it currently only relabels — the let-binding-into-generic, enum-variant
payload, and container-element-store paths. Likely a sema-level coercion node
(`EUnsizeToDyn`) emitted at the coercion boundary that mlir-gen lowers via the
existing `coerce_to_dyn`, instead of a silent type relabel. The reusable mlir-gen
consumer helper (`coerce_value_to_dyn_if_needed`) was prototyped + reverted — it's
correct but inert until sema stops pre-relabeling. Same fat-pointer-storage family
as G167-7 (fixed) and G167-3b (boxed-closure env).

---

## G168-B — `Vec<T>` of a move/Drop type: `get(i)→T` aliases the element → double-free

**Root (high-impact, architectural).** `Vec::get(&self, i) -> T` returns the element
BY VALUE — a shallow copy of the `T` struct. For a move/Drop `T` (`String`, `Vec<_>`,
any owner of a heap buffer) the copy aliases the stored element's backing pointer.
At scope exit BOTH the copy's Drop and the Vec's element-Drop free the same buffer
→ **double-free (SIGABRT)**. Reproduces with a SINGLE element + single read (not a
2-element artifact).

- **g4** `Vec<Vec<i64>>` — push one inner vec, `outer.get(0).get(0)` — **CRASH (SIGABRT)**.
- **g10** `Vec<String>` — push one string, `v.get(0).len()` — **CRASH (SIGABRT)**.

Boundary: `Vec<Option<i64>>` (g9) and `HashMap<i64,Point>` (g8) work — element has no
Drop. `Vec<(i64,String)>` (g3) returned cleanly — a separate subtlety (tuple-element
Drop glue over Vec may not be wired, so no double-free fires; needs follow-up).

✅ FIXED 2026-05-24 (clean-error guard + existing `borrow`). Logos already has the
Rust-parity accessors: `borrow(i) -> &T` (= Rust `&v[i]`), `remove(i)`/`pop() -> T`
(owning, decrement `len` so no double-drop). `get(i) -> T` stays the Copy-convenience
by-value read. The fix: `lower_method_call` now rejects `Vec::get` on a NON-Copy
(move) element — *"cannot move a non-Copy element out of `Vec` via `get` … use
`.borrow(i)` / `.remove(..)` / `.pop()`"* — exactly Rust's "cannot move out of index."
This converts the silent double-free into a clean compile error; Copy-element `get`
(the ~225 existing callers) is unaffected. The only stdlib site that moved a move-type
out via `get` was `Debug for Vec<T>` (latently double-freeing for `Vec<String>`) — now
reads via `borrow`. Tests: array-slice-vec/vec-string-borrow-b168,
array-slice-vec/vec-vec-borrow-b168 (the read idiom via `borrow`).

---

## Summary
- **5 PASS, 5 gaps in 2 deep clusters.** Adversarial hit rate stays high on this axis.
- **G168-A** (g2/g5/g6): unsize coercion through generic container/enum payload =
  relabel-without-fatten. One root, ≥3 sites. **Highest impact** — `Option<Box<dyn>>`,
  `Vec<Box<dyn>>` via push of a concrete, `HashMap<_,Box<dyn>>` are all idiomatic.
- **G168-B** (g4/g10): `Vec<move-type>` get-by-value aliasing → double-free. Also
  high impact (any `Vec<String>` / `Vec<Vec<_>>` read-then-drop).
- Both are focused mini-sprints (architectural), not point fixes — surfaced by the
  prioritized adversarial pass, as intended (gap census = primary deliverable).
