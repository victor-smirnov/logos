# Borrow-Checker Escape Analysis — Design

Goal: enforce the remaining Hermes2 (and general) borrow-safety properties the
checker does NOT yet prove — **a reference may not outlive what it borrows** —
without false-positiving the legitimate patterns that make this hard
(heap-owning locals, leaks, references-through-references, method results).

Status: **design (2026-06-06)**, implementation not started. Written after FOUR
failed probe-fixes (3 suite blowups + a wrong sub-agent analysis) proved this is a
real dataflow feature, not a one-liner. Companion to
[hermes2-minimal-container-plan.md](hermes2-minimal-container-plan.md) (its step 3).

File: `src/compiler/borrow_check.cpp` (+ `include/logos/compiler/borrow_check.hpp`).

---

## 1. What the checker ALREADY enforces (verified, do not re-doubt)

Re-probed carefully (several earlier "gaps" were MISTESTS — a stale binary, or an
unused borrow released by NLL before the conflict point):

- **Direct borrow vs assign / move** — `let v = &x; x = 2;` rejected; field path
  too (`&c.x` then `c.x = 2`). (`SAssign` conflict + `visit(AddrOfTemp)` checks.)
- **`&mut self` method call while a DIRECT borrow of the receiver is held+used** —
  `let r = &c; c.set(…); use r` rejected. `visit(AddrOfTemp)` (borrow_check.cpp
  ~2201) checks `is_mut && shared_borrows>0` on the receiver root. The receiver of
  a `&mut self` call is lowered as `AddrOfTemp(…, is_mut=true)` — `is_mut()`
  cleanly distinguishes `&self` (0) from `&mut self` (1) at the call site.
- **Use-after-move** (Phase 1).
- **Return of a local by elided lifetime** — `fn f() -> &i64 { let x=1; return &x }`
  rejected (`AddrOf(VarRef)` → `prov_of` line ~862 checks `states_`).

So the easy enforcement is DONE. `prov_of` is consumed ONLY by
`check_return_value` and the `prov_` map (Let/Assign/branch-merge) — its blast
radius is return-checking, but the set of expressions it touches is large.

## 2. The three real gaps (all hard — root causes)

**(c) return-of-local FIELD/INDEX chain.** `return &c.x` (c a value local) is NOT
caught: it lowers to `AddrOfTemp(FieldRead(VarRef c))`; `prov_of` recurses to
`prov_of(VarRef c)`, which checks `param_names_` + the `prov_` map but NOT
`states_`, so it returns empty = "unknown/safe". (Contrast `&c` = `AddrOf(VarRef)`
which DOES check `states_`.) The naive fix — make `prov_of(VarRef local)` return
`is_local` — **blew up 5064/5547**: it flags every reference rooted at a value
local, including legitimate HEAP references (`Box::leak(b)` where `b` is a by-value
owning param that is LEAKED, so `&*b` is valid; `&vec[i]`; iterators). Empty
provenance deliberately swallows both genuine-safe (heap/leak/global) and
genuine-dangling (frame field).

**(a) method-RESULT reference not held.** `let v = c.get_ref(); c.set(2); use v`
is NOT caught — a reference returned by a method does not record a borrow of the
receiver. The naive fix (borrow the receiver in `take_ref_borrows` MethodCall)
**broke persistent_showcase**: it over-borrows methods whose result does NOT alias
the receiver (returns a global, or re-borrows an ARG). Needs per-method lifetime
elision ("does the result borrow `self`?").

**(b) borrow THROUGH a reference is untracked (B93.2, intentional today).** `&*a`
where `a: &mut ZArrayAny` has root kind `MutRef` → `visit(AddrOfTemp)` skips
tracking (line ~2160). So objects accessed via a `&mut`/`*mut` (the whole Hermes2
container, since arrays are held as `&mut ZArrayAny`) get NO exclusivity, and
`return a` (a from `h.array()`, h a local) is not caught (method-result provenance
is empty for a local receiver).

## 3. Core model — a provenance LATTICE with path + escape classification

The root defect: `RefProv` collapses to "unknown/safe (empty)" too eagerly. Refine
provenance for a borrowed reference into a small lattice over its **borrow path**:

| Origin | Meaning | On return |
|---|---|---|
| `Param(set)` | rooted at one/more ref params | SAFE (tie to caller; existing) |
| `Static` | a module const / `'static` | SAFE |
| `FrameDirect(var)` | a field/tuple/index chain rooted at a value local, with **NO deref through an owning pointer** — the target is IN the stack frame | DANGLING (var drops) — **unless var is consumed/moved-out before return** |
| `Heap(owner)` | the path crosses a `Deref` of an owning pointer (`Box`/`Rc`) or an owning collection's element — target is on the heap owned by `owner` | tie to `owner`'s provenance; dangling iff `owner` is a frame-local that DROPS (not leaked/moved/param) |
| `Unknown` | can't classify | conservative-SAFE (today's default; keep, to avoid false positives) |

Two cross-cutting facts make this tractable:

- **The Deref discriminator.** `&c.x` (FrameDirect) is `AddrOfTemp(FieldRead*(VarRef c))`
  with NO `Deref`. `&*box` / through-pointer is `AddrOfTemp(…Deref(VarRef box)…)`.
  Walking the inner chain and checking for a `Deref` separates frame-direct from
  through-heap **structurally** — this is what makes the box_leak case (a `Deref`)
  excludable from the FrameDirect rule.
- **Consume/leak awareness.** The checker already tracks moves (`moved_vars_`).
  `Box::leak` works because it CONSUMES the box (`into_raw` moves it out → its Drop
  never runs). So "FrameDirect/Heap of a local that is **moved-out before the
  return**" is NOT dangling — the data outlived the local. Gate the dangling
  verdict on "the owner is still live (un-moved) at the return".

## 4. Per-front design

### Front (c) — return-of-local field/index chain  [LOWEST RISK, do first]
In `prov_of`'s `AddrOfTemp` case: walk the inner chain (`FieldRead` / `TupleIndex`
/ `IndexRead` receivers) to the terminal. If it terminates at a `VarRef` that is a
value local (`states_.count(name) && !param_names_.count(name)`) **and the chain
contains NO `Deref` and NO `MethodCall`** → `FrameDirect` (is_local). The
NO-`Deref` rule excludes `&*box`; the value-local + no-method rule excludes
through-method heap refs. `&c.x`, `&c.t.0`, `&c.arr[i]` (frame chains) → flagged;
`&*box`, `&param.x`, `&GLOBAL.x` → not. (Gate on the FULL suite; the 5064 blowup
came from touching `prov_of(VarRef)` globally — this stays inside `AddrOfTemp`.)
Refinement: skip the verdict if the root var is `moved_vars_`-moved at that point
(consumed/leaked → not dangling).

### Front (a) — method-result reference holds the receiver
Needs a per-method predicate **`result_borrows_self(method)`** — true when the
method's return lifetime elides to / is constrained by `&self` (Rust elision: a
`&self`/`&mut self` method whose output lifetime is unannotated, or annotated to
the receiver's). Source: the resolved method's signature (via
`EMethodCallView::resolved_symbol`/`resolved_type` → the function's params + return
lifetime). When true, `take_ref_borrows(MethodCall)` records a borrow of the
receiver root (kind = result mutability) with the binding as holder; NLL releases
it. When false (returns a global / an arg ref) → do NOT borrow the receiver (this
is what persistent_showcase needs). Conservative fallback when the predicate is
unknown: **do not borrow** (under-approximate — avoid the false positive; we lose
some real catches but never break valid code).

### Front (b) — provenance/exclusivity through a reference + interior-mut returns
Two parts. (i) Exclusivity through a `&mut` reference root — **DONE (f9e4efa3).**
B93.2 had skipped ALL tracking for `&mut`/raw-ptr roots (anti-false-positive for
nested `c.v.set(c.v.get()+1)`); that overshot into a real UAF (aliased `&mut`+`&`
through a ref). Fix: split the root kind in `visit(AddrOfTemp)` — RAW pointers stay
unchecked (Rust parity), but REFERENCE roots run the conflict checks (mut-binding
check still skipped). Closes `let r=&*a; a.v=5; use r`. (ii) `return a` where
`a = h.array()` and h is local: **DONE** — falls out of Front (a)+(c).

### Collection iterator-invalidation (`let r=&v[i]; v.push(); use r`) — DONE (849b1a91)
The classic Vec borrow error (verified UAF: valgrind "Invalid read" after realloc)
is now caught. Three pieces, all in the borrow checker (path 2 chosen):
1. **resolve operator/trait calls.** `&v[i]` → `&*Vec::index(&v,i)`; the index call
   has an EMPTY `resolved_symbol` (operator desugar) and lives in `prog_.impls`.
   Fix: index `prog_.impls` methods into the map, and fall back from
   `resolved_symbol` to the unmangled method NAME (every same-named method must be
   self-borrowing — Index/Deref contract; conservative, any disagreement → false).
2. **reborrow-of-method-result routing.** `&*(MethodCall)` in `take_ref_borrows`
   routes to the MethodCall so Front (a) records the receiver borrow.
3. **bare-place receiver conflict check.** `v.push()` lowers its `&mut self`
   receiver as a VarRef (generic/stdlib methods) — the AddrOfTemp path doesn't see
   it. New `method_self_kind` + `check_recv_conflict` run the whole-root conflict
   check for VarRef/place receivers. (Discovered by instrumentation: `recvKind=4`
   VarRef for `push`, vs `12` AddrOfTemp for a user method.)
Catches direct Vec/collection indexing + user `Index` impls. Test
`vec_iterator_invalidation`. **Known remaining (rare):** the through-`&mut`-ref
variant `let a=&mut v; let r=&(*a)[i]; a.push()`. ROOT CAUSE (instrumented to the
mechanism): the index call's receiver `&(*a)` is hoisted by `materialize_recv_ref`
into a statement-scoped temp `__rtmp_N = &(*a)`; `extract_borrow_place(__rtmp)`
returns an EMPTY root (temps aren't tracked places), so Front-a records no borrow
on `a` — and even if it did, the temp drops at statement end while `r` (which
reborrows through it) lives longer, so the borrow must tie to `r`, not the temp.
There is no `__rtmp`→source map. Fixing needs EITHER (1) temp-provenance threading
(resolve `__rtmp` to its source place + tie the borrow to the holder), OR (2) sema
suppressing receiver materialization when the receiver is a reborrow of a tracked
place. Both are a dedicated piece; the shape is contrived AND not container-
critical (Hermes2 arrays expose elements by VALUE — ZonedAny — not by `&`).
(Note: changing Front-a recording to fire on `&mut`-ref roots — root_is_rawptr=
Ptr-only — does NOT help here, the root is empty not a ref-root; it only added risk
and was reverted.)

## 5. False-positive traps (the things that bit us — must stay green)
- `Box::leak(b: Box<T>) -> &T` — `&*b`: a `Deref` path → NOT FrameDirect; and `b`
  is consumed → not dangling. Both guards protect it.
- `fn first<'a>(v: &'a Vec<T>) -> &'a T { &v[0] }` — `v` is a ref PARAM → `Param`,
  not FrameDirect.
- `fn pick<'a>(&self, x: &'a T) -> &'a T { x }` — method result borrows the ARG,
  not self → Front (a)'s `result_borrows_self` must be FALSE here (don't borrow
  the receiver).
- Iterators (`it.next()`), `vec.get(i)` returning heap refs from a value-local —
  these ARE dangling if the local drops while the ref is used/returned; flagging
  them on RETURN-of-frame-local is correct, but mid-scope NLL must release.

## 6. Implementation plan (each step gated on the FULL suite; revert on any
unresolved false positive)
1. **Front (c)** narrow (AddrOfTemp frame-chain, no-Deref, value-local root,
   un-moved). Smallest, most isolated. Add a fail-test + verify 0 regressions.
2. **`result_borrows_self` predicate** — build the per-method elision lookup from
   resolved signatures; unit-probe it on `get_ref`/`pick`/global-returning methods.
3. **Front (a)** using the predicate (conservative-false fallback). Gate.
4. **Front (b.ii)** — verify `return a` (a from h.array()) now caught via (a)+(c).
5. Decide on (b.i) through-`&mut` exclusivity separately (relaxation trade-off).

DISCIPLINE: instrument first (print each newly-flagged fn + its borrow path) on the
first suite run of each step — the 5064 blowup was only understood via
instrumentation; predictions (mine and a sub-agent's) were wrong. Trust the suite,
not reasoning, about blast radius.

## 7. What this buys the container
With (c)+(a): `return &local.field`, `return value_local.method_ref()`, and
`return a` (a from `h.array()`, h local) are caught — the container's
"a reference may not outlive its container" becomes BC-PROVEN, not just
disciplined. (b.i) would add exclusivity through `&mut` refs; deferred. The
residency `holder` (Rc<dyn Resident>) already gives runtime liveness; this adds the
static proof for the non-escaping (RC-elided) path.
