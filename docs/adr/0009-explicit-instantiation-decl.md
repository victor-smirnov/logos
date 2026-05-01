# ADR 0009 — `instantiate` declaration for explicit pre-instantiation

Status: ✅ Implemented 2026-05-01 (L2). 984/984 ctest.

## Context

Current monomorphization scheme:

- **Free generic `fn`** is **lazy**: `enqueue_if_needed` collects instances via
  worklist starting from non-generic roots; `fn foo<T>` without a call is never
  instantiated.
- **Generic `struct`** is **eager-on-demand**: when any concrete `Foo<T>` is
  referenced (variable type, field type, `Foo::<T>::method` call), `clone_struct_def`
  clones **all** inherent and trait methods of the template under that
  substitution (modulo bound gate).

Compared to C++ and Rust:

| | C++ default | C++ `template class Foo<int>;` | Rust | Logos today |
|---|---|---|---|---|
| Body type-check at def | partial (two-phase) | partial | full (HM, against bounds) | full |
| Methods codegen'd per inst | only those used | **all** inherent | only reachable | **all** inherent + trait (eager) |
| Opt-in pre-instantiation | n/a | yes | n/a (linkonce_odr) | not declarable |

Two gaps motivate this ADR:

1. **No way to declare a root pin.** A library author building a generic
   container (e.g. `BTree<K, V>` in `persistent`) cannot say "produce a
   pre-built instance of `BTree<MyKey, MyVal>` in this object file". Today
   the answer is "write code that uses every method", which doesn't scale.
2. **Future-proofing for lazy collector.** L1 (separate ADR) will flip the
   default to lazy method codegen. At that point, `instantiate Foo<T>;`
   becomes the *real* opt-in for pre-instantiation: it must already exist
   as a syntactic surface, with the right semantics, before the flip.

## Decision

Introduce a top-level item:

```logos
instantiate Foo<T1, T2, ...>;
pub instantiate Foo<T1, T2, ...>;
```

Semantics (in the eager scheme — current state):

- Resolves the type expression `Foo<T1, ...>` to a concrete `LogosType`.
- Pushes an `LInstAnnotation` with `is_root_pin=true` and `struct_type` set.
- Mono picks it up via the existing `inst_annotations` loop in `mono.cpp` —
  `record_needed_struct` is called, the struct template is cloned, all its
  methods are eagerly emitted under the substitution.

Semantics (after L1 flip — lazy scheme):

- Same up to the demand. Additionally, each method of the template is
  enqueued as a worklist root for this instance; transitive closure pulls
  in everything those methods call. This is the C++ `template class Foo<int>;`
  analog: take the type as a root, codegen all inherent + trait methods,
  then let the collector chase callees.

`pub` semantics:

- Until separate codegen lands, identical to bare `instantiate` —
  `is_pub_reexport` is recorded but no behavior diverges.
- Once `.a`-artefact-based separate codegen exists (Mode B in the metacall
  plan), `pub instantiate` declares "this instance is part of the package's
  public API and codegen'd here; downstream packages should not duplicate".
  The downstream-side mechanism (linkonce_odr-equivalent) is out of scope
  for this ADR.

## Implementation

1. **Grammar** (`tools/peg_gen/grammars/logos.peg`):
   - `KW_INSTANTIATE = "instantiate"` keyword.
   - Field key `INSTANTIATE_DECL = 208`.
   - Productions `instantiate_decl` and `pub_instantiate_decl`, added to
     the `item` alternation.
2. **AST** (`include/logos/compiler/ast.hpp`): `INSTANTIATE_DECL` code 208.
3. **LIR** (`include/logos/compiler/lir.hpp`): `LInstAnnotation` extended
   with `is_root_pin` and `is_pub_reexport` flags.
4. **Sema** (`src/compiler/sema.cpp`): new case at top of item dispatch.
   Resolves the type, validates kind (struct / zoned struct / enum),
   pushes `LInstAnnotation` with mangled name + struct_type + flags.
5. **Mono**: no change required for L2. Existing `inst_annotations` loop
   in [mono.cpp:253-258](../../src/compiler/mono.cpp#L253-L258) already
   demands struct instantiation when `mangled_name` contains `$G`, which
   is true for any generic instance.

## Probe

`tests/logos/pass/instantiate_decl.logos` — both `instantiate Box<i64>;` and
`pub instantiate Box<i32>;` materialise the respective instances and all
their methods (verified via `nm` showing `Box$G1$i64__unused`,
`Box$G1$i32__unused` even though `unused` is never called).

`tests/logos/fail/instantiate_decl_unknown.logos` — error on unknown type.

## Non-goals

- Not a vehicle for `#[type_code=N]` binding. That stays on the existing
  `eidos Foo<T>;` / `struct Foo<T>;` no-body forms.
- Does not affect free `fn` — those remain lazy via the existing worklist.
- Does not enable cross-package pre-instantiation re-use yet (`pub` is a
  marker for future separate codegen).

## Status of related work

- L1 (lazy method codegen + dispatch-as-root) — **complete 2026-05-01**
  (L1.0–L1.6). Lazy is the default; `LOGOS_LAZY_METHODS=0` restores eager
  for bisection. Both modes: 984/984. With lazy default, `instantiate
  Foo<T>;` is the real C++ `template class Foo<int>;` analog — it pins
  every inherent + trait method of the instance as a worklist root.
- ADR 0008 (assoc-eq bounds) — landed; orthogonal.
- C++ `extern template`-equivalent for downstream skip-codegen — deferred
  until separate codegen lands.
