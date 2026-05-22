# B131 — UI-surfaced gaps (structs + functions run-pass)

Batch B131 imported 22 run-pass tests from `tests/ui/{structs,structs-enums,fn,functions-closures}`.
All 22 compile + link + exit 0. Two NEW gaps surfaced, both §B catch-up (no new §A).
Suffixes: `-st2` (structs, placed in `pass/struct/`), `-fn2` (functions, placed in `pass/functions-closures/`).

## NEW gaps

### G131-1 — match on a GENERIC-enum-typed STRUCT FIELD SIGSEGVs
A struct field whose type is a monomorphized generic enum, matched via the
struct-field projection, crashes at runtime (SIGSEGV), even when the field is
first let-pinned to a local.

Minimal repro (SIGSEGV, exit 139):
```
enum Opt<T> { ONone, OSome(T) }
struct Request { foo: Opt<u64> }
fn main() -> i32 {
    let a = Request { foo: Opt::ONone };
    match a.foo {                      // also crashes via `let f = a.foo; match f {…}`
        Opt::ONone => { return 0i32; }
        Opt::OSome(_) => { return 2i32; }
    }
}
```

Bisection (all three independently verified):
- standalone generic-enum match (`let f: Opt<u64> = Opt::ONone; match f {…}`) → **works**
- NON-generic enum as a struct field (`enum Opt{…}; struct R{foo:Opt}; match a.foo {…}`) → **works**
- GENERIC enum as a struct field, matched → **SIGSEGV** (the conjunction)

So the gap is reaching a monomorphized generic-enum value through a struct-field
projection: the layout/pointer convention used for the field-stored generic enum
disagrees with what gen_match expects (likely the two-level enum heap convention
not being applied when the enum comes out of a struct field rather than a local
binding / standalone construction). Distinct from G126-1 (doubly-nested struct
literal under enum ctor) and from the known "match over &Enum no-deref" — this
crashes by-value, no reference involved.

Dropped candidate: `se_struct_in_enum` (distilled from
tests/ui/structs-enums/codegen-tag-static-padding.rs) — exactly this shape
(`struct Request { foo: TestOption<u64>, bar: u8 }` matched out). Removed.

### G131-2 — chained postfix `arr[i](args)` is a parse error
An array index immediately followed by a call expression fails to parse:
```
let table: [fn(i64)->i64; 3] = [inc, dec, sq];
if table[0](10i64) != 11i64 { … }   // error: syntax error near ']'
```
Workaround (used in `fnptr-in-array-fn2`): let-pin the indexed element first
(`let f0 = table[0]; f0(10i64)`) — the array-of-fn-pointers storage + index +
indirect call all work; only the chained `index-then-call` postfix sequence is
rejected by the grammar. (Sibling of the known if-as-value `…[0]` postfix-index
gap G126-4, but on the parser side rather than mlir-gen.)

## Re-confirmed known-open (NOT re-reported; source dropped or facet trimmed)

- FRU with a field-shadow where the FRU base supplies a field that the outer
  literal overrides AND that field holds a moved value
  (`S { f0: new, ..S { f0: moved_var, f1: 23 } }`) → spurious "use of moved
  variable" (move-checker counts the about-to-be-superseded base field as a
  live consume). Dropped struct-order-of-eval-1 because of this; the plain
  Copy-field FRU (struct-update-syntax-2463) is already in the corpus.
- Unit-like struct bare form `struct Foo;` is not Logos syntax (Logos uses
  `struct Foo {}`); converting changes the match-as-unit-value shape, so
  unit-like-struct was dropped (overlaps existing empty-struct-braces / empty
  tag tests anyway).

## Dropped as already-imported (dup check)
- tests/ui/structs/struct-update-syntax-2463.rs — already at
  `pass/structs/struct-update-syntax-2463.logos`.
- tests/ui/fn/nested-function-names-issue-8587.rs — already at
  `pass/fn/nested-function-names-issue-8587.logos`.
