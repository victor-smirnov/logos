# Roadmap

Snapshot of where Logos is, where it's going, and how. Hand-updated, not a contract. The canonical machine-readable plan lives in `MEMORY.md` (out of tree); this is a human-readable summary.

## Where We Are (2026-05-07)

**Toolchain MVP reached.** `lforge` is complete enough for external collaboration: B0..B5 shipped (multi-file projects, native C/asm sources, incremental + parallel build, transitive git deps, MVS conflict detection, lockfile, content-addressed build cache, `replace:`, `requires_logos:`). The first external `.logos` package — `github.com/victor-smirnov/lforge-hello-world` — is live and consumed via all three URL forms (bare / https / SCP-ssh), which canonicalise to one display id and share the cache. The compiler also gained `--version`, an `if/while/for cond` no-struct-lit grammar fix, and smaller bug fixes from writing lforge in Logos. Baseline 1013+ tests green.

**Strategic shift:** the toolchain hole is closed; the next phase scales **wide**. Memoria, the persistent collections package, metaprog phase 2, and the http-garden differential harness can each become their own GitHub project consumed via `lforge.hermes`, rather than living in-tree under `stdlib/` or `examples/`. See [internals/lforge.md](internals/lforge.md).

Logos develops in alternating multi-month cycles — feature, then refactoring, then feature — never in parallel; each phase closes before the other resumes. See [ADR 0006](adr/0006-lir-hermes-cutover.md) for the most recent refactor close.

## Strategic Direction

Three threads converge on one milestone: **Memoria, ported to Logos, used inside the Logos build system itself.**

### Metaprogramming in three phases (MP1 / MP2 / MP3)

Generative-first: a metaprogram is the primary thing, the user's code is data it consumes to build the final program. The DF-graph of metafunctions runs to a fixed point; declarations existing before that cycle are immutable. See [internals/metaprog.md](internals/metaprog.md), [ADR 0003](adr/0003-metafunctions.md), [ADR 0004](adr/0004-definition-centric-tu.md).

**MP1 — targeted application + templates + type-level metaprog.**
- `template struct/enum/impl` keyword: declarations as data.
- `#[apply(metafn)]` at declaration sites; linear composition.
- Typed `quote_*!` forms (`quote_item!`, `quote_expr!`, `quote_stmt!`, …) with `#expr` antiquotation and `#(...)*` repetition.
- Type-level computation as ordinary Logos run at compile time.
- Coverage ≈ Rust's `macro_rules!` and proc-macros, but through the generative model rather than token rewriting.

**MP2 — build system `lforge` with MCP/LSP integration.**
- A Logos-written build system replacing make/ninja for production builds. **Toolchain MVP shipped 2026-05-07** — see [internals/lforge.md](internals/lforge.md). B0..B5 cover `build`/`run`/`update`/`clean`/`test`/`install`/`version` over multi-target projects, transitive git deps, MVS, lockfile, content-addressed cache, `replace:`, `requires_logos:`. Daemon mode + LSP/MCP remain open.
- Build graph aware of the MP1 DF-graph; incremental on Hermes content hashes. B4 cache keyed on `logosc` mtime + sub-dep cache_keys → transitive invalidation on compiler upgrades and source edits.
- MCP server for AI authors as first-class users; LSP server for IDEs; VS Code plugin candidate.
- Single integration surface: model and IDE talk to one service holding build + semantic + metaprog state simultaneously.

**MP3 — transformative phase on top of `logos`.**
- Transformative metaprogramming: AOP-style passes, BC, escape analysis, lints — all `Pass<Rewrites, Diagnostics>` plugins in `logos`.
- Identity table (`Map<U32, AnyVal>` reachable from the zone root) plus parent links for stable, compactification-surviving AST identity. See `feat_metaprog_phases.md`.
- Datalog over a fact-base as the universal pointcut mechanism, shared by user passes, the borrow checker, and trait resolution.

### The build system as the new center of gravity

The strategic shift: **the build system `logos` becomes the center**, and the compiler is decomposed into services plugging into it — the named orchestrator the SOA-compiler vision ([ADR 0004](adr/0004-definition-centric-tu.md), [ai-platform](ai-platform/README.md)) always implied. Why it is the right center:
- Incremental analysis, content-hash caching, dependency tracking are build-system primitives, and the metaprog DF-graph lives at this layer.
- AI-via-MCP and IDE-via-LSP both want a single contact point holding whole-project state; the build system is the only candidate.
- SOA services without an orchestrator are components without a host. `logos` is the host.

### Memoria as the only driving use case

Logos's metaprogramming is **out of distribution** — no comparable model to copy. C++ TMP, Rust `macro_rules!`/proc-macros, Zig `comptime`, Sutter's metaclasses (P0707), AspectJ, Lisp macros each contribute a fragment; none gives the whole (generative + transformative + DF-graph + build-system-integrated). Precedents are mostly negative examples. So design happens through practice: Memoria drives MP1 (derives for schemas, indices, serialization, Hermes zones) and later MP3 (instrumentation for query planning, persistence boundaries, transactional wrapping). Logos and Memoria are co-developed, like Hermes and Logos.

## Self-Hosting Plan

The compiler is C++ today and stays C++ for the foreseeable future. Porting it to Logos is gated on two preconditions:

1. The Logos language has stabilized — type system, full metaprog model (MP1+MP2+MP3), borrow checker, and stdlib APIs settled enough that porting is not chasing a moving target.
2. Executable Logos compilers (or cross-compilers) exist for all major target platforms. Without that, "build from source" breaks on platforms with no pre-built Logos compiler — a distribution-level bootstrap problem distinct from local-dev.

When both hold, almost everything moves to Logos: frontend, sema, metaprog runtime, BC, stdlib, build system, Memoria. **Codegen stays C++** — MLIR/LLVM bindings are a C++ API; rewriting them in Logos is neither realistic nor useful. Codegen becomes an SOA service in `logos`. Logos is for application code and data engines, not systems-level work C++ already covers.

### Two-stage bootstrap

- **Stage 1 (bootstrap):** cmake + ninja, builds `logos` (build system + compiler) from a clean machine. Permanent — the only path not requiring a pre-existing Logos toolchain.
- **Stage 2 (`logos`):** every other build — stdlib, Memoria, user projects, future compiler refactorings.

Cmake/ninja stay as a minimal-surface service tool, neither improved nor replaced.

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
- Dedup of `@`-literal blob globals via `unnamed_addr` and ConstantMerge.
- See `feat_metacall_arch.md`.

### Compiler IR refactor (just closed)
- Hermes adopted as the L-IR format. `LExpr`/`LStmt`/`Pattern`/`HermesVal` are POD shells with a `mirror_offset_` into a Hermes document; `lir_view` is the typed accessor.
- Why: the bytes *are* the IR, making content-hash, cross-process exchange, and a future SOA split tractable.

### Tooling and Tests
- `logosc` driver, end-to-end build via CMake + VCPKG.
- `lforge` build system, toolchain-MVP complete — `build`/`run`/`update`/`clean`/`test`/`install`/`version` from a Hermes-SDN manifest, with multi-target projects, native C/asm sources, parallel per-file compile, mtime-based incremental, transitive git deps via clone cache, lockfile + MVS, content-addressed build cache, `replace:` overrides, `requires_logos:` ABI floor. Split across an entry point + 15 sub-packages (`tools/lforge/main.logos` + `tools/lforge/pkg/`). See [internals/lforge.md](internals/lforge.md), [internals/package-manager.md](internals/package-manager.md).
- `logosc --version` / `-V` — single source of truth for the version string.
- `logosc --diag-format=json` — NDJSON diagnostics + structured exit codes (`EXIT_USER_ERROR` / `EXIT_USAGE` / `EXIT_LINK_IO` / …) for lforge and IDEs.
- Stdlib gap-fill for build orchestration: `path::normalize`, `str_*` predicates + `Splitter`, `fs::walk_dir`/`mkdir_p`/`rm_rf`/`canonical`, `Child.stdout_lines() : Iterator<String>`, JSON parser, sha256 + sha256_hex, FS-CAS primitives.
- Grammar: no-struct-lit cond expression chain (`if a < b { ... }` without parens); UTF-8 codepoints in char literals.
- Test suite: 1013+ unit/integration/diag tests, plus 16 `lforge` shell scenarios.
- HRPC, reactor (io_uring + green fibers), verification subsystems.

## In Progress (Current Feature Phase)

Toolchain hole closed; scaling **wide** — building real things in their own repos. Direction set by Memoria's needs and the MP1 plan. See `MEMORY.md` for the live picture.

- **Memoria-on-Logos as a first external project.** Driving use case for MP1 derives + persistent collections; consumed via `lforge.hermes` from a separate repo.
- **lforge cache prune.** GC for `~/.cache/lforge/build/`; the only original v1 item still pending.
- **Daemon mode + file watcher; LSP / MCP servers.** Single integration surface for AI and IDE clients.
- Foundations for MP1 (templates, `#[apply]`, typed quote, identity-table groundwork).
- Continued metacall shake-out: `Hermes`-return parity, blob dedup, capture roadmap (`feat_metacall_arch.md` §"Captures").
- Datatype × Storage × View Hermes refactor — GAT-style relations, `UnsizedPayload`, `Meta + Atom` for small objects.
- Decimal View — moving `to_f64`/`to_string_value` from `*const Decimal` onto a `DecimalView` carrying `base + ptr`.
- HTTP differential harness via http-garden continues finding RFC 7230 parser bugs.

## Near-Term

- **Auto traits** — Send/Sync-like markers, compiler-derived from field types. Required for the concurrency model.
- **Bitwise intrinsics** — `popcount`, `clz`, `ctz`, `bswap` via LLVM intrinsics. ~10× speedup expected for `TinyObjectMap` hot paths.
- **`pub const` end-to-end** — grammar, sema, import. Today blocked behind a `pub fn` accessor workaround.
- **Unicode in source** — parser is ASCII-only today, comments included.
- **Multi-clause / nested-for comprehensions** — chained `for`/`if` clauses (`[x for xs in mat for x in xs]`).
- **`Set<T>`** — until then, `Map<K, ()>` / `ObjectMap<K, null>` carries set semantics.
- **`if let`** — `match` with a wildcard arm is the workaround.
- **Iterator surface — finish the chain story.** `Iterator`/`IntoIterator` traits and `for x in iter`/`vec`/`&vec`/`&mut vec` desugar landed (2026-05-07). Still missing:
    - `lo..hi` as a free expression (today only legal in for-head; `(0..10).rev()` doesn't parse — must write `range_i32(0, 10).rev()`).
    - `.enumerate()` / `.map()` / `.filter()` as `Iterator` methods (today only free functions `iter_enumerate`/`iter_map`/`iter_filter`).
    - Tuple-destructure binding in for-head: `for (i, x) in it { }` — grammar accepts only a single IDENT.
    - Deref-pattern in for-binding (`for &item in xs { }`) and slice `.iter()` so `bytes.iter().enumerate()` works on `&[u8]`.
- **Constraint solving via Z3** — clean isolated solver layer for trait resolution and reward signals.

## Longer-Term

- **MP2 build system `lforge`** continued — daemon mode, file watcher, MCP and LSP servers, VS Code plugin. (MVP closed 2026-05-07; B6+ pending: cache prune, more `replace` forms, `requires_logos` inheritance, cross-compile, vendoring, signing.)
- **MP3 transformative phase** — `Pass<Rewrites, Diagnostics>`, identity table, parent links, Datalog fact-base.
- **Datalog / Rete engine in Logos** — native, on Hermes, for application-level queries (the compiler internally uses Souffle).
- **Coroutines / FSM optimization** — stackful fibers by default with implicit suspend; lower to FSMs via `llvm.coro.*` where escape analysis permits.
- **Module binary delivery format** — `libfoo.a = foo.o + foo.hermes` (interfaces + full template bodies + metadata).
- **Three-implementation Hermes** — Logos reference, Rust derived, C++ follower. Per-language conformance tooling; no FFI; pass zone pointers.
- **Layer 2 schema sync and link-time collision detection** — deferred until the Logos Hermes API stabilizes.
- **Compiler ported to Logos**, gated on the two [Self-Hosting Plan](#self-hosting-plan) preconditions. Codegen stays C++.
- **Genos** — schema × API × algorithm as a structured inductive programming target.

## Known Bugs and Quirks

Tracked items, not surprises. Most have memory entries with reproducers.

- Associated-fn generic-impl monomorphization gap: `T::assoc_fn(...)` for a generic trait impl can fail; workaround via self-method with phantom argument.
- `Map<K, V>` concrete-impl dispatch: sibling concrete specializations can suppress each other's tag-dispatch registration; use a blanket `impl<V>` instead.
- Struct-array literal stride: `[Struct; N] = [...]` uses 8-byte stride instead of `sizeof(T)`; workaround via raw `[i64; 2*N]`.
- Shadowed `let` + `==`: an inner `let X: i64` shadowing an outer match binding `X` makes `==` compare an alloca pointer against an `i64`. Rename to work around.
- Struct field separator is `;` not `,`. Cosmetic; likely to change.

## Source of Truth

The canonical machine-readable plan lives in `MEMORY.md` (`~/.claude/projects/-home-victor-devel-logos/memory/MEMORY.md`) and `~/.claude/plans/validated-tinkering-spring.md`. The deprecated `openspec/`, the older `docs/datatypes.md`, and in-tree `sandbox/` are not authoritative. Strategic context:
- `project_mp_roadmap.md` — the MP1/MP2/MP3 split and build-system pivot.
- `project_self_hosting.md` — self-hosting plan, two-stage bootstrap, codegen exception.
- `project_dev_cadence.md` — the feature ↔ refactor rhythm.
- `feat_metaprog_inversion.md` — generative model, single syntax, metacall and template/quote as the only world boundaries.
- `feat_metaprog_phases.md` — Phase 1 vs Phase 2, identity table, parent links, trade-off summary.
