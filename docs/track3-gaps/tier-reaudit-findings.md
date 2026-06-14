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

## Documented — confirmed soundness holes, NOT yet fixed (need dedicated work)

### MOVE-OUT-OF-BORROW (E0507) — a whole class, unenforced
Logos accepts moving a move-type value out of a place it doesn't own — rustc
E0507. Confirmed cases (all accepted; rustc rejects; runtime double-free/abort):
- `let t = *r;`  (r: `&String`)              — move out of `*&`
- `fn f(r: &mut String) -> String { *r }`    — move out of `*&mut`
- `fn f(r: &S) -> String { r.name }`         — move a field out of `&S`
- `let s = v[0];` / `f(v[0])`  (Vec<String>) — move out of index (`f2b` aborts EXIT:134, double-free)

A naive check (flag a consumed Deref/Index of move-type behind a `&`/`&mut`
VarRef) was implemented and **reverted**: it false-positives on a destructure
that binds only Copy fields — `let Fd(s) = *self;` with `Fd(u32)` (test
`newtype-struct-with-dtor`) is valid (the u32 is copied, Fd is not moved). The
correct fix is PATTERN-AWARE: a move out of a borrow is an error only when the
binding actually moves a move-typed part. Also Box deref-move (`let s = *b`)
must stay allowed (Box owns its content). Needs per-binding Copy analysis at the
SLet/pattern level, not a node-local Deref check.

### Box deref-move runtime double-free
`let s = *b;` (b: `Box<String>`) compiles but aborts (EXIT:134, double-free) —
the String is bit-copied out without suppressing the Box's drop. Pre-existing;
orthogonal to the E0507 class above.

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
