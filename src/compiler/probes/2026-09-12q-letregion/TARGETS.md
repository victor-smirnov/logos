# 2026-09-12q-letregion — TARGET ROWS, WRITTEN BEFORE THE COMPILER WAS TOUCHED

Base build `fc8539b37318b78a 43` (read with `scripts/build_hash.py`), HEAD `f0e571fe0`.
Subject ledger: `tests/logos/bc_admits.ledger` (# TOTAL 79 by listing).

## TARGET ROWS

    T1 method-ufcs-inherent-3   nllmoves.NEW-1     `let x: A<'a> = A::newa(&v, 22)`, impl<'a> A<'a>
    T2 method-ufcs-inherent-4   nllmoves.NEW-S7-1  `let x: A<'a> = make::<&'a i64>(&v, &v)`, free fn
    T3 adt-tuple-enums--t33     nllmoves.NEW-L1    `let e: SomeEnum<&'static i64> = SomeVariant::<&'static i64>(&c)`

All three re-verified ADMITTED (compile rc 0, run rc 0) on the base binary before any edit.

## WHY THIS BLOCK

Three different root labels, one hypothesised door: a WRITTEN region in a `let` annotation
(`'a` a fn lifetime parameter, or `'static`) receiving a value whose region is a LOCAL
borrow. The comparator is an ARM THAT EXISTS — `check_variance` at `let` — and it already
refuses the bare-reference `'static` form (`let x: &'static i64 = &v;` rc 1 today), so the
question is which fact the named-parameter and ADT-argument forms fail to carry.

NOT the `lifereg.B`/`NEW-B2` door plane (excluded by name). NOT `bck.NEW-A16`/`NEW-1`/`NEW-4`
(A16 owner-blocked), `argresvact`, `bck.D + nllmoves.D` (declined in file), `bck.NEW-CAPMOVE`.

## ONE-VARIABLE CONTROLS MEASURED BEFORE CHOOSING (base binary)

    e1  T1 with the impl binder alpha-renamed `impl<'q> A<'q>`        REFUSED "expected A<'a>, got A<'q>"
    e2  T1 with a FREE fn `newa<'q,T>` instead of the assoc fn        ADMITTED
    f1/f2  `let x: bool = newa(&v,22)` vs `= A::newa(&v,22)`          got `A<'_>` vs got `A<'q>`

⇒ T1 is admitted BY NAME COINCIDENCE (rule 12): `lower_static_call` returns the callee's
`fi.ret_type` unsubstituted, so the impl's `'a` and `foo`'s `'a` compare equal as strings.
Hypothesis: T1 = door C (static-call return region substitution) IN SERIES with door L
(let annotation's named region vs a local borrow). T2 = door L alone. T3 = door L at a
`'static` ADT argument — possibly the owner-walled `stland` half (PROBES.md 2026-09-02p §4).

## GROUPING TO TEST, NOT ASSUME

Does ONE candidate change at door L move T1 (with door C), T2 and T3?
