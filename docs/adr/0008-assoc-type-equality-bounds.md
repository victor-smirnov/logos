# ADR 0008 — Associated-type-equality bounds in impl headers

Status: Proposed 2026-05-01. Design only — no implementation in this revision.

## Context

Logos already supports associated types in traits:

```logos
trait Profile {
    type Order: OrderKind;
}
```

and trait bounds on type parameters:

```logos
impl<P: Profile> HasInsertKv for P { ... }
```

What is missing is the ability to bound by an *equality* between an
associated type and a concrete type — the form Rust writes as
`P: Profile<Order = ByKey>`. This is the load-bearing pattern for
profile-style libraries (Memoria, the persistent-collections package),
where method sets fan out per profile by intersecting traits with
associated-type values:

```logos
// Want: only profiles whose Order is ByKey expose insert_kv.
impl<P: Profile<Order = ByKey>> HasInsertKv for P {
    fn ikv(&self) -> i64 { ... }
}
```

Without this, the workaround is one of:
1. A separate marker trait `HasByKeyOrder` plus a manual impl per profile.
2. Encoding the discriminator as an extra bound, losing the link to
   `Profile::Order`.
3. Concrete impls per `(profile, op)` pair.

All three duplicate information already declared on the profile.

## Decision

Add `Trait<AssocName = ConcreteType, ...>` syntax to the bound position
of `where`-clauses and inline `<T: ...>` bound lists. The bound matches
a candidate type `P` when:

1. `P: Trait` holds (existing satisfaction check), and
2. The associated-type entry `Trait::AssocName` for `P` resolves to a
   type definitionally equal to `ConcreteType` after substitution.

Multiple equality clauses combine with `,` and behave as a conjunction:
`Trait<A = T1, B = T2>` requires both equalities. Equality clauses can
appear alongside ordinary trait bounds: `T: Profile<Order = ByKey> + Clone`.

Syntax sites:

- inline: `impl<P: Profile<Order = ByKey>> Foo for P { ... }`
- where: `where P: Profile<Order = ByKey>`
- function/method bounds: `fn f<P: Profile<Order = ByKey>>(...)`

## Implementation sketch

Three landing points:

1. **Grammar (`tools/peg_gen/grammars/logos.peg`).** Extend the
   trait-bound production to accept an optional `<` ASSOC_BINDING
   (`,` ASSOC_BINDING)* `>` after the trait name, where each
   `ASSOC_BINDING := IDENT EQ type`. Emit an `ASSOC_BINDS` slot on
   the `TRAIT_BOUND` node carrying a list of `(name, type)` pairs.

2. **Sema collect (`sema_collect.cpp`).** When recording a `TraitBound`,
   carry the assoc-binding list into the existing `TraitBound` struct
   (or its successor). Bound-satisfaction (`check_type_bounds`) gains
   a second pass: after confirming `P: Trait`, look up the impl's
   assoc entries via `assoc_type_impls_` and require equality with
   the bound's listed concrete types after subst.

3. **Blanket dispatch (`sema_expr.cpp`, `mono.cpp`).** Both the
   blanket-dispatch viable-match pass and the eager-instantiation
   loop in mono must filter candidates whose assoc-types do not
   satisfy the equality clause. The candidate set is already iterated
   per primary bound; equality clauses add an extra lookup.

No change to the type system itself: this is a constraint on which
candidates a bound matches, not a new kind of type. Codegen and
mono are unaffected once selection is correct.

## Probe

`/tmp/blanket_probes/p9_assoc_eq.logos` exercises the canonical case:
two profile structs (`MapProfile`, `ArrayProfile`), each binding
`type Order` to a different concrete type; a blanket impl gated on
`P: Profile<Order = ByKey>` exposing `ikv` only on `MapProfile`.

## Non-goals

- General associated-type *inequality* (`Order != ByKey`) is not in scope.
- Higher-kinded equality (e.g. `Trait<Item = Container<_>>` with a hole)
  is out of scope; the rhs must be a fully-resolved type expression.
- This ADR does not extend trait-object syntax; equality clauses are
  meaningful only in static-bound positions where monomorphization
  selects a candidate.

## Status of related work

- Auto traits (`feat_auto_traits.md`) — landed.
- Blanket impls full coverage (this branch) — landed: unbounded form,
  multi-bound, overlap detection, cosmetic ctx for diagnostics.
- Implementation of this ADR is queued for a separate session.
