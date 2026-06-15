# Tier-1/Tier-2 re-audit — adversarial pass over the ✅-marked audit items (2026-06-14)

Per Victor's directive: go item-by-item over the feature-audit's ranked Tier-1
(soundness, items 7–13) and Tier-2 (parity, items 14–30) "✅ DONE" moves and
adversarially re-test each — "done" ≠ "hole-free". rustc 1.93 oracle, probes in
/tmp/{t1p,t2p,adv4}. **6 bugs fixed (all full-L4 green), 2 classes documented.**

## Fixed (committed)

| Commit | Tier | Bug |
|---|---|---|
| `4e88805f` | T1 #10 | Bare TypeVar not move-classified in borrow_check's partial-move tracker → partial move of a generic field undiagnosed in generic bodies (`fn bad<T>(s:Pair<T>){take(s.a);take(s.a)}`, rustc E0382). Bound-aware is_move_type (move unless `T:Copy`), Tier-1 keeps partial-move diagnostics. |
| `ef37d8fb` | T1 #10 | False positive: disjoint sibling read after a deep partial move (`take(o.i.s); o.i.t`) rejected — the moved-overlap check fired on the intermediate `o.i` projection. Split overlap directions; parent-of-moved only errors for a genuine whole read, not an intermediate projection to a disjoint leaf. |
| `b999687e` | T1 #12 | `Box`/`Rc`/`Arc<dyn Trait + Send>` dropped the auto-bound at type construction (make_unsized_dyn_type didn't carry the bits) AND the coercion check only peeled `&`/`&mut` — a non-Send concrete laundered into a Send dyn at every site (rustc E0277). UnsizedDyn carries the bits; check peels smart pointers. |
| `1847e8bc` | T1 (sound) | `&mut v[i]` (user IndexMut) recorded a SHARED borrow, not mut — two live `&mut v[i]` (even same index) aliased undetected (rustc E0499). method_self_kind can't resolve the desugared index_mut; the outer `&mut` is now the authoritative mutability (reborrow_force_mut_). |
| `036a5b55` | T2 #16 | PartialOrd dispatch assumed `partial_cmp -> Ordering`; the canonical Rust `-> Option<Ordering>` crashed mlir-gen (`Option__is_lt` undefined). Route through concrete `cmp_opt_is_{lt,le,gt,ge}` helpers (None ⇒ false). |
| `94f69732` | T2 (misc) | Tail-position enum literal in a fn returning a MULTI-type-param generic enum left the unconstrained param `<error>` → `unknown tagged enum Either__i64__<error>` + corrupt return ⇒ **runtime segfault**. `fn f()->Result<T,E>{Ok(v)}` is the canonical case. Thread ret_type_ as the enum-inference hint in the tail path (the `return e;` path already did). |

## MOVE-OUT-OF-BORROW (E0507) — PARTIALLY FIXED (457ca8ee), rest documented

Logos accepted moving a move-type value out of a place it doesn't own — rustc
E0507/E0508 → double-free/use-after-free. **NOW FULLY CLOSED** across all
positions (commits 457ca8ee, 214f04f7, 9753b60f, 8c71acb7, d6c20319):
- `let t = *r;` / `return *r` / `f(*r)`        (deref of a `&`/`&mut` VARIABLE)
- `let s = arr[i];` / `return arr[i]` / …      (array/slice index, non-raw)
- `let s = v[i];` / `f(v[i])` (Vec/user Index) (`*(v.index(i))`)
- `fn f(r:&S)->T{ r.field }`                    (field out of a `&`/`&mut` receiver)
- `let s = *b;` (Box/Rc/user Deref move-out)    (`*(x.deref())` — see below)

**Mechanism:** `is_unowned_move_source(e)` (sema_impl.hpp) recognizes the
unowned-place shapes — Deref of a ref VARIABLE; IndexRead/SliceIndex; and Deref
of an `index`/`index_mut`/`deref`/`deref_mut` call. Wired at the four by-value
move sites: `lower_let`, both return-coercion sites, and `coerce_arg_to_param`
(the single call-arg chokepoint). Gated by `is_move_type` (Copy copies out
fine). Exemptions: any place rooted through a RAW-pointer hop (`(*p).f[i]`,
`self.data[i]` with self:*mut — unsafe, the programmer owns aliasing; this is how
mem/ptr/Vec primitives legitimately move out). Owned-self field reads are partial
moves (receiver is a Struct, not a reference) — allowed.

**Root-cause fixes that unblocked it (vs N local rewrites):**
- **Non-owning `&[T]` slice is Copy** (compute_auto_copy_types): a shared slice
  is a fat pointer (Rust parity; `Box<[T]>` owning stays move). Made metaprog
  `Type` (a `&[u8]`-field struct) Copy, resolving ~8 meta.logos `Type`-array
  sites at once.
- stdlib `ArrayIntoIter::next` + meta.logos `head` read via raw pointer (the two
  genuine safe-context array-index move-outs; Rust uses ptr::read).

**Box DerefMove — IMPLEMENTED (ff8e243b).** `let s = *b` / `return *b` over a
move-typed Box now MOVE the boxed value out (Rust parity): ownership transfers
to the binding, the heap block is freed, the content is NOT dropped via the Box
(no double-free — valgrind-clean; previously bit-copied → double-free abort
EXIT:134). New stdlib `box_take<T>(b)->T` = `box_into_raw` (consume b via
ManuallyDrop, suppress Box::drop) + raw read (move out, no drop) + `dealloc`
(free block). sema `try_lower_box_deref_move` desugars `*<box-var>` → box_take
via finish_generic_call, hooked at lower_let / lower_return / tail-return.
Copy-element Box still copies (b stays live); non-Box Deref (`*rc`, user Deref)
move-out stays E0507 (only Box has DerefMove); `b` is consumed after `*b`. Arg
position (`f(*b)`) stays E0507 (rare; `let t=*b; f(t)` workaround).

**Tests fixed (were invalid Rust — unbounded generic getter / `*self` returns a
field/elem by value out of a borrow; compiled only because instantiations are
Copy; added the `T: Copy` rustc requires):** regions-early-bound-used-in-type-
param, generic-struct-methods-st2, generic-pair-methods-b163, generic-multi-impl
-on-type-arg-b156, generic-two-param-box-b164, generic_match_ergonomics_typevar,
method-self-arg (`impl Copy for Foo` restored — empty struct is move by default).

Regressions: 12 tests under `move_out_*` / `move_copy_*` / `slice_field_*` /
`box_deref_*`. Full L4 5698/5698.

## Verified solid (probed, no divergence)
Closure auto-traits over captures incl. `*mut`/Rc-move (#7); or-pattern E0408
nested+top (#8); E0184 Copy+Drop incl. generic (#11); extern-static read/write
gating (#13); CTFE assoc-const / array fill (#14); Not / comparison ops, real
`for 0..=n` ranges (#15/#16/#17); where-clause concrete subject (#18); lifetime
elision E0106 (#19); matches!/patterns `S{ref a}`/`S{0:a}`/`(..)` (#21/#27);
match ergonomics &/&mut payload (#26); deep partial-move siblings/drop;
dyn-in-Vec dispatch; nested generic enums; ?-operator; slice patterns; integer
overflow trap; casts/closures-as-fnptr/tuples/shadowing/char/bitops. The common
paths are robust; divergences cluster in deep feature intersections.

Note: `step_by` on a range takes a non-standard `zero: Item` 3rd arg (a
deliberate stdlib stopgap, not a compiler bug).
