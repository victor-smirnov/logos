# Core Port — Logos Divergences from Rust

> **Canonical, language-wide register: [`docs/DIVERGENCES.md`](../../docs/DIVERGENCES.md).**
> That doc defines the rule (only explicitly-replaced / deliberate-design /
> Logos-addition deltas are *blessed*; everything else is a catch-up TODO
> marked "не откладывать", never "deferred indefinitely"). This file is the
> coretest/alloc/std per-module ledger; keep its architectural rows in sync
> with §A there. The Custom-DST and `Box<?Sized>` rows are reclassified there
> as catch-up TODOs (§B2/§B3) — no longer "deferred".

This file lists every place where the Logos port of Rust core/alloc/std semantically deviates from the upstream. Each entry has:

- **Location** — package + symbol where divergence appears (or "global" for cross-cutting decisions)
- **Upstream** — what Rust does
- **Logos** — what we do instead
- **Reason** — why we diverge (semantic incompatibility, deferred feature, design difference)
- **Status** — `accepted` (intentional, won't change) / `deferred` (intend to converge later) / `under-review`

When this file is empty, no divergences have been recorded — first divergence lands with the first ported module.

---

## Architectural divergences (apply across many files)

| Location | Upstream | Logos | Reason | Status |
|---|---|---|---|---|
| global / `const fn` | Compile-time evaluation via const-fn | Compile-time evaluation via `metacall { ... }` | Logos has no const-eval; metacall is the compile-time computation channel | accepted |
| global / async-await | `async fn`, `.await`, `Future`, `Pin` | Native fibers + reactor at runtime level | Async machinery deferred; green stacks at language level | deferred |
| global / `macro_rules!` | Declarative pattern-based macros | `fn`-macros over `metacall` (or compiler-internal where appropriate) | Logos metaprog is the expressive macro layer | accepted |
| global / `#[stable]`/`#[unstable]`/`#[rustc_const_stable]` | Per-API stability tracking | Stripped on import | Logos has no stability tracking concept (yet) | accepted |
| global / `#[lang = "..."]` | Connects type/trait to compiler internals | Distilled — replaced by Logos-side mechanism (`is_anyval`, `#[writ_eidos]`, etc.) or fixed by adding to compiler | rustc-internal; equivalent functionality exists | accepted |
| global / `#[rustc_*]` family | rustc-internal hints | Stripped on import unless functional equivalent exists | rustc-internal | accepted |
| global / `#[diagnostic::*]` | Custom trait diagnostics | Deferred — diagnostics handled separately, not at port time | Diagnostic divergence does not block port | deferred |
| global / `Box<T: ?Sized>` | Heap allocation of unsized values | Not initially supported (Phase 1 ships only `?Sized` references; `Box<?Sized>` deferred to Phase 1.5) | Custom-DST + sized-via-Box layout requires extra codegen work | deferred |
| global / Custom DST tail-slice | `struct Foo { hdr: H, tail: [T] }` | Not supported in Phase 1 | Layout/alloc complexity; only `alloc`-tier types need it | deferred |
| global / `:tt` fragment in macros | "Any token tree" macro fragment | Not supported — users must specify a concrete fragment | `:tt` is "I didn't decide" — Logos requires explicit fragment choice | accepted |

---

## Per-module divergences

Recorded as modules land. Format:

### `std.<package>`

| Symbol | Upstream | Logos | Reason | Status |
|---|---|---|---|---|
| _(no divergences yet — first entry lands with the first ported module)_ |

---

## Process

When porting a file, if any of these apply, **add an entry first, then port**:

1. The Rust code requires a language feature we lack → gap, close-in-language or record as deferred + work around in port.
2. The Rust code requires a stdlib type/trait we lack → port that first, recursively.
3. The Rust code is rustc-internal (lang items, rustc-attrs, intrinsics) → distill: figure out what semantic role it plays, replace with Logos equivalent (often a compiler-side fix), strip the marker.
4. The Rust code has a Logos-incompatible design choice (e.g. `Box<dyn Error>` with custom-DST internals) → record divergence, adapt the API surface to match what Logos can do, document the gap.

**Anti-pattern**: silently changing semantics to make the port compile. If you adjust behavior, document it here in the same batch.

---

## See also

- [stdlib/imported/RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md) — per-file provenance manifest
- [docs/core-port/package-mapping.md](../../docs/core-port/package-mapping.md) — current → target package mapping
- [docs/core-port/shim-policy.md](../../docs/core-port/shim-policy.md) — re-export shim conventions
- [~/.claude/plans/core-port-roadmap.md](../../../.claude/plans/core-port-roadmap.md) — full roadmap
