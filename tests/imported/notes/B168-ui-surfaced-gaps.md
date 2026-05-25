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
- **g2** `HashMap<i64, Box<dyn Sh>>` ← `insert(k, box_new(Sq{..}))` — **CRASH (MLIR-gen**,
  `getelementptr operand #0 must be pointer, got i32`). Same family: the `insert`
  arg (generic `V`=`Box<dyn>`) gets a concrete `Box<Sq>` not fattened. (insert is a
  `*mut self` raw-ptr method — a different arg path than G167-7's method coercion.)
- **g6** `for b in &Vec<Box<dyn Sh>>` — **REJECT** *"method call: receiver is not a struct
  (got &&dyn Sh)"*. for-by-ref yields `&Element` = `&Box<dyn>` which erases to `&&dyn Sh`;
  `b.area()` doesn't deref+dispatch the boxed trait object. PROBED 2026-05-24: adding a
  `&TraitObject`→`TraitObject` auto-deref in `try_method_on_dyn` unblocks SEMA (compiles)
  but then SIGSEGVs at runtime — the crash is DEEPER, in the for-each / VecIter-over-dyn
  element addressing (the `while i<len { v.get(i).area() }` form works — banked
  vec-box-dyn-dispatch-b167). Reverted (a clean reject beats a silent crash). Root: VecIter
  yielding a `&`(dyn-handle) element + dispatch. Separate sub-sprint.

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

**Fix direction:** either (a) `Vec::get` returns `&T` (Rust semantics) and callers
borrow — broad API change; or (b) the by-value `get` of a move-type element returns
a *deep clone* (requires `Clone`); or (c) suppress the Vec's element-Drop for an
element whose ownership was handed out by `get` (move-out tracking). Rust's answer is
(a) `&T` + a separate `swap_remove`/`remove` for by-value. Intersects Vec Drop glue ×
move-type element × get-by-value API.

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
