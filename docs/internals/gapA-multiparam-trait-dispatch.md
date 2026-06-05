# Gap A — multi-param trait-method dispatch ignores the type-param

**Status:** OPEN (2026-06-04). Blocks `Sum<&T>` (and the by-ref iterator model).

## Symptom
`impl Sum<&i32> for i32` alongside `impl Sum<i32> for i32`, then
`v.iter().sum::<i32>()` (iter yields `&i32`) → mono picks the WRONG impl:
`'func.call' op operand type mismatch: expected i32, provided ptr` (or
`'i32__sum' does not reference a valid function`).

## Minimal repro (independent of iterators)
```logos
trait Conv<A> { fn conv(x: A) -> Self; }
impl Conv<i32>  for i64 { fn conv(x: i32)  -> i64 { return x as i64; } }
impl Conv<&i32> for i64 { fn conv(x: &i32) -> i64 { return (*x as i64) + 100i64; } }

fn doit<A, S: Conv<A>>(x: A) -> S { return S::conv(x); }   // INDIRECT dispatch

fn main() -> i32 {
    let a: i32 = 5i32;
    let r1: i64 = doit::<i32,  i64>(a);    // wants Conv<i32>  -> 5
    let r2: i64 = doit::<&i32, i64>(&a);   // wants Conv<&i32> -> 105  (FAILS: picks Conv<i32>)
    return (r1 + r2) as i32;               // want 110
}
```
DIRECT dispatch (`i64::conv(a)` vs `i64::conv(&a)`) WORKS — disambiguated by the
concrete argument. The gap is INDIRECT dispatch via a generic fn: `S::conv(x)`
where the impl must be selected by the trait type-param `A` bound from context
(`A=&i32`), not by a concrete arg.

## Root locus
mono's trait-method resolution selects the template by PREFIX match on
`<Self>__<method>__g__` and takes the FIRST hit (mono_clone.cpp ~3252-3263 for
the receiver/method path; the static `S::method` UFCS path is the one this repro
exercises — likely a sibling resolver, find via the `i64__conv` symbol). With two
impls (`i64__conv__g__i32`, `i64__conv__g__ref_i32`) both match the prefix → first
wins → wrong.

## Fix strategy
When >1 template matches `<Self>__<method>__g__`, disambiguate by the call's
ARGUMENT-type mangling (or the bound trait type-arg A): pick the candidate whose
`__g__<suffix>` matches the arg/trait-arg mangle. Bounded regression risk — only
the (already arbitrary) multi-match case changes. Single-match unaffected.

---

# Gap A′ — disambiguation by a bound trait-ARG (deeper; OPEN)

When the multi-param trait's method does NOT take `A` as a parameter, but `A`
is determined by a SECOND trait bound on a param (the `Sum<A>::sum<I:
Iterator<A>>(iter: I)` shape — `A` = the iterator's `Item`), the Gap-A
arg-type-string fix does not apply. The candidate symbols are TRAIT-QUALIFIED:
`i32__Sum$G1$i32__sum__g__I` vs `i32__Sum$G1$_i32__sum__g__I` (`_i32` = ref).
`A` lives in `$G1$<A>` and is NOT in the subst map (only `s[S]=i32`, `s[T]=i32`).

Minimal repro: docs/internals/gapA-prime-repro.logos (`Producer<Item>` +
`Collect<A>::collect<P: Producer<A>>` with `Collect<i32>`/`Collect<&i32>` for
i64; `run<A,P,S>` does `S::collect(p)`). Symptom: `'i64__collect' does not
reference a valid function`.

ROOT (confirmed): mono's trait-satisfaction registry is **trait-NAME-only** —
`concrete_impls_` is a `StrSet`, and `mono_has_impl_recursive` /
`method_bound_ok` check `"Producer"` without the trait-arg. So they cannot
tell `RefProd: Producer<&i32>` from `RefProd: Producer<i32>` → both candidate
bounds "pass" → no disambiguation. FIX = make trait satisfaction ARG-AWARE:
record/lookup `(trait, type) -> trait-args`, then either (a) pick the candidate
whose method-tparam bound `P: Trait<A>` the concrete arg satisfies WITH the
arg, or (b) resolve the arg's impl of the bound-trait, read its arg to get `A`,
and select the matching `$G1$<A>` candidate + drive its instantiation (the
inference block rebuilds a BARE `<Self>__<m>` base, so the trait-qualified base
must be threaded through). Substantial mono trait-engine work.

## Gap A′ — attempt + deeper wall (2026-06-04)

Tried: at the trait-static dispatch, find the trait-qualified candidates
`<Self>__<Trait>$G1$<A>__<m>` and pick the one whose `$G1$<A>` token the
ARGUMENT type implements — recovering "arg implements Trait<A>" from impl
symbol names (`<Arg>__<Trait>$G1$<A>__*`).

**Wall:** single-impl methods are mangled WITHOUT the trait qualifier
(`RefProd__produce__f__ref_RefProd`, not `RefProd__Producer$G1$_i32__produce`)
— the `$G1$<A>` qualifier is only added to DISAMBIGUATE multiple colliding
impls (the `tag_trait` path). So for the typical case (the iterator / producer
has ONE impl of the bound trait), the discriminating trait-arg (the iterator's
`Item` = `&i32`) is NOT in any symbol name. And `LFunction` carries no
impl-trait/trait-args field. So Gap A′ cannot be solved by string matching.

**What it actually needs:** ASSOCIATED-TYPE / arg-aware-trait-impl resolution
— "given arg type C and the bound trait T (from the candidate's stripped
method-tparam bound), what is C's `T::Item` (or the trait-arg of C's impl of
T)?" Then match the candidate whose `$G1$<A>` == mangle(that). This requires
either (a) an impl registry recording `(type, trait) -> trait-args / assoc
types`, or (b) resolving the arg's bound-trait method (`next`/`produce`) return
type and unwrapping it. Both are real mono trait-engine additions. The method-
tparam bound itself is ALSO stripped from the mono template (`type_params[P]`
has empty `bounds`), so even the bound trait name must be recovered (from the
source fn pre-mono, or threaded through). Substantial; deferred.
