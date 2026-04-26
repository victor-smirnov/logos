# Roadmap

This page summarizes implementation status across the Logos project. It is updated by hand and is a snapshot, not a contract.

## Done

### Language Core
- Statically typed compiled language, native binaries via LLVM/MLIR.
- `struct`, `enum` (with payload variants), traits, generics, monomorphization.
- Pattern matching with `match`.
- Implicit safe integer widening (`u32 → i64`, etc.) via injected casts.
- Postfix `?` operator on `Result<T, E>`.
- Function, method, and operator overloading with strict exact-type resolution.
- `HashMap<K, V>` with `Hash`/`Eq` infrastructure for primitive keys; iteration; multi-key shapes.
- List and map comprehensions in plain (`Vec<T>`, `HashMap<K, V>`) and Hermes (`@[...]`, `@{...}`) forms.
- Ownership and borrowing with a flow-sensitive borrow checker (Phases 1–4).
- Named lifetimes on function signatures.

### Hermes
- Document, parser, stringifier, binary codec, cross-zone clone.
- `Map<K, V>`, `Array<T>`, typed arrays (`I32`, `U64`, …), `String`, `Decimal`.
- Trait-dispatched function registry (`HermesStringify`, `HermesEqual`, `HermesHash`, `HermesClone`, `HermesRelease`).
- Capture (`$ident`, `${expr}`) inside `@{...}` / `@[...]`, including `as<T>[...]` typed-array casts.
- View types: `HermesCtrView<'a>`, `HermesStatic` for rodata literals.
- Read/Write trait split.
- Zone migration to `Zone<Mutable>` parameterization on the parser path.

### Generic Containers
- `Array<T>` and `Map<K, V>` migrated from concrete `ArrayI32`/`ArrayU64` (commit `6594bd4`).
- Blanket trait impls; `CloneElem` and `RelPtr` element-side traits.
- Partial specialization with deferred `type_code_of` and monomorphization-time dispatch emit.

### Tooling and Tests
- `logosc` driver, end-to-end build via CMake + VCPKG.
- Test suite: ~660 passing, ~245 diagnostic tests.
- HRPC, reactor (io_uring + green fibers), and verification subsystems.

## In Progress

- **Phase 2c.4e** — `TypeRef` accessor migration in the compiler. 33 of ~255 sites done; large files (`sema_expr`, `sema_stmt`, `mlir_gen_*`, `mono_impl`) remain.
- **Datatype × Storage × View** Hermes refactor — GAT-style relations between datatype, storage, and view; `UnsizedPayload` for variable-length types; `Meta + Atom` for small objects.
- **HTTP differential harness** via http-garden. First pass surfaced three RFC 7230 parser bugs (commit `a47f447`).
- **Decimal View** — moving `to_f64`/`to_string_value` from `*const Decimal` onto a `DecimalView` carrying `base + ptr`.

## Planned (Near-Term)

- **Auto traits** — Send/Sync-style markers, compiler-derived from field types. Required for the concurrency model.
- **Bitwise intrinsics** — `popcount`, `clz`, `ctz`, `bswap` via LLVM intrinsics. ~10× speedup expected for `TinyObjectMap` hot paths.
- **`pub const` end-to-end** — grammar, sema, import. Today blocked behind a `pub fn` accessor workaround.
- **Unicode in source** — currently parser is ASCII-only, comments included.
- **Multi-clause / nested-for comprehensions** — chained `for`/`if` clauses (Python's `[x for xs in mat for x in xs]`). Single-clause + optional guard is implemented; the grammar will be extended.
- **Set<T>** — until then, `Map<K, ()>` / `ObjectMap<K, null>` carries set semantics.
- **`if let`** — `match` with a wildcard arm is the workaround.
- **Constraint solving via Z3** — clean isolated solver layer; for trait resolution and reward signals.

## Planned (Longer-Term)

- **Compile-time Logos programs** — ordinary Logos with a compiler API; replaces macros/templates. See [Metaprogramming](internals/metaprog.md).
- **Datalog / Rete engine in Logos** — native, on Hermes; candidate for the compiler's own trait resolution; dogfooding loop.
- **Coroutines / FSM optimization** — stackful fibers by default with implicit suspend; the compiler lowers to FSMs via `llvm.coro.*` where escape analysis permits.
- **Module binary delivery format** — `libfoo.a = foo.o + foo.hermes` (interfaces + full template bodies + metadata). Lands after Hermes integration matures.
- **Compiler frontend rewrite** — Rust frontend + C++ codegen, sharing Hermes as IR via a zone pointer. No FFI for AST.
- **Three-implementation Hermes** — Logos as reference, Rust derived, C++ as follower. Conformance tooling per language; no FFI; pass zone pointers.
- **Layer 2 schema sync and link-time collision detection** — deferred until the Logos Hermes API stabilizes.
- **Agora platform** (working name `lforge`) — second-stage data platform: storage, network, distributed, MCP/LSP/HTTP, build.
- **Genos** — schema × API × algorithm as a structured inductive programming target.

## Known Bugs and Quirks

These are tracked items, not surprises. Most have memory entries with reproducers.

- Associated-fn generic-impl monomorphization gap: `T::assoc_fn(...)` for a generic trait impl can fail; workaround via self-method with phantom argument.
- `Map<K, V>` concrete-impl dispatch: sibling concrete specializations can suppress each other's tag-dispatch registration; use a blanket `impl<V>` instead.
- Struct-array literal stride: `[Struct; N] = [...]` uses an 8-byte stride instead of `sizeof(T)`; workaround via raw `[i64; 2*N]`.
- Shadowed `let` + `==`: an inner `let X: i64` shadowing an outer match binding `X` causes `==` to compare an alloca pointer against an `i64`. Rename to work around.
- Struct field separator is `;` rather than `,`. Cosmetic; will likely change.

## Source of Truth

The canonical project plan lives in [MEMORY.md](#) and `~/.claude/plans/validated-tinkering-spring.md` (out of tree). The deprecated `openspec/`, the older `docs/datatypes.md`, and the in-tree `sandbox/` are not authoritative.
