# Three-Layer Stdlib Split

Single design document for the migration of the current monolithic
`stdlib/` (one `module stdlib`, one `liblstdlib.a`, all under
`std.*` package paths) into three independent layers with explicit
dependencies, mirroring Rust's `core` / `alloc` / `std` separation
but with Logos-native naming.

Status: **planning complete, not started.** Phase 1 kickoff awaits
explicit start command. Last revised 2026-05-16.

---

## Target end-state

Three independent Logos modules, three archives, three preludes,
linear dependency:

| Layer | Artifact | Package prefix | Prelude | Depends on |
|---|---|---|---|---|
| L0 — language core, no heap, no OS | `liblogos-lang.a` | `logos.lang.*` | `logos.lang.prelude` | — |
| L1 — heap, no OS | `liblogos-mem.a` | `logos.mem.*` | `logos.mem.prelude` | lang |
| L2 — full (heap + OS + IO) | `liblogos-std.a` | `logos.std.*` | `logos.std.prelude` | mem, lang |

Each layer ships **two parallel subtrees**:

```
stdlib/
  lang/
    logos.module
    <own>/...           ← Logos-native code (no Rust analog)
    imported/...        ← Rust-derived (provenance headers required)
      RUSTC-PROVENANCE.md
  mem/
    logos.module
    <own>/...
    imported/...
      RUSTC-PROVENANCE.md
  std/
    logos.module
    <own>/...
    imported/...
      RUSTC-PROVENANCE.md
  rt/                   ← native runtime support (C/asm), unchanged
```

The compiler globs both subtrees and merges by `package` declaration —
physical placement is purely organizational (own-vs-imported is a
provenance + Apache-2.0 § 4(b) requirement, not a compile-time
distinction).

## CLI surface

logosc gains tier flags:

- `--no-alloc` — link only `liblogos-lang.a`. Prelude = `logos.lang.prelude`.
- `--no-std` — link `liblogos-lang.a + liblogos-mem.a`. Prelude = `logos.mem.prelude`.
- (default) — link all three. Prelude = `logos.std.prelude`.

Manifest sugar: `tier lang | mem | std` in `logos.module`,
equivalent to the corresponding CLI flag for that package.

---

## Layer assignment — high-level rules

**`logos.lang.*` (L0):** language items + read-only / static
utilities. No heap allocation in any code path.
- Trait machinery: `Option/Result`, `Clone/Copy/Drop`, `PartialEq/Eq/Ord/Hash`,
  `Iterator + adapters (trait defs only)`, `Fn/FnMut/FnOnce`, `From/Into/TryFrom`,
  `AsRef/AsMut`, `Default`, `marker`, `range`, `ops`.
- Primitives: `bool`, `char`, integer/float ops, `str` (read-side only).
- `logos.lang.fmt` — formatter trait + arg-list types (no heap).
- `logos.lang.math` — free functions over primitives.
- `logos.lang.metaprog` — compile-time only; metacall JIT runs in
  the compiler's arena, never in user runtime, so no user-side alloc.
- `logos.lang.tokens` — compiler interface.
- `logos.lang.hermes.{view, static_view, hermes_read, anyval_read,
  tags, datatag, type_tag_read, relptr, relptr_traits,
  stringify_into, hbs_read, pat}` — read-only Hermes surface.

**`logos.mem.*` (L1):** owning containers and the allocator-side
runtime. Heap, no OS.
- `box`, `rc`, `arc`, `vec`, `string`, `collections.{hash_map,
  hash_set, btree_map, vec_deque}`.
- `fmt` runtime (`format!` macro path that allocates).
- `persistent.*` — mini-Memoria CoW B+tree stack.
- `hermes.{mem_holder, zone, own, release, parser, objectmap, array,
  ctr, clone, hbs_write, string, type_tag_dynamic}` — mutable
  Hermes-fabric runtime.

**`logos.std.*` (L2):** everything OS-touching.
- `io` (Read/Write traits live here, not in lang — bytes flow over OS).
- `fs`, `path`, `net`, `net.tls`, `net.url`, `pipe`, `linux.uring`, `http`.
- `sync`, `sync.atomic`, `thread`, `rt.fiber`.
- `time`, `time.datetime`.
- `env`, `process`, `os.*`.
- `log`, `testing`, `crypto`.
- `encoding.{json, csv, base64, hex}`.
- `random`.

Phase 2 produces a per-package authoritative table in
`docs/core-port/layer-assignment.md`.

---

## Prelude design

**`logos.lang.prelude`:**
- Types: `Option`, `Result`, `Ordering`.
- Variants: `Some/None`, `Ok/Err`, `Less/Equal/Greater`.
- Traits: `Copy`, `Sized`, `Send`, `Sync`, `Unpin`, `Drop`, `Clone`,
  `Fn/FnMut/FnOnce`, `AsRef/AsMut`, `From/Into`, `Default`,
  `PartialEq/Eq/PartialOrd/Ord`, `Hash`, `IntoIterator`, `Iterator`,
  `Extend`, `DoubleEndedIterator`, `ExactSizeIterator`.
- Functions: `drop`.

**`logos.mem.prelude` (depends `logos.lang.prelude`):**
- Adds: `Box`, `String`, `Vec`, `ToString`, `ToOwned`.

**`logos.std.prelude` (depends `logos.mem.prelude`):**
- Re-exports both. No new items expected initially (matches Rust
  `std::prelude::v1`).

**Versioning:** append-only without versions (no `prelude.v1`).
Forward break = new prelude package, hard cut. Aligned with the
"linear, append-only" HARD RULE for tier surfaces.

**Implicit-import mechanism:**
- Manifest directive `prelude <pkg-name>` declares which package is
  the prelude for files in this module.
- Sema's collect phase inserts an implicit `use <prelude-pkg>` at
  the head of every AST file in that module — unless the file
  carries `#![no_implicit_prelude]` (file-level attribute, new
  grammar).
- The prelude package itself, and any stdlib-internal that would
  recurse onto its own prelude, carries `#![no_implicit_prelude]`.

**Conflict resolution:** explicit `use` beats implicit prelude
(Rust convention).

---

## Migration plan

### Phase 1 — Manifest plumbing + grammar verification — DONE 2026-05-16

**No source moves.** Confirms that the infrastructure can support
a three-archive depends chain, and surveys grammar gaps before
catalog work.

**Work delivered:**

- `manifest.depends` wired through `emit_module`. Convention:
  `depends X` → `libX.a` lookup in search_paths (Unix-style;
  matches `-lX` linker convention). Resolved paths prepended to
  `EmitModuleOptions::extra_lib_files` so they load before any
  user-supplied `-l` files. New helper
  `resolve_manifest_depends` in `src/compiler/emit_module.cpp`.
- `tests/logos/three_layer/{low, mid, hi}.module` fixture (mid
  `depends low`, hi `depends mid + low`) + smoke test
  `tests/logos/pass/three_layer_chain.logos`. Verified
  end-to-end: build chain works, consumer compiles + runs.
- Negative path verified: missing dependency yields
  `emit_module: manifest 'depends X': cannot find 'libX.a' in any
  search path` and `exit 1`.

**Grammar/sema audit findings:**

- **Glob `use <pkg>.*`** — not parsed in grammar, but `use pkg;`
  already imports all public items (de-facto glob). No work
  needed for prelude — `use logos.lang.prelude;` will Just Work.
  Explicit `.*` form is cosmetic-only; defer indefinitely unless
  user pressure surfaces.
- **`pub use` re-exports** — **fully working** end-to-end
  (grammar `pub_use_decl` at `logos.peg:467`, sema at
  `sema_collect.cpp:213-221`, used in `tests/logos/pub_lib/`).
  The "defer pending advanced module design" note in earlier
  drafts is stale — the feature is available. The migration
  policy stays "atomic per-package, no shim period", but if a
  shim is ever needed in Phase 4, `pub use` is on the shelf.
- **`#![no_implicit_prelude]`** — confirmed not parsed. Outer
  `#[...]` works (grammar `annotation` at `logos.peg:579`,
  attribute registry in `sema_impl.hpp`), but module root
  (`logos.peg:462`) accepts only `inner_doc_decl` /
  `inner_doc_block_decl` before `package`, not `annotation`.
  No `AttrTarget::Module` enum value. No `LProgram` field for
  file-level annotations. Estimated complexity: ~200 lines
  (grammar + AttrTarget + LProgram field + sema_collect
  extraction). Add in Phase 3 alongside prelude infrastructure.

**Deferred from this phase:**

- Drop of two prefix-guards
  ([`module_loader.cpp:919`](../../src/compiler/module_loader.cpp#L919)
  `pkg_in_prelude` + [`sema_impl.hpp:1178`](../../src/compiler/sema_impl.hpp#L1178)
  `is_stdlib`) — **moved to Phase 7**. Both have concrete
  purposes (the former is a de-facto implicit-prelude
  auto-load; the latter gates reserved type-code range
  `[1..128]`). Replacing them requires the manifest `tier`
  field which lands in Phase 3. Phase 1 attempt would have
  required a no-op string-list parameterization that Phase 3
  rewrites anyway — skipped.

**Blast radius:** zero — pure infrastructure addition. All
existing tests pass; one new test added.

### Phase 2 — Catalog (paper only) — DONE 2026-05-16

**No source moves.** Two documents produced:

- [`layer-assignment.md`](layer-assignment.md) — per-package
  target name + layer + rationale for all 106 current packages.
  Includes splits (`std.lang.text` → str+string,
  `std.fmt` → lang+mem), merges (ops absorbs arith+range+drop;
  cmp absorbs ord; io.* flattens), relocations
  (`std.lang.thread` → `logos.std.thread` — was misplaced),
  and the Hermes per-package split (15 lang + 17 mem out of 32
  sub-packages). Final counts: ~35 lang + ~40 mem + ~27 std =
  ~102 target packages.
- [`prelude-design.md`](prelude-design.md) — exact contents of
  `logos.lang.prelude`, `logos.mem.prelude`, `logos.std.prelude`
  with explicit rationale for inclusions and exclusions, a
  Rust-comparison table, and the Phase 3 implementation sketch
  (manifest directive + sema injection + no_implicit_prelude
  opt-out + which stdlib-internal files must carry the opt-out).

**Reviewable, no commits to stdlib.**

### Phase 3 — Build infra (parallel path) — DONE 2026-05-16

**No removal of existing monolith.** Three new manifests + three
new archives stand up alongside the current `liblstdlib.a`.

**Work delivered:**

- **Manifest fields** ([src/compiler/module_manifest.{hpp,cpp}](../../src/compiler/module_manifest.hpp)):
  `tier {lang|mem|std}` and `prelude <pkg>` directives. `tier`
  validated against closed set. `prelude` stored only (enforcement
  via sema injection — see below).
- **Grammar** ([tools/peg_gen/grammars/logos.peg](../../tools/peg_gen/grammars/logos.peg)):
  new `inner_annotation` production parses `#![name]` /
  `#![name(args)]` / `#![name=val]` before the `package` directive.
  New AST code `INNER_ANNOTATION` (242). Glob `use pkg.*` deferred —
  Phase 1 audit confirmed `use pkg;` already de-facto glob.
- **Sema implicit-prelude injection** ([sema_collect.cpp](../../src/compiler/sema_collect.cpp)
  + [sema.cpp](../../src/compiler/sema.cpp)): manifest's `prelude`
  package added to `cur_imports_.wildcard_packages` for every
  non-binary file that doesn't carry `#![no_implicit_prelude]`.
  Threaded via `SemaOptions::implicit_prelude` →
  `SemaChecker::implicit_prelude_`. Self-import and
  explicit-`use` dedup. Binary-archive files skip (producer
  already applied its own prelude).
- **Loader thread** ([module_loader.cpp](../../src/compiler/module_loader.cpp)):
  `extract_uses` takes optional `implicit_prelude` arg; appends
  via `finalize` lambda after explicit-uses loop. `load_modules`
  signature extended. Plus a fix to `scan_package_decl` to skip
  `#![...]` lines that may legitimately precede `package`.
- **emit_module wiring** ([emit_module.cpp](../../src/compiler/emit_module.cpp)):
  `compile_to_object` takes `implicit_prelude` arg; passes
  `manifest.prelude` through to both `load_modules` and
  `sema_lower`.
- **Three layer manifests** (placeholder content):
  - `stdlib/lang/logos.module` — `module logos-lang`, `tier lang`
  - `stdlib/mem/logos.module` — `module logos-mem`, `tier mem`,
    `depends logos-lang`
  - `stdlib/std-new/logos.module` — `module logos-std`, `tier std`,
    `depends logos-mem`, `depends logos-lang`
  - One `_placeholder/_placeholder.logos` per layer carrying
    `#![no_implicit_prelude]` (defensive — no items in scope
    yet, but verifies the opt-out attribute parses everywhere).
  - `std-new` is the temporary name — renamed to `std` in Phase 7
    after the old monolith is removed.
- **CMake** ([tests/logos/CMakeLists.txt](../../tests/logos/CMakeLists.txt)):
  three `add_custom_command` invocations modeled on the existing
  `liblstdlib.a` build, with depends-chain ordering. Outputs to
  `${LOGOS_BUILD_LIB_DIR}` (same dir as monolith) so existing
  logosc auto-discovery picks them up without changes.
- **Trilayer fixture extension**
  ([tests/logos/three_layer/](../../tests/logos/three_layer/)):
  mid.module declares `prelude three_layer.low`; mid.logos uses
  `low_double` without explicit `use` — exercises injection.
  hi.module declares `prelude three_layer.mid`; hi.logos carries
  `#![no_implicit_prelude]` and falls back to explicit uses —
  exercises opt-out.

**Discovery — no changes needed.** The existing
`build_binary_index` globs `lib*.a` in search paths, so
`liblogos-lang.a` etc. are auto-discovered alongside
`liblstdlib.a`. During Phase 4 migration the two paths coexist
(first archive in scan order wins on duplicate package).

**Blast radius:** zero for existing code — parallel path. All
3197/3197 tests pass.

### Phase 4 — Per-package migration

**In dependency order: lang → mem → std.** One atomic commit per
package (rename + grep-replace consumers + `git mv` together; no
shim period since `pub use` is deferred).

Per package:

1. Decide own-vs-imported placement:
   - **Logos-native (no Rust analog)** → `stdlib/<layer>/<pkg-path>/`.
   - **Adapted from rustc** → `stdlib/<layer>/imported/<pkg-path>/`
     with provenance header per `stdlib/imported/README.md`.
2. Mass-sed `package std.X.Y` → `package logos.<layer>.Y` in files.
3. Mass-sed `use std.X.Y;` → `use logos.<layer>.Y;` across the
   repo (stdlib + tests + examples + tools/lforge).
4. `git mv` files to the new physical location.
5. Build clean → full ctest.

**Consumer scope** (everything outside `stdlib/`):
- `tests/` — pass/fail/imported/lazy/coex/diag/lforge fixtures.
- `examples/` — 4 files (`persistent_showcase.logos`,
  `hermes_round_trip.logos`, `logos_showcase.logos`,
  `hermes_showcase.logos`).
- `tools/lforge/` — ~17 .logos files (main.logos + pkg/).
- External lforge-test repo on GitHub — temporarily disabled
  during migration; re-enabled with renamed imports as a
  validation step.

Per-layer expected pain:
- **lang** — largest consumer footprint (every file imports at
  least `Option`/`Result`/iter), but renames are mechanical.
- **mem** — moderate (Vec/Box/String usage sites).
- **std** — minimal (IO/sync paths are narrower).

### Phase 5 — Imported tree adoption

Activates the per-layer `imported/` subtree as the destination for
Rust ports.

- Migrate `stdlib/imported/RUSTC-PROVENANCE.md` from a single
  monolith into a thin root index + per-layer manifests:
  - `stdlib/lang/imported/RUSTC-PROVENANCE.md`
  - `stdlib/mem/imported/RUSTC-PROVENANCE.md`
  - `stdlib/std/imported/RUSTC-PROVENANCE.md`
- Tests: `tests/imported/core/` → `tests/imported/lang/`. Add
  `tests/imported/mem/`, `tests/imported/std/`. (Existing
  `tests/imported/{pass, fail}/` are rustc tests/ui — they stay
  under those paths and remain layer-agnostic.)
- First port batch lands in `stdlib/lang/imported/` (e.g. real
  `core::option` content into `logos.lang.option`).

### Phase 6 — Availability constraints

- **6.A — Library-side enforcement.** Manifest's `tier` is
  authoritative. A package at tier L cannot `use` a package
  at tier >L. Sema error with diagnostic naming both packages
  and both tiers.
- **6.B — Binary-side flags.** `--no-alloc` and `--no-std` (and
  manifest `tier`) propagate to a `target_tier` bit in SemaCtx.
  Imports beyond the bit are rejected. Linker (CMake) drops the
  excluded archives from the link line.
- **6.C — Sema feature gating under `--no-alloc`:**
  - `@{...}` / `@[...]` with **runtime-value** `${expr}` captures
    rejected. Diagnostic: «`@{...}` runtime-value capture
    `${...}` requires a runtime zone allocator (mem tier);
    compiling with --no-alloc — use a const/type capture or
    remove --no-alloc».
  - Type-level (`<type:T>`) and const-generic captures **remain
    allowed** — they resolve at monomorphization, the resulting
    blob is static rodata.
  - HermesStatic, HermesView, HermesRead, pattern matching
    `@{"k": pat}`, AnyVal read, metacall/quote (compile-time
    only) — all **remain available**.
  - `mem.*` and `std.*` package imports rejected.

Implementation note for 6.C: the existing `has_captures` flag in
[`src/compiler/mlir_gen_expr.cpp:3970`](../../src/compiler/mlir_gen_expr.cpp#L3970)
may already mean "has runtime-value captures" (type/const captures
are resolved before LIR). Verify; if too coarse, factor into
`has_runtime_captures` vs `is_pure_static` and gate sema on the
former.

### Phase 7 — Cleanup

- Delete `stdlib/logos.module` (old monolith).
- Delete `stdlib/std-new/` symlink/rename to `stdlib/std/`.
- Replace `find_enum_by_name("Option")`-style hardcodes
  (sema_expr.cpp, sema_stmt.cpp, sema.cpp) with prelude lookup —
  identifies Option/Result/Vec/Box through the active prelude
  rather than literal type-name strings. Migration is per-call-site
  and easier after prelude infrastructure is stable.
- Update [`docs/internals/architecture.md`](../internals/architecture.md)
  with the three-layer diagram.
- Write `project_three_layer_stdlib.md` memory entry; add
  invariants for layer linearity (lang ⊂ mem ⊂ std) and the
  no-implicit-prelude requirement for stdlib-internal files.

---

## Open items deferred

- **`pub use` re-exports as a migration shim.** The feature is
  available in the language (verified Phase 1), but migration
  policy stays "atomic per-package, no shim period" — every
  consumer renames in the same commit as the package itself.
  `pub use` becomes useful later for Rust-style re-export trees
  (e.g. `logos.std.prelude` re-exporting from
  `logos.mem.prelude`) — Phase 3 prelude design relies on it.
- **Edition / prelude versioning.** Append-only for now;
  versioning may land if a breaking prelude change is ever needed.
- **HW-tier subset** (cf. `discuss_runtime_stdlib_tiers_2026_05_07.md`
  Thread 7-8). Out of scope; the three-tier split is forward-
  compatible with a future tape-out subset annotation.

---

## Per-file decision template (for Phase 4 commits)

Each migration commit follows:

```
stdlib: migrate <package> to <new-package> (<layer> layer)

Was: package std.X.Y in stdlib/std/X/Y/
Now: package logos.<layer>.Y in stdlib/<layer>/{Y,imported/Y}/

Consumers updated: <N> files in stdlib, <N> in tests, <N> in
examples, <N> in tools/lforge.
```

Body lists every grep-replace target. Single-package per commit,
even if multiple packages co-located in the source.
