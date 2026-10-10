# ADR 0035. Effects: inferred effect sets, named bounds, tiers as bounds

Status: PROPOSED for pair review (2026-10-10). Part of the set under [0032](0032-logos-core-composition.md). Takes over the effect vocabulary of [0003](0003-metafunctions.md) §4 (Draft, not implemented) and generalises it from metafunctions to every fn.

## Problem

Measured 2026-10-10:

- No fn effect exists in the compiler. `#[may_suspend]` / `#[no_suspend]` were a 2026-05 design decision and were never implemented; DIVERGENCES A4: every fn is implicitly suspendable (green fibers, no colour).
- The only inferred-and-checked fn property is `unsafe` (`IS_UNSAFE`, E0133 at `sema_impl.hpp:2832-2842`); it is local, not transitive.
- Tiers lang → mem → lcm → std are defined by what they may do (`module_manifest.cpp:50-62`: lang is "synthesizable, static memory", mem adds malloc, lcm adds virtualised IO, threads, fibers, std adds processes and OS IO) and enforced only by link order. `extern fn` bypasses them: the mem tier declares libc `open` / `write` / `getenv` (`oracle_rt.logos:14-15`, `dplan.logos:31`, `codegen.logos:158`, `el.logos:2297`, `dl_export.logos:45`).
- Deem UDFs are registered with name, return class and arity only (`reflect.logos:659`); an impure UDF is accepted silently, although incremental maintenance (ADR 0033) and plan reordering assume deterministic, side-effect-free functions.
- Compile-time evaluation is `metacall` only (`const fn` is a parse error, fafc0e3e9); the metacall JIT resolves any process symbol (`jit.cpp:163`).
- Consumers waiting for effects: Deem UDF purity, metafunction admission (ADR 0003), tiers, Hest vertex bounds and placement (ADR 0034 D7), the synthesizable subset, specification functions (ADR 0037), capabilities (ADR 0036).

## Decisions

### D1. Effects are inferred properties, not colours

Every fn `f` has an effect set `E(f) ⊆ V` (the vocabulary, D2). `E(f)` is the union of the effects of the operations in its body and of `E(g)` for every callee `g`, computed as a least fixpoint over the call graph (recursion included). Inside a package nothing is written in signatures. An effect changes no calling convention and no type: a fn with `Suspend` is called like any other (the fiber model stays, DIVERGENCES A4 unchanged).

### D2. Vocabulary

| Effect | Originates at | Notes |
|---|---|---|
| `Alloc` | heap allocation intrinsics | zone / arena allocation: see O3 |
| `Panic` | `panic!`, failed checked arithmetic, bounds checks, `unwrap` on `None` | the largest source; D7 |
| `Diverge` | a loop or recursion without a proven bound | conservative: any `loop`, `while`, recursive SCC, unless a `decreases` proof (ADR 0037) or a bounded `for` over a finite range |
| `Suspend` | fiber yield, blocking channel / IO ops | replaces the never-implemented `may_suspend` |
| `IO(k)` | operations on a capability of kind `k` (ADR 0036) | `k` ∈ fs, net, clock, env, proc, stdio, device, … |
| `Nondet` | time, entropy, thread scheduling, Hest `Merge`, iteration over unordered containers whose order leaks | |
| `Global` | read-write of a mutable `static` | |
| `FFI` | call of an `extern fn` without a declared set (D3) | |
| `Reflect`, `Inject`, `Query` | metaprog operations (ADR 0003) | compile-time only |

`unsafe` stays a separate, local property (Rust semantics); it is not an effect.

### D3. Where effects originate

- Intrinsics and lang items carry declared sets.
- An `extern fn` declares its set: `#[effects(io(fs), panic)] extern fn open(…)`. An undeclared `extern fn` has `{FFI} ∪ V` (top).
- Inline assembly is top.
- Capability methods (ADR 0036) carry `IO(k)` of their kind; nothing else can produce `IO(k)` (ADR 0036 D3).

### D4. Bounds at boundaries

- A BOUND `#[effects(<= {Alloc, Panic})]` on a fn is checked: `E(f) ⊆ bound`, else an error with the derivation chain (`f → g → h → write: IO(fs)`).
- `pub` fns record their inferred set in the package interface (`.pkgi`). Widening a recorded set is an interface change caught by `abi-check.sh` (the inferred set of a dependency cannot change silently under a caller). ADR 0003 §4.1 required an annotation at the pub boundary; recording plus the ABI gate gives the same stability without writing the set, and an explicit bound remains available.
- Trait methods may declare a bound; every impl must satisfy it; a `dyn` call or a call through a generic `T: Trait` assumes the bound (or top when undeclared).
- Generic fns: `E(f::<T>)` is computed per instantiation after mono (effects travel through mono, ADR 0003); a bound on a generic fn is checked per instantiation, with a pre-mono check where the bound holds for every instantiation the trait bounds admit.
- Closures and fn pointers: the closure's own body is inferred like a fn; a parameter `F: Fn(..)` may carry a bound (`F: Fn(..) + effects(<= B)`, syntax O1); without one, a call of `F` contributes top.

### D5. Named bounds

| Name | Bound | Used by |
|---|---|---|
| `pure` | `⊆ {Alloc, Panic}` | Deem UDFs, ADR 0033 operators, specification fns (ADR 0037) when also `total` |
| `total` | `∩ {Panic, Diverge} = ∅` | spec fns, Lean export (ADR 0038) |
| `metafn` | `⊆ {Alloc, Panic, Reflect, Inject, Query}` | metacall admission (ADR 0003) |
| `synth` | `⊆ {Panic}` (a panic is an error signal in hardware), plus type restrictions: fixed-width types, no heap types, no `dyn` | Hest hardware candidates, the lang tier |
| `tier_lang` | `⊆ {Panic, Diverge}` (static memory; `synth` additionally requires `Diverge` absent, which the conservative rule of D2 rejects for most loops today) | every fn of the lang tier |
| `tier_mem` | `⊆ {Alloc, Panic, Diverge}` | mem tier |
| `tier_lcm` | `tier_mem ∪ {Suspend, IO(virtual kinds), Nondet}` | lcm tier |
| `tier_std` | top minus `FFI` (FFI allowed only by grant, ADR 0036 D4) | std tier |

The tier bounds are checked in sema on every fn of a package of that tier, `extern fn` declarations included. The mem-tier libc calls of the census get declared sets or move up a tier.

### D6. Computation and diagnostics

The effect sets are a rule program over call-graph facts (ADR 0032 D6), evaluated by the ADR 0028 engine now, by Deem after the logosc rewrite. Every effect in `E(f)` has a first-derivation witness; the diagnostic for a violated bound prints the chain. Facts and results are exported (`--emit-facts`) so a model can query them.

### D7. `Panic` precision

`Panic` from checked arithmetic and bounds checks makes most fns non-`total`. Two mechanisms, both needed: (a) the SMT layer (ADR 0037) discharges the check (the index is in range, the sum does not overflow) and removes that origin; (b) wrapping / saturating operations and `get` → `Option` produce no `Panic`. No unchecked escape that silences `Panic` without a proof.

## Rows

| Row | Content | Acceptance |
|---|---|---|
| E0 | call-graph facts exported from sema / L-IR (post-mono, with indirect calls resolved where known) | facts gate: a planted call edge appears |
| E1 | inference of `Alloc`, `Panic`, `FFI`, `Global`, `Diverge` (conservative); `--emit-facts` output; derivation chains | fixtures per effect, chain diagnostics |
| E2 | `#[effects(...)]` on `extern fn`; the census of stdlib `extern fn` declared | top count in stdlib = 0, gate pins it |
| E3 | bounds (D4) on fns, trait methods, generic instantiations; `.pkgi` recording and abi-check | fail fixtures per boundary kind |
| E4 | tier bounds checked (D5) | the five mem-tier libc uses resolved; a planted `extern fn open` in the mem tier fails |
| E5 | Deem UDFs required `pure` | an impure UDF fails at its call site in the query, `file:line:col` |
| E6 | `Suspend`, `Nondet`, `IO(k)` (with ADR 0036 K1) | fixtures |
| E7 | `metafn` bound for metacall; JIT symbol filter from the bound | a metacall reaching `getenv` fails |

## Open

- O1. Syntax of a bound on a closure parameter and on fn pointer types.
- O2. Effect polymorphism: a higher-order fn whose effect is "whatever its argument does" (`map`). D4 handles it per instantiation after mono; a pre-mono effect variable (row polymorphism) is more precise for `dyn` and fn pointers. Recommendation: mono-time first, effect variables only if a measured need appears.
- O3. `Alloc` and zones: allocation into a caller-provided zone or arena is not a heap effect; is it a capability use (ADR 0036) or no effect at all.
- O4. `Diverge` precision without SMT: bounded `for` over ranges and iterators of declared finite size (`size` in source declarations) as the first non-SMT rule.
