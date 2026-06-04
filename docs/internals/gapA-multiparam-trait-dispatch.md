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
