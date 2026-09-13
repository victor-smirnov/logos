# 2026-09-13e-declarrival — TARGETS, written before the compiler was touched

Base binary 4cf0bf5e5e07b0cf 43 (read), HEAD 7b21acc54, bc_admits # TOTAL 72, soundness_queue # TOTAL 117, queue gate rc 0.

## Rows, by id
| row | root | door |
|---|---|---|
| outlives-with-missing | lifereg.R17 (where half) | W: an UNDECLARED name as a where-clause SUBJECT is minted as a fresh type param (`fold_where_bounds` add-fallback) instead of reaching "unknown type" |
| constructor-lifetime-early-binding-error | lifereg.R17 (ctor half) | C: lifetime args in a TURBOFISH on an enum-variant constructor are never counted; the arity arm lives only at a TYPE reference (`resolve_type`) |
| trait-associated-constant | nllmoves.R18 | K: an impl's associated-const type is compared to the trait's with region-blind `types_equal`; the region comparator (`subtype`) is never asked there |

## Why this block
Survey by SET (records = `## ` blocks with `site:`; a root matches as `<block>.<suffix>`/`*.<suffix>`): these roots
appear in exactly ONE record — 2026-09-13c's own "zero-record" survey list — so nothing was ever priced on them.
All three are the shape that paid three landings running: an ARM THAT EXISTS reached through a fact the site does
not carry. None is the Self/impl-header plane, the 'static-demand plane, or the lifereg.B/NEW-B2 holder plane.
One-variable controls on 4cf0bf5e (multi-line programs, every legal one linked and RUN):
- W: `fn f(x: &T)` REFUSED "unknown type 'T'"; `fn f<U: Gen<T>>` REFUSED "unknown type 'T'"; `where U: Gen<T>` REFUSED;
  `where U: NoSuchTrait` REFUSED "unknown trait"; `where T: Tr` with T undeclared ADMITTED on a free fn, an inherent
  impl method, a trait-impl method, a trait method decl, an impl header, a struct. `f::<A, A>(&a)` against
  `fn f<U: Tr>(..) where T: Tr` COMPILES AND RUNS (rc 3): T became a SECOND TYPE PARAMETER — the fallback's own
  comment ("only a genuinely-undeclared type-PARAM name keeps the lenient add-fallback").
- C: `let e: E<'static> = ..` for `enum E<'a,'b>` REFUSED "'E': expected 2 lifetime arg(s), got 1"; `E::V::<'static>(&x)`
  and `E::V::<'static,'static,'static>(&x)` ADMITTED; `S::<'static,'static,'static> { .. }` ADMITTED; `E::N::<'static>`
  (unit variant) ADMITTED. `E::V::<i64,i64>(1)` (TYPE arity, `enum E<T>`) ADMITTED.
- K: const type `Option<i64>` vs trait `Option<&'b str>` REFUSED (by base name); `&'c str` vs `&'b str` ADMITTED;
  `Option<&'b str>` vs trait `Option<&'a str>` under `'a: 'b` ADMITTED; legal `&'static str` vs `&'b str` ADMITTED;
  legal `Option<&'a str>` vs `Option<&'b str>` under `'a: 'b` ADMITTED. Method-return `&'c str` vs trait `&'b str`
  ADMITTED too — the SIGARITY comparator site does not ask it either (neighbour by fact, different site).

## Not taken, and why
lifereg.L1 (method-call receiver region demand): the free-fn analogue refuses only at RETURN, not at the call — no
existing call-site arm identified. nllmoves.NEW-N2 (regions-normalize-in-where-clause-list): the struct-bound analogue
`fn bar<'a,'b>(p: P<'a,'b>)` over `struct P<'a: 'b,'b>` is LEGAL Rust (implied bounds), so there is no arm to reach.
lifereg.NEW-E0226: docs/spec/types.md says `+ 'lt` is "recorded but not yet enforced" — no arm exists.
lifereg.NEW-PROJBOUND: `AuthSession<U>` with unbounded `U` against `struct AuthSession<B: AuthnBackend>` is ADMITTED
too — signature-type WF is a wide class with an unpriced stdlib cost; not one row's arrival.

## Excluded by name
Self/impl-header plane; 'static-demand plane; lifereg.B/NEW-B2 holder-deposit plane; A16 rows (bck.NEW-1, bck.NEW-4,
bck.NEW-A16); argresvact; bck.D + nllmoves.D; bck.NEW-CAPMOVE — per the ledger's own notes and the prompt.
