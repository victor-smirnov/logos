# 2026-09-14a-ptrcoerce — TARGETS, written before any compiler edit

## Target rows (bc_admits.ledger), by name
- type-check-pointer-coercions   nllmoves.R14
- borrowck-loan-vec-content      bck.B

## How the never-priced set was derived
Every root id in bc_admits.ledger, each row's FIXTURE NAME searched (word-bounded) in
every `## ` record of src/compiler/PROBES.md that carries a `site:` line. A root id
alone is not a pricing: the 13c/13e survey blocks list every root. Rows whose
fixture name appears in NO site record (build 6d1bbee8344b2a81):
bck.A-FNMUT(pins) bck.B(both rows) bck.D(2 of 3) bck.NEW-CAPMOVE(excluded)
bck.NEW-L(both) bck.NEW-A16 --t13(A16) bck.NEW-CESC issue-95079 lifereg.C
regions-escape-method lifereg.L1 trait-method-return-lifetime-mismatch
lifereg.NEW-4 19552 lifereg.NEW-E0226 lifereg.NEW-N2 lifereg.NEW-N3
lifereg.NEW-R19 glb-free-free nllmoves.B(both) nllmoves.D(all, excluded)
nllmoves.E(both) nllmoves.NEW-2(both) nllmoves.NEW-4 nllmoves.NEW-CESC 42574 pair
nllmoves.NEW-N2 nllmoves.R14 nllmoves.R2.
⚠ Narrative blocks without `site:` did discuss some (NEW-2 --c in the 09-01d ltbnd
round; bck.B / nllmoves.B in the 08-2x class-B survey as B-13 / X-2, "not probed").

## Why these two — an ARM THAT EXISTS reached through a fact the site does not carry
One-variable controls on build 6d1bbee8344b2a81 (legal ones linked and run):
R14  `*const &'a -> *const &'b` return     REFUSED  variance mismatch
     `*mut &'a -> *mut &'b` let            REFUSED
     `& &'a -> *const &'b` return (row)    ADMITTED
     `&mut &'a -> *mut &'b` let            ADMITTED
     `*mut &'a -> *const &'b` return       ADMITTED
  subtype() (include/logos/compiler/subtype.hpp) returns true on ANY kind mismatch
  except the fn-value kinds (the 2026-09-08 FnItem->FnPtr carve-out), and its Ptr arm
  returns true when mut_ptr differs. types_compatible (sema.cpp) accepts
  `&T/&mut T -> *const T/*mut T` and `*mut T -> *const T`. The Ptr/Ref arms exist.
bck.B  `let e = &a[0]; a[1] = 4; *e` (array)   REFUSED
       closure `|| a[1] = 4` beside `&a[0]` arg REFUSED
       `v[1] += 4` / `v[0].a = 4` / `v.push` (Vec) REFUSED
       `let e = &v[0]; v[1] = 4; *e` (Vec)      ADMITTED
       closure `|| v[1] = 4` beside `&v[0]` (row) ADMITTED
  try_index_mut_assign lowers a Vec store to DerefWrite(MethodCall index_mut,
  receiver = AddrOf(v) typed &mut) when no 2-param `__index_mut` candidate is found;
  the receiver conflict is keyed on method_self_kind, which cannot resolve the
  desugared index_mut (borrow_check.cpp reborrow_force_mut_ comment says so).

## Not taken, with the reason
- nllmoves.B capture-ref-in-struct--t08: a struct literal, tuple ctor, enum ctor AND
  a `mk(&mut p, &y) -> S<'a,'b>` call all ADMIT; only `put(&mut p,&y)` whose BODY
  writes refuses. No arm relates regions across fields/args without a write: the
  holder-deposit plane, excluded.
- nllmoves.NEW-2: the ltbnd arm exists; --c needs substitution + region graph (priced
  2026-09-01d, strict name spelling condemned); --b needs projection outlives.
- nllmoves.R2: `x == y` operands — a different site (binary operator), no arm seen.
- lifereg.NEW-E0226: a new arity rule, no arm (13e).
- nllmoves.NEW-4: `DoubleCell<_>` inference; explicit twin already refuses.
