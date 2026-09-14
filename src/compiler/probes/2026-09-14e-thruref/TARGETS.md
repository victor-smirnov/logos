# 2026-09-14e-thruref — TARGET ROWS, written before any compiler edit (build baa2a38e8b650fb3 43, read)

## bc_admits rows
- issue-51117 (bck.B) — `match bar { Option::Some(baz) => { bar.take(); baz.len() } }` over `bar: &mut Option<String>`.
- type-check-pointer-comparisons (nllmoves.R2) — `x == y` over `*mut &'a` / `*const &mut &'a` / `fn(&'c mut &'a)` operands.

## Why this block
Both roots have NO `site:` record naming their program (derived by SET: every ledger root x every `## ` record with a
`site:` line, matching the row's fixture names; 11 roots came back empty). Both are an ARM THAT EXISTS reached through a
fact the site does not carry, shown by one-variable controls on baa2a38e8b650fb3:
- bck.B: the pattern loan is recorded for a written `ref mut` (b02, b05), a struct default binding (b08), a tuple default
  binding (c01), a nested variant (c03) — and NOT for a depth-1 variant payload bound by the DEFAULT binding mode (b01 b03
  b04 b06 b07 c04 c06). `propagate_pat_borrows` excludes modes 3/4 BY MEASUREMENT (type_3 / type_8 list walk `cur = &**rest`
  refused when the loan is keyed on `cur.1`). That measured wall is a SECOND door in series: an assignment to a
  REFERENCE-typed local conflicts with a field loan recorded under it, although a reference has no fields of its own —
  LEGAL programs refused TODAY: t1 (explicit `match *cur { Cons(ref v, ref rest) }` walk), t2 (struct door), t3 (tuple
  door), m1 m2 m5 (`let a = &r.a; r = &s2;`). Door D1 = the mode filter, door D2 = the assign-site field-loan read.
- nllmoves.R2: the invariant comparison exists at a call (`same(x, y)`, r07) and a let (r03); `==`/`!=` asks
  `types_compatible` both ways, region-blind (r01 r05 admitted).

## Not taken, and why
bck.NEW-L buffer-reuse (loop + push; the assign-in-loop and push-without-loop shapes both refuse — the arm exists, but
12i measured the loop plane and its neighbour two-phase-across-loop is on the reservation plane) · lifereg.NEW-E0226 (no
arm: "recorded but not yet enforced") · lifereg.NEW-N2 / NEW-N3 / nllmoves.NEW-N2 (projection WF / HRTB: no arm) ·
nllmoves.E (drop-glue E0713 / tail temporaries) · nllmoves.NEW-4 (`_` hole unified twice) · nllmoves.NEW-2 (ltbnd
narrative records exist, 09-07b) · nllmoves.B (match-guards: implicit-loan PERMISSIVE by construction, recorded at the
guard site; capture-ref-in-struct: struct-literal meet plane, excluded).
