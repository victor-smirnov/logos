# B165 — UI run-pass import: surfaced gaps & notes

Batch **B165** imported **44 NEW DISTINCT run-pass tests** (target 40+), all verified
to compile + link + exit 0 against the as-is `build/bin/logosc` (no compiler /
stdlib / grammar changes, no rebuild). Upstream pin:
`rust-lang/rust@4b0c9d76ae7d387229caea55cfa73c280b08b8a7`, date 2026-05-24.

Link command used (matches B149–B164):
`clang test.o $(ls build/lib/logos/liblogos-*.a) -Wl,--gc-sections -lm`.

## Areas covered (44 tests, one per area unless noted)

- result: `?` operator chaining two fallible steps, short-circuit on first Err (result-question-chain)
- option: `?` operator on Option, short-circuit to None (option-question-mark)
- iterators: for-by-ref array iteration deref+double (for-ref-array-doubled); indexed filter-square-sum + running max (indexed-filter-sum)
- closures: FnMut closure capturing a counter by mut-ref over a generic driver (closure-fnmut-counter)
- traits: default method delegating to required across two impls (trait-default-delegate); generic fn bounded by `A + B` calling a method from each (trait-multi-bound-call); generic fn dispatching a conversion method across two impls (trait-generic-dispatch-convert)
- generics: generic struct `Pair<T>` with `&T`-returning methods (generic-struct-ref-method); generic identity + two-type-param `pick_first` at distinct monomorphizations (generic-multi-mono-id)
- match: arms with guards partitioning i64 into ranges, first-match wins (match-guard-ordering)
- enum: Option<UserEnum> produced by a fn, matched, inner enum dispatched by value (enum-option-payload-byval); `Num(n @ 1..=5)` nested refutable payload pattern (enum-range-at-binding); enum with mixed payload variants + inherent match-on-self method (enum-payload-method-area)
- deref: user `Deref<i64>` whose target is reached via `*c` (deref-target-field)
- ops: user `Add<V2>` overload on a 2D vector struct (ops-add-overload-vec); user `Index<i64>` overload returning `&i64` (ops-index-overload)
- cmp: user `PartialEq` impl used via `==`/`!=` (cmp-partialeq-struct)
- intops: bitwise `& | ^ << >>` on u32 (intops-bitwise)
- cast: numeric `as` casts — i64->u8 trunc, int->f64 widen, f64->i64 trunc (cast-numeric-chain)
- slice: `&[i64]` parameter summed via index + len() (slice-param-sum)
- array-slice-vec: chained 2D place-write `grid[i][j]=v` (array-2d-place-write); index-write through `&mut [i64; N]` param (array-mut-ref-write)
- loops: `loop { break v }` expression (loop-break-value); labeled `break 'outer` from nested loops (labeled-break-nested)
- tuple: 2-tuple return + `let (q,r)` destructure (tuple-divmod-destructure)
- struct: nested struct field mutation `o.inner.v = v` (struct-nested-field-mut)
- let-else: `let Some(v) = … else { return }` past a divergent else (let-else-double)
- pattern: or-pattern `1 | 2 | 3` in a match arm (or-pattern-literal)
- dyn: dynamic dispatch through a `&dyn Trait` parameter, two impls (dyn-arg-dispatch)
- associated-consts: assoc const in an inherent impl, read in a method body + via `Type::CONST` (assoc-const-impl)
- consts: module consts in arithmetic + loop bounds (const-in-expr)
- recursion: mutual recursion is_even/is_odd (recursion-mutual)
- shadowed: successive type-preserving `let` shadow chain (shadow-rebind)
- numbers-arithmetic: i64 inherent methods `abs`, `pow` (int-methods)
- char: char classification methods is_alphabetic / is_numeric (char-classify)
- str: `str.len()` (i64) + byte index `s[i]` (u64) on ASCII literal (str-len-byte-index)
- refs: explicit `*p` deref of a tuple reference + `(*p).0`/`(*p).1` (ref-tuple-deref)
- functions-closures: higher-order fn taking a `fn(i64)->i64` pointer (higher-order-fnptr-apply)
- fn: function returning a `fn(i64)->i64` pointer from a non-capturing closure (fn-return-fnptr)
- methods: summing a field across an array of structs via indexed access (struct-array-field-sum)
- control: if / else-if / else chain selecting one of four buckets (control-else-if-chain)
- for-loop-while: `while let Some(n) = …` draining an Option countdown (while-let-countdown)
- binop: arithmetic precedence + short-circuit `&&`/`||` evaluation order (binop-precedence-bool)

## NEW gap surfaced (1)

### G165-1 — taking `&payload` of an enum-typed `match` binding and passing it to a `&Enum` param ⚠️ SILENT CRASH (mlir-gen failure), TRACTABLE

When a `match` arm binds an enum-typed payload by value (e.g. `Option::Some(c)`
where the payload is a USER enum `Color`) and the body then takes a reference
`&c` and passes it to a function expecting `&Color`, MLIR generation fails
(`logosc: MLIR generation failed`). Minimal repro:

```logos
package repro;
enum Color { Red, Green }
fn code(c: &Color) -> i64 {
    match c { Color::Red => { return 1i64; } Color::Green => { return 2i64; } }
}
fn main() -> i32 {
    let o: Option<Color> = Option::Some(Color::Green);
    match o {
        Option::Some(c) => { return code(&c) as i32; }   // &c of enum-typed binding => MLIR gen failure
        Option::None => { return 9i32; }
    }
}
```

What WORKS (so the gap is narrow):
- consuming the payload BY VALUE `code(c)` (param `c: Color`) compiles + runs.
- a payload-less / scalar binding is fine; the issue is specifically `&binding`
  where `binding` is an enum aggregate extracted from a payload.

Related but DISTINCT sema rejection (NOT this gap): `match &o { Option::Some(c) => code(c) }`
gives a clean sema error (`expected &Color, got Color`) — default-binding-mode
through Option<UserEnum> does not yield `&Color` for the inner binding here.

**Reshape applied:** enum-option-payload-byval-b165 consumes the extracted enum
payload BY VALUE (`fn code(c: Color)`), which preserves the tested semantics
(Option<UserEnum> produced by a fn, matched, inner enum dispatched). No test was
dropped wholesale.

## Author idiom mismatches (NOT compiler gaps; fixed in-place, all underlying features WORK)

1. Unit struct `struct Dog;` is a parse/sema reject (`'Dog' is not defined — did
   you mean 'struct Dog { ... }'`). Gave each unit struct a dummy `tag: i64`
   field in dyn-arg-dispatch-b165. `&dyn Trait` dispatch itself works fine.
2. `const N` in array-LENGTH type position (`[i64; N]`) is not accepted as-is
   (mismatch errors); matching the B164 idiom, const-in-expr-b165 uses a literal
   `[i64; 4]` length and exercises `const` in arithmetic + loop bounds instead.
3. f64 has no inherent `.sqrt()` method as-is (`receiver is not a struct (got
   f64)`). A candidate f64-methods test was DROPPED in favour of int-methods
   (`abs`/`pow` on i64, which DO exist) to keep the numeric-methods coverage.

## Summary

44 submitted, all green (EXIT=0). 1 NEW gap (G165-1, tractable). 1 test reshaped
(enum-option-payload-byval, by-value payload). 1 candidate dropped (f64 methods)
and replaced by int-methods. No silent miscompiles among submitted tests; no
crashes among submitted tests.
