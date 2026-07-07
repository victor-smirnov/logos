# ADR 0003: Metafunctions — Design Rationale

**Status:** Draft (design discussion, not implemented)
**Date:** 2026-04-26
**Context:** Evolution of the Logos metaprog architecture beyond Tier 0/1
(see `feat_metaprog_tiers` in auto-memory). This document records the
design decisions for the metafunction mechanism that we converged on
in discussion. It is a rationale document, not a specification.

## 0. Motivation: metaprogramming as offloading

Before the mechanism, the question worth answering is *why invest design
effort in advanced metaprogramming at all*. The honest answer is not
"because it is elegant" or "because Lisp had it". It is **offloading**.

Metaprogramming is the most general offloading mechanism a language can
offer: it lets a *program* generate the part of *another program* that
would otherwise have to be written, maintained, and reasoned about by
hand. Every concrete offloading axis the language eventually supports —
generated containers, derived trait impls, schema-bound serialization,
table-driven dispatch, generated FFI shims, generated tests — bottoms
out as a special case of "run a function at compile time, take its
output as code". A language with strong metaprogramming has *one*
mechanism for all of these. A language without it grows N ad-hoc
mechanisms, each with its own quirks.

This connects directly to Platform Goal 1 — *ease of offloading work
from the LLM to deterministic components* (see the AI-platform essays on
[logos-lang.dev](https://logos-lang.dev/blog/)).
Metafunctions are the in-language realization of that goal: the
deterministic component is *itself a Logos function*, with full type
checking, capability gating, and provenance — not an opaque external
generator wired in via build glue.

### 0.1 Why metaprogramming is underdeveloped in 2026

The state of the art in mainstream metaprogramming is poor: Rust proc
macros, C++ templates + constexpr, TH, Nim macros — each is either
opaque, capability-unsafe, or non-incremental. This is not because the
problem is unsolved in theory; it is because **there has been no
sustained practical demand** for solving it in earnest.

The cause is human-side: most working programmers struggle to handle
non-trivial *first-order* programming. Code that writes code is one
level of abstraction above that, and the population that can use it
productively has historically been small. Without a user base, the
investment in serious tooling — proper hygiene, capability systems,
incremental caching, IDE integration, debuggability — does not pay back.
Metaprogramming has remained a niche craft with niche tools.

There has been one notable exception in our experience: **Memoria**.
Building a generic, schema-driven, in-database programming substrate
forced metaprogramming to do real load-bearing work — generating
container variants, dispatch tables, serialization, layout logic — far
beyond what hand-written code could sustain. Memoria's pressure on the
existing C++ template machinery is one of the reasons Logos exists at
all. It was an existence proof that metaprogramming has real practical
value when the problem is structured to demand it.

### 0.2 Why models change the picture

Modern LLMs already exceed median human throughput on routine code by
one to three orders of magnitude. The factor that has historically
suppressed metaprogramming — *not enough humans able to use it
productively* — is being inverted by an agent class that is
qualitatively well-suited to it:

- Models hold and manipulate large symbolic structures (ASTs, type
  graphs, generated code) without the working-memory limits that keep
  humans on the surface of the program.
- Models do not "find" recursion or higher-order generation harder than
  flat code; the cognitive cliff humans hit at one or two levels of
  abstraction is not a feature of model dynamics.
- Models benefit *more* than humans from offloading: every chunk of
  generated code that comes out of a deterministic, type-checked
  metafunction is a chunk that does not have to come out of probabilistic
  sampling and does not need to be reviewed for hallucinations.
- Models are capable of *designing* and *evolving* metafunctions, not
  just calling them — turning the metaprogramming layer itself into a
  surface they can iterate on under the same reward signal as ordinary
  code.

The expected outcome: metaprogramming, which has been a niche tool with
a small ceiling for fifty years, becomes a primary working surface — for
the same structural reason that high-level languages displaced assembly
once compilers became reliable. The bottleneck moves, and the right
abstraction follows.

This is the real motivation for the rest of this ADR. The design
choices below — signature-as-contract, capability gating, content-
addressed caching, hygienic generation, explicit dep declarations —
look heavyweight if the user is a single human writing a single macro.
They are exactly right if the user is an AI agent generating and
composing dozens of them under continuous compiler feedback. We design
for the latter case.

## 1. Core idea

A metafunction is **an ordinary Logos function** that the compiler can
execute at compile time via JIT (`liblogos_jit.a`).

There is no separate "meta-language", no separate rules for metaprog code.
It uses:

- the same body language,
- the same type system,
- the same module system,
- the same trait system,
- the same `let`/`match`/`if`.

What differs is **who and when calls it** (the compiler, during sema/mono),
and **what its signature permits** (capability gating, see §4).

Principle: one universe of functions. The "meta vs runtime" category is
**a property of the call site, not of the declaration**.

## 2. Call sites

Two situations are distinguished:

### 2.1 Expression position — explicit `metacall`

```logos
let n = metacall optimal_size_for(typearg(MyType));
```

In a runtime context the same function could be called normally. `metacall`
is an explicit marker meaning "evaluate now in the compiler, splice the
result back into source as a constant".

What a metafunction return value becomes here:
- a number → numeric literal,
- a string → string literal,
- a Writ value of a schema the compiler recognises → the corresponding
  AST entity.

### 2.2 Declaration / type / constraint position — implicit metacall

```logos
struct Buffer<T, const N: usize = optimal_size_for(T)> { ... }

impl<T> Foo for T where has_method(T, "drop") { ... }

type Wrapper<T> = make_wrapper(T);
```

A runtime call in these positions is impossible by category. Therefore the
compiler **silently** executes the function at compile time, checking the
signature against the expected return category.

`metacall` is not needed here — it would mark something already unambiguous.
Principle: **position in the AST carries the mode information**, the keyword
need not duplicate it.

### 2.3 Reification

Types enter the meta-universe via `typearg(T)` (T → `TypeDef`). Symmetrically,
a metafunction returns `TypeDef` / `ClassDef` / `FnDef` and the compiler
materialises it back into the AST.

The "type world ↔ value world" boundary is crossed by two explicit operations,
not by implicit rules.

## 3. The signature is the contract

A metafunction's signature is the **complete contract** with the compiler:

- what the function may do (capabilities),
- what the result depends on (dep set),
- when it may be called (pipeline phase),
- what it returns (result category).

One mechanism, **four uses** of the same information. This is the central
architectural decision; everything else follows from it.

### 3.1 No ambient context

Everything a metafunction receives from the compiler is an **explicit
argument**. No `__current_class__`, no implicit access to the fact base.

If a metafunction needs context:

```logos
fn audit(ctx: ReflectCtx, c: ClassDef) -> Vec<Diagnostic> { ... }
```

`ReflectCtx` is passed in explicitly by the caller. In most cases the caller
is the compiler itself, when the function appears in a declaration position.

### 3.2 TypeDef and friends — public stdlib contract

`TypeDef`, `ClassDef`, `FnDef`, `MethodDef`, `Diagnostic`, etc. are ordinary
Writ structures defined in stdlib. The compiler recognises them by their
Writ `schema_type_code`, not by name.

This means:
- the structures are extensible and versionable like any Writ schemas,
- diff, hash, serialisation all work,
- no type-system bifurcation.

Writ as the unified IR is **load-bearing** here: the compiler's fact base
is itself a Writ structure.

## 4. Capabilities and admission control

Metafunctions execute in the compiler's address space. What they are allowed
to do is determined by capabilities passed as arguments.

Minimum set:

| Capability | Meaning | Default in meta-context |
|---|---|---|
| `Alloc` | heap allocation | allowed |
| `Panic` | may panic, caught → compile error | allowed |
| `ReflectCtx` | read existing types/functions | on demand (token arg) |
| `InjectCtx` | create new entities | on demand |
| `QueryCtx` | Datalog queries against the fact base | on demand |
| `IO` | files, network, syscalls | **forbidden** (determinism + isolation) |
| `Nondet` | time, RNG, threads | **forbidden** (cache determinism) |
| `FFI` | calls to extern functions | **forbidden**, no opt-in |

A "pure" metafunction has `effects ⊆ {Alloc, Panic}` plus whatever capability
tokens it was given.

`IO` and `Nondet` are forbidden **hard**, not by default: violating
determinism breaks caching and hermetic compilation. Anyone who wants those
writes a build script, not a metafunction.

`FFI` is forbidden: the compiler must not crash from foreign code, and
hermetic compilation requires that all read inputs are declared.

### 4.1 Inference vs annotation

Capability set:
- **inside a crate**: inferred from the body,
- **at the pub boundary**: annotation in the signature is required.

This trades off "comfortable to write" against "stable as a contract".

## 5. Dependencies and scheduling

The signature **is** the build-dependency declaration. The compiler builds
a **call-site-specific** dependency graph and topo-sorts it.

### 5.1 Base case

```logos
fn build_table(items: Array<EnumVariant>) -> TableDef
```

The compiler sees the call site, sees which variants are passed in (or read
out of which enum), and **does not call the function** until they are known.

### 5.2 `MetaCtx` categories ⇒ pipeline phases

Each ctx token is available only in a specific phase:

- `ReflectCtx` — after type stabilisation,
- `InjectCtx` — in the generation phase,
- `QueryCtx` — after the fact base is built,
- `ModuleCtx` (if it ever exists) — final phase, sink nodes.

A metafunction requiring `InjectCtx` cannot be called before the generation
phase. The compiler knows this from the signature, without reading the body.

### 5.3 Higher-order

```logos
fn map_methods<F: fn(MethodDef) -> MethodDef>(c: ClassDef, f: F) -> ClassDef
```

Dep set = own ∪ `F`'s. `F` is a parameter; its dep set is fixed at
monomorphisation time. Scheduling is deferred until the concrete call site.
Falls out naturally from monomorphisation.

### 5.4 Conditional reads — over-approximation

If a function reads one of two facts depending on a condition, the dep set
lists both. The topo sort is conservative. This is fine; a user who hits
an artificial cycle refactors into two functions plus a dispatcher. We do
not introduce dependent typing for this case.

## 6. Phase ordering / fixpoint

The cycle "AST → sema → mono → possible new types → sema again" works
because:

- The AST is monotonic: metaprograms **do not modify** existing entities,
  only add new ones.
- Each iteration adds something, otherwise we stop.
- An iteration cap catches pathologies.

This is enough for practical use. Lean-style on-demand with a proper cycle
trace is **a possible upgrade**, not a starting requirement: fixpoint is
simpler and the upgrade does not break the API.

The "metafunction asks the compiler 'does type X exist yet?'" case is
handled by **contract**: the function declares which types it expects,
and the compiler calls it only when they are present. This is a rare case;
deferred for later.

## 7. Hygiene

Returned AST fragments are **hygienic by default**. Names bind at the
metafunction's definition site, do not leak out. Block-as-value in Logos
gives this naturally.

Unhygienic mode (explicit injection into the call-site scope) is a separate
operation, on request: `inject_into(ctx, name)`. Same discipline: safe
default, crossing the boundary is explicit.

## 8. Provenance and incremental compilation

Each generated entity stores provenance:
- which metafunction created it,
- with what arguments,
- which facts it read from MetaCtx (read set),
- hash of the metafunction body at execution time.

Cache key for a meta call: `hash(M_body) ∪ hash(args) ∪ hash(values(read_set))`.
Invalidation is automatic. Salsa-style; Writ provides stable hashing.

The `logosc expand <file>` command is a **dev tool** that expands metafunctions
into source listings for reading and debugging. Not for builds. Without it,
metaprog-heavy code becomes black magic.

## 9. What we deliberately do NOT do

- **A separate const evaluator.** Any function satisfying the capability gate
  is executed by the JIT. `const fn` as a separate keyword goes away over time.
- **A `const VAL` keyword.** Module-level `let` is enough; mode is determined
  by position. See table:

  | | function-scope `let` | module-scope `let` |
  |---|---|---|
  | When evaluated | runtime | compile time, sema |
  | RHS effects | any | `⊆ {Alloc, Panic}` |
  | Mutability | `let mut` ok | forbidden |

- **A separate `meta fn` / `comptime fn` keyword.** The signature already
  carries all the information. If the return is `TypeDef`, the function is
  type-level. If it takes `InjectCtx`, the function is generative.
  No declaration-side marker is needed.
- **AST rewrite.** A metaprogram creates **new** entities, does not mutate
  existing ones. This is load-bearing for the fixpoint scheme (§6) and
  for provenance (§8).
- **Implicit `metactx`.** The "everything explicit" principle is relaxed only
  where the AST position alone determines the mode (declaration positions,
  §2.2).

## 10. Comparison with prior art

- **C++ `constexpr`/`consteval`** — declaration-side marker, requires a
  separate const-evaluation machinery. We have one machinery (the JIT).
- **C++ metaclasses (P0707, Sutter)** — close idea of "generative functions",
  but tied to a class declaration (`interface Shape { ... }`). For us, the
  same is expressed by an ordinary metafunction call in declaration position.
- **Zig comptime** — close philosophy (one language, context determines mode),
  but relies on first-class types in the runtime universe. For us types are
  first-class only in meta, via `TypeDef`/`typearg(T)` — simpler for the user.
- **Rust proc macros** — opaque, hard to cache, hard to incrementalise,
  ambient access. With everything explicit in the signature, cache,
  scheduling, and incrementality become tractable problems.
- **Template Haskell** — stage restrictions because of ambient access. Here
  stage is a function of capabilities, no separate rules.
- **Nim macros** — typed AST in/out, close in spirit. Provides no capability
  system and no hermetic guarantees.

## 11. Open questions

- **Disambiguating type-position calls vs generic application.** Currently
  `Bar<T>` is generic application, `Bar(T)` is a function call. If we ever
  want `Bar(T)` as sugar for application, conflict. Reserve.
- **IDE feedback for implicit metacall.** The user does not see that a
  function executes at compile time. Solved via LSP semantic tokens
  (visual cue), not via syntax.
- **SemVer of pub API in the presence of metaprog.** Changing a private
  function that someone else's metafunction reads via reflection breaks
  their build. A separate policy is needed — possibly a marker "this
  private entity is read by meta from another crate, beware of changes".
- **`metacall` in bounds with non-pure ctx.** Unresolved; for now bounds
  positions allow only pure-meta functions (no MetaCtx).

## 12. Relation to other decisions

- Relies on Writ as the unified IR (§3.2, §8).
- Relies on the JIT (§1, §4).
- Connected to the capability discussion for function effects/purity
  (same mechanism).
- Compatible with the Datalog resolver (§5.2 — `QueryCtx` is the token
  for Datalog queries).
- Replaces a mature `const fn` (§9).
