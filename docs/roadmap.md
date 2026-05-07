# Roadmap

Snapshot of where Logos is, where it's going, and how it gets there. Hand-updated, not a contract.

The canonical, machine-readable plan lives in `MEMORY.md` (out of tree). This page is a human-readable summary that points back to it.

## Where We Are (2026-05-07)

**Toolchain MVP reached.** `lforge` is functionally complete enough that external collaboration works: B0..B5 all shipped (multi-file projects, native C/asm sources, incremental + parallel build, transitive git deps, MVS conflict detection, lockfile, content-addressed build cache, `replace:`, `requires_logos:`). The first external `.logos` package — `github.com/victor-smirnov/lforge-hello-world` — is live and consumes via all three URL forms (bare / https / SCP-ssh) which canonicalise to the same display id and share the cache.

The compiler also picked up `--version`, an `if/while/for cond` no-struct-lit grammar fix, and several smaller bugs that fell out of writing lforge in Logos. Baseline 1013+ tests green (depending on suite).

**Strategic shift:** the toolchain hole is closed, and the next phase scales **wide** — Memoria, the persistent collections package, the metaprog phase 2 work, and the http-garden differential harness can each become their own GitHub project consumed via `lforge.hermes`, instead of living in-tree under `stdlib/` or `examples/`. See [internals/lforge.md](internals/lforge.md) for the live build-system state.

Logos develops in alternating multi-month cycles — feature phase, then refactoring phase, then feature again. The two phases are not run in parallel; refactoring is closed before feature work resumes, and vice versa. See [ADR 0006](adr/0006-lir-hermes-cutover.md) for the closing of the most recent refactor.

## Strategic Direction

Three threads converge on a single milestone: **Memoria, ported to Logos, used inside the Logos build system itself.** This is the focal point that organizes the next several phases.

### Metaprogramming in three phases (MP1 / MP2 / MP3)

The metaprogramming model is generative-first: a metaprogram is the primary thing, the user's code is data the metaprogram consumes to build the final program. The DF-graph of metafunctions runs to a fixed point; declarations existing before that cycle are immutable. See [internals/metaprog.md](internals/metaprog.md), [ADR 0003](adr/0003-metafunctions.md), and [ADR 0004](adr/0004-definition-centric-tu.md) for the model in depth.

**MP1 — targeted application + templates + type-level metaprog.**
- `template struct/enum/impl` keyword: declarations as data.
- `#[apply(metafn)]` at declaration sites; linear composition.
- Typed `quote_*!` forms (`quote_item!`, `quote_expr!`, `quote_stmt!`, …) with `#expr` antiquotation and `#(...)*` repetition.
- Type-level computation as ordinary Logos run at compile time.
- Coverage: roughly Rust's `macro_rules!` and proc-macros, but through the generative model rather than token rewriting.

**MP2 — build system `lforge` with MCP/LSP integration.**
- A new build system written in Logos, replacing make/ninja for production builds. **Toolchain MVP shipped 2026-05-07** — see [internals/lforge.md](internals/lforge.md). B0..B5 cover `build` / `run` / `update` / `clean` / `test` / `install` / `version` over multi-target projects, transitive git deps, MVS, lockfile, content-addressed cache, `replace:`, `requires_logos:`. Daemon mode + LSP/MCP integration are the remaining open pieces.
- Build graph aware of the MP1 DF-graph; incremental on Hermes content hashes. The B4 build cache is keyed on `logosc` mtime + sub-dep cache_keys, giving transitive invalidation on compiler upgrades and source edits.
- MCP server for AI authors as first-class users. LSP server for IDEs. A VS Code plugin is a candidate.
- Single integration surface: model and IDE talk to one service that knows build state, semantic state, and metaprog state simultaneously.

**MP3 — transformative phase on top of `logos`.**
- The transformative half of metaprogramming: AOP-style passes, BC, escape analysis, lints — all implemented as `Pass<Rewrites, Diagnostics>` plugins in `logos`.
- Identity table (`Map<U32, AnyVal>` reachable from the zone root) plus parent links for stable, compactification-surviving AST identity. See `feat_metaprog_phases.md` for the design.
- Datalog over a fact-base as the universal pointcut mechanism, shared by user passes, the borrow checker, and trait resolution.

### The build system as the new center of gravity

Until now the compiler was the central artifact. The strategic shift: **the build system `logos` becomes the center**, and the compiler is decomposed into services that plug into it. This is the named orchestrator the SOA-compiler vision (see [ADR 0004](adr/0004-definition-centric-tu.md), the broader argument in [ai-platform](ai-platform/README.md)) was always implying.

Why the build system is the right center:
- Incremental analysis, content-hash caching, dependency tracking — these are build-system primitives, and the metaprog DF-graph naturally lives at this layer.
- AI-via-MCP and IDE-via-LSP both want a single point of contact that holds the whole project state. The build system is the only candidate.
- SOA services without an orchestrator are a set of components without a host. `logos` is the host.

### Memoria as the only driving use case

Logos's metaprogramming is **out of distribution** — there is no comparable existing model to copy. C++ TMP, Rust `macro_rules!`/proc-macros, Zig `comptime`, Sutter's metaclasses (P0707), AspectJ, Lisp macros — each contributes a fragment, none gives the whole picture (generative + transformative + DF-graph + build-system-integrated). The existing precedents are mostly negative examples.

Because of that, design happens through practice, not on paper. Memoria is the practical task that drives MP1 (derives for schemas, indices, serialization, Hermes zones) and later MP3 (instrumentation for query planning, persistence boundaries, transactional wrapping). Logos and Memoria are co-developed, the way Hermes and Logos already are.

## Self-Hosting Plan

The compiler is written in C++ today and stays in C++ for the foreseeable future. Porting the compiler to Logos is gated on **two preconditions**:

1. The Logos language has stabilized — type system, the full metaprog model (MP1+MP2+MP3), the borrow checker, and stdlib APIs all settled enough that porting is not chasing a moving target.
2. Executable Logos compilers (or cross-compilers) are available for all major target platforms. Without that, "build from source" stops working for users on platforms where there is no pre-built Logos compiler — a distribution-level bootstrap problem distinct from the local-dev one.

When both hold, almost everything moves to Logos: frontend, sema, metaprog runtime, BC, stdlib, build system, Memoria. **Codegen stays in C++** — MLIR/LLVM bindings are a C++ API, and rewriting them in Logos is neither realistic nor useful. Codegen becomes one of the SOA services in `logos`. This is the right shape: Logos as a language for application code and data engines, not for systems-level work that C++ already covers natively.

### Two-stage bootstrap

To dodge the chicken-and-egg problem at build time:

- **Stage 1 (bootstrap):** cmake + ninja. Used to build `logos` (the build system + the compiler) from a clean machine. Stays around permanently — it is the only path that does not require a pre-existing Logos toolchain.
- **Stage 2 (`logos`):** every other build. Once `logos` is built, it handles stdlib, Memoria, user projects, future refactorings of the compiler itself.

Cmake/ninja are not improved or replaced — they stay as a minimal-surface service tool.

## Implemented

### Language Core
- Statically typed, compiled to native via LLVM/MLIR.
- `struct`, `enum` with payloads, traits, generics with monomorphization.
- Pattern matching, postfix `?`, function/method/operator overloading with strict resolution.
- Implicit safe integer widening (`u32 → i64`, etc.).
- Ownership and borrowing, flow-sensitive borrow checker with named lifetimes.
- `HashMap<K, V>` and infrastructure: `Hash`/`Eq` for primitive keys, multi-key shapes.
- Comprehensions in plain (`Vec<T>`, `HashMap<K, V>`) and Hermes (`@[...]`, `@{...}`) forms.

### Hermes
- Document, parser, stringifier, binary codec, cross-zone clone.
- `Map<K, V>`, `Array<T>`, typed arrays, `String`, `Decimal`.
- Trait-dispatched function registry (Stringify / Equal / Hash / Clone / Release).
- Capture (`$ident`, `${expr}`) inside `@{...}`/`@[...]`; `as<T>[...]` typed-array casts.
- View types: `HermesCtrView<'a>`, `HermesStatic` for rodata literals.
- Read/Write trait split. Zone migration to `Zone<Mutable>`.

### Generic Containers
- `Array<T>` and `Map<K, V>` migrated from concrete `ArrayI32`/`ArrayU64` (commit `6594bd4`).
- Blanket trait impls; `CloneElem` / `RelPtr` element-side traits.
- Partial specialization with deferred `type_code_of` and monomorphization-time dispatch emit.

### Metacall (early metaprog)
- Expression-position `metacall foo(...)` for primitive returns and `HermesStatic`. Mode A (JIT-compile L-IR) + Mode B (link symbol from `.a`).
- `Hermes` (mutable) return with auto-freeze via `__metacall_freeze` shim.
- Deduplication of `@`-literal blob globals via `unnamed_addr` and ConstantMerge.
- See `feat_metacall_arch.md` for the full model.

### Compiler IR refactor (just closed)
- Hermes adopted as the format for L-IR. `LExpr`/`LStmt`/`Pattern`/`HermesVal` are POD shells with a `mirror_offset_` into a Hermes document; `lir_view` is the typed accessor.
- Why it mattered: the bytes *are* the IR, which makes content-hash, cross-process exchange, and a future SOA split tractable.

### Tooling and Tests
- `logosc` driver, end-to-end build via CMake + VCPKG.
- `lforge` build system, toolchain-MVP complete — `build` / `run` / `update` / `clean` / `test` / `install` / `version` from a Hermes-SDN manifest, with multi-target projects, native C/asm sources, parallel per-file compile, mtime-based incremental, transitive git deps via clone cache, lockfile + MVS, content-addressed build cache, `replace:` overrides, `requires_logos:` ABI floor. Split across an entry point + 15 sub-packages (`tools/lforge/main.logos` + `tools/lforge/pkg/`). See [internals/lforge.md](internals/lforge.md) and [internals/package-manager.md](internals/package-manager.md).
- `logosc --version` / `-V` — single source of truth for the compiler version string.
- `logosc --diag-format=json` — NDJSON diagnostics + structured exit codes (`EXIT_USER_ERROR` / `EXIT_USAGE` / `EXIT_LINK_IO` / …) for programmatic consumption by lforge and IDEs.
- Stdlib gap-fill for build-system orchestration: `path::normalize`, `str_*` predicates + `Splitter`, `fs::walk_dir`/`mkdir_p`/`rm_rf`/`canonical`, `Child.stdout_lines() : Iterator<String>`, JSON parser, sha256 + sha256_hex, FS-CAS primitives.
- Grammar: no-struct-lit cond expression chain (`if a < b { ... }` parses without parentheses); UTF-8 codepoints in char literals.
- Test suite: 1013+ unit/integration/diag tests, plus 16 `lforge` shell-driven scenarios.
- HRPC, reactor (io_uring + green fibers), verification subsystems.

## In Progress (Current Feature Phase)

Toolchain hole is closed; the phase is now scaling **wide** — using the toolchain to build real things in their own repos, instead of in-tree. Direction is set by Memoria's needs and the MP1 plan. Consult `MEMORY.md` for the live picture.

- **Memoria-on-Logos as a first external project.** Driving use case for MP1 derives + the persistent collections package; consumed via `lforge.hermes` from a separate repository.
- **lforge cache prune.** GC for `~/.cache/lforge/build/` entries; the only piece of the original v1 list still pending.
- **Daemon mode + file watcher; LSP / MCP servers.** Single integration surface for AI and IDE clients on top of the existing build state.
- Foundations for MP1 (templates, `#[apply]`, typed quote, identity table groundwork).
- Continued shake-out of metacall slices: `Hermes`-return parity, blob dedup, capture roadmap (`feat_metacall_arch.md` §"Captures").
- Datatype × Storage × View Hermes refactor — GAT-style relations, `UnsizedPayload`, `Meta + Atom` for small objects.
- Decimal View — moving `to_f64`/`to_string_value` from `*const Decimal` onto a `DecimalView` carrying `base + ptr`.
- HTTP differential harness via http-garden continues to find RFC 7230 parser bugs.

## Near-Term

- **Auto traits** — Send/Sync-like markers, compiler-derived from field types. Required for the concurrency model.
- **Bitwise intrinsics** — `popcount`, `clz`, `ctz`, `bswap` via LLVM intrinsics. ~10× speedup expected for `TinyObjectMap` hot paths.
- **`pub const` end-to-end** — grammar, sema, import. Today blocked behind a `pub fn` accessor workaround.
- **Unicode in source** — currently parser is ASCII-only, comments included.
- **Multi-clause / nested-for comprehensions** — chained `for`/`if` clauses (Python's `[x for xs in mat for x in xs]`).
- **`Set<T>`** — until then, `Map<K, ()>` / `ObjectMap<K, null>` carries set semantics.
- **`if let`** — `match` with a wildcard arm is the workaround.
- **Range as value-type + `Iterator` trait + blanket `for` over `IntoIterator`** — today `for x in lo..hi` (and `..=`) is a fixed grammar form; range is not first-class and has no methods. Lift `lo..hi` to a value of `Range<T>`, define `Iterator`/`IntoIterator` traits in stdlib, rewrite `for` as desugaring to `IntoIterator::into_iter` + `Iterator::next` loop. Unlocks `(1..4).rev()`, `.map()`, `.filter()`, user-defined iterables.
- **Constraint solving via Z3** — clean isolated solver layer; for trait resolution and reward signals.

## Longer-Term

- **MP2 build system `lforge`** continued — daemon mode, file watcher, MCP and LSP servers, VS Code plugin candidate. (Toolchain MVP closed 2026-05-07; B6+ pending: cache prune, more `replace` forms, `requires_logos` inheritance, cross-compile, vendoring, signing.)
- **MP3 transformative phase** with `Pass<Rewrites, Diagnostics>`, identity table, parent links, Datalog fact-base.
- **Datalog / Rete engine in Logos** — native, on Hermes; for application-level queries (the compiler internally uses Souffle).
- **Coroutines / FSM optimization** — stackful fibers by default with implicit suspend; lower to FSMs via `llvm.coro.*` where escape analysis permits.
- **Module binary delivery format** — `libfoo.a = foo.o + foo.hermes` (interfaces + full template bodies + metadata).
- **Three-implementation Hermes** — Logos as reference, Rust derived, C++ as follower. Conformance tooling per language; no FFI; pass zone pointers.
- **Layer 2 schema sync and link-time collision detection** — deferred until the Logos Hermes API stabilizes.
- **Compiler ported to Logos**, gated on the two preconditions in [Self-Hosting Plan](#self-hosting-plan). Codegen stays C++.
- **Genos** — schema × API × algorithm as a structured inductive programming target.

## Known Bugs and Quirks

Tracked items, not surprises. Most have memory entries with reproducers.

- Associated-fn generic-impl monomorphization gap: `T::assoc_fn(...)` for a generic trait impl can fail; workaround via self-method with phantom argument.
- `Map<K, V>` concrete-impl dispatch: sibling concrete specializations can suppress each other's tag-dispatch registration; use a blanket `impl<V>` instead.
- Struct-array literal stride: `[Struct; N] = [...]` uses an 8-byte stride instead of `sizeof(T)`; workaround via raw `[i64; 2*N]`.
- Shadowed `let` + `==`: an inner `let X: i64` shadowing an outer match binding `X` causes `==` to compare an alloca pointer against an `i64`. Rename to work around.
- Struct field separator is `;` rather than `,`. Cosmetic; will likely change.

## Source of Truth

The canonical, machine-readable plan lives in `MEMORY.md` (out of tree at `~/.claude/projects/-home-victor-devel-logos/memory/MEMORY.md`) and `~/.claude/plans/validated-tinkering-spring.md`. The deprecated `openspec/`, the older `docs/datatypes.md`, and the in-tree `sandbox/` are not authoritative.

For the strategic context behind this roadmap:
- `project_mp_roadmap.md` — the MP1/MP2/MP3 split and the build-system pivot.
- `project_self_hosting.md` — the self-hosting plan, two-stage bootstrap, codegen exception.
- `project_dev_cadence.md` — the feature ↔ refactor rhythm.
- `feat_metaprog_inversion.md` — generative model, single syntax, metacall and template/quote as the only world boundaries.
- `feat_metaprog_phases.md` — Phase 1 vs Phase 2, identity table, parent links, the trade-off summary.
