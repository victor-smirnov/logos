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
E0507/E0508 → double-free/use-after-free. Confirmed cases:
- `let t = *r;` (r: `&String`)               — **FIXED** (lower_let + is_unowned_move_source)
- `let s = arr[i];` ([String;N])             — **FIXED** (array index, non-raw)
- `fn f(r: &mut String) -> String { *r }`    — open (return position)
- `fn f(r: &S) -> String { r.name }`         — open (field-out-of-&self)
- `let s = v[0];` (Vec<String>)              — open (Vec index lowers to Deref(index_call))

**Fixed slice (457ca8ee):** `is_unowned_move_source` flags `*ref_var`
(Deref of a `&`/`&mut` VARIABLE) and `arr[i]`/`s[i]` (index of a NON-raw
container) of a move type, wired at `lower_let`. Raw-ptr deref/index is exempt
(unsafe; mem/ptr/Vec primitives move out via `p[0]` with p:*mut T). stdlib
`ArrayIntoIter::next` rewritten to a raw-ptr read (it was the one safe-context
violation). Copy values copy out; Box deref-move stays allowed.

**Why the rest is hard (do NOT retry naively):**
- **return/arg position**: a node-local check at the return-coercion site broke
  131 L2 tests + the stdlib build (`meta.logos head` etc.) — return-position
  move-out is pervasive (much of it sound-by-overwrite or copy). Needs flow/
  context awareness.
- **Vec index** (`let s = v[0]`): lowers to `Deref(Vec::index(&v,i) -> &T)` —
  STRUCTURALLY IDENTICAL to Box deref-move `*b` = `Deref(Box::deref(&b) -> &T)`,
  which is LEGAL. Distinguishing needs callee inspection (index/index_mut vs
  deref/deref_mut, and which deref impls permit move-out).
- **field-out-of-&self** (`r.name`): same shape as the ubiquitous `let x =
  self.f` in `&mut self` methods — flagging it surfaces pervasive stdlib uses.
- **destructure whole-temp**: `let Fd(s) = *self` desugars to a whole-value temp
  `__pat = *self`; a node-local check false-positives when the pattern binds
  only Copy fields (`Fd(u32)`, test `newtype-struct-with-dtor` is valid). Needs
  PATTERN-AWARE per-binding Copy analysis at the SLet/pattern level.

The proper complete fix: rewrite stdlib mem/ptr/Vec move-out primitives onto
explicit unsafe `ptr::read` (so they're exempt), then enable the check in all
safe contexts (return/arg/field/destructure) gated on `!inside_unsafe_` with
per-binding Copy analysis. A dedicated session.

### Box deref-move runtime double-free

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
