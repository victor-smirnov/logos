# Hermes Layer Split — Design

Sub-design for the three-layer split's hardest sub-problem: cleaving
the 32-package `std.hermes.*` tree into a lang-tier (lifecycle +
read-only) part and a mem-tier (allocate + mutate) part. This design
unblocks Phase 4 — the layer-lang archive currently cannot stand
alone because `std.lang.any` and `std.lang.cmp` use `std.hermes.view`
and `std.lang.text` (both in monolith), creating a circular dep with
layer-lang's `logos.lang.*` content.

Status: **Steps 0–4 complete.** Steps 5–8 (mem-tier batch, then
unblock any/cmp, then activate excludes) still pending. Companion to
[three-layer-split.md](three-layer-split.md) and
[layer-assignment.md](layer-assignment.md).

### Progress log

| Step | Status | Note |
|------|--------|------|
| 0    | ✅ done | excludes-aware loader plumbing (no activation) |
| 1    | ✅ done | 4 hermes leaves (datatag/tags/typetag/relptr) → lang |
| 2    | ✅ done | hermes lifecycle (mem_holder/zone/own) → lang. release deferred |
| 3    | ✅ done | text split (str/utf8/split → lang; String/Display → mem). 391 consumers |
| 4a   | ✅ done | scalar → lang (no split needed) |
| 4b   | ✅ done | HermesView/HermesStatic structs → lang. HermesRead trait stays mem |
| 4c   | ✅ done | anyval → lang. hermes_pat_eq_str (only HermesString user) moved to std.hermes.pat |
| 4d   | ✅ done | typed_value → lang (transitive name-resolution dep on HermesString via RelPtr<T> field accepted) |
| 4e   | ✅ done | StringView → lang.hermes.view. HermesString stays mem |
| 4f   | ✅ done | relptr_traits → lang (orphan rule allows impl-of-foreign-trait-for-local-RelPtr) |

---

## Core principle (from source code + Victor's call)

> "MemHolder itself does NOT malloc or free memory. It tracks the
> lifetime of whatever buffer was handed to it via a user-supplied
> destroyer callback that fires when the refcount reaches zero."
> — [`stdlib/std/hermes/mem_holder/mem_holder.logos`](../../stdlib/std/hermes/mem_holder/mem_holder.logos)

Lifecycle ≠ allocation. **MemHolder is lang-tier** despite its
convenience `memholder_alloc_backed()` path using `malloc()` — the
type itself is allocator-agnostic.

By the same logic, anything that:
- Manages refcounts / owns handles → **lang** (Zone, Own, Release)
- Reads bytes from a base pointer → **lang** (HermesStatic, HermesView, AnyVal-read, scalar, tags, …)
- Mutates a buffer / allocates into a Zone → **mem** (String, Map, Array, ObjectMap, Ctr, Document, Parser, …)

---

## Per-package split

### → logos.lang.hermes.* (lifecycle + read)

| Package | Notes | Action |
|---|---|---|
| `mem_holder` | RC + Arena handle. Uses libc malloc/realloc/free externs but the *type* is allocator-agnostic per source comment. Only Logos dep: `std.hermes.datatag`. | **Move as-is.** |
| `zone` | Wrapper over `*mut MemHolder` with type-tracking type param `M`. | **Move as-is.** Deps: mem_holder. |
| `own` | RC-bound `Own<T>` smart pointer. | **Move as-is.** Deps: mem_holder. |
| `release` | RC release helper. | **Move as-is.** |
| `datatag` | Pure enum constants. No deps. | **Move as-is.** True leaf. |
| `tags` | Pure constants. No deps. | **Move as-is.** True leaf. |
| `typetag` | Type-tag values. No deps. | **Move as-is.** True leaf. |
| `relptr` | Relative pointer arithmetic. No deps. | **Move as-is.** True leaf. |
| `relptr_traits` | Trait surface. Currently uses clone/equal/hashing/release/relptr/stringify/text. Most refs are trait-bound generics where the trait lives elsewhere. | **Audit + move.** Likely needs no actual code change if its `use` lines update to the new paths. |
| `view` | HermesView/HermesStatic structs + HermesRead trait + base/size accessors. Currently uses string, map, array, objectmap, decimal, document, etc. for owning/stringifying methods. | **SPLIT.** See "View split" below. |
| `anyval` | AnyVal type + tag predicates + read accessors. Uses string, datatag, typetag. | **Audit; likely SPLIT.** Read-side accessors lang; String-producing helpers mem. |
| `scalar` | Scalar value access. Uses tags. | **Move as-is** (read-only). |
| `typed_value` | Typed accessor wrappers. Uses anyval, relptr, string, tags. | **Audit + SPLIT** if String-allocating methods present. |
| `pat` | Pattern matching navigation. Uses anyval/array/map/objectmap/string/typed_value/typetag — but all read-only. | **Move as-is** — depends only on lang-side hermes after this migration. |
| `check` | Validation. Read-only walk. Uses heavy hermes surface. | **Move as-is** (read-only). |
| `equal` | Equality compare. Read-only. | **Move as-is**. |
| `hashing` | Hash function. Read-only. | **Move as-is**. |
| `stringify` | Formatter writing into a caller-supplied `String`. Uses text. | **SPLIT.** Core formatter lang (caller-side buffer); String-allocating wrappers mem. |
| `hbs_read` | Binary wire parser. Reads into provided structures. Uses most hermes types as targets. | **Audit; likely lang** (read-only into pre-allocated). |

### → logos.mem.hermes.* (allocate + mutate)

| Package | Notes |
|---|---|
| `string` | Arena-backed mutable String. Allocator-heavy. |
| `array` | Mutable typed array. |
| `map` | Mutable map. |
| `objectmap` | Mutable object map. |
| `ctr` | Container builder. |
| `document` | Document builder. |
| `parser` | Text parser allocates objects into a zone. |
| `clone` | Clone-into-zone. |
| `hbs_write` | Binary writer (output buffer). |
| `tag_system` | Runtime tag registry (dynamic). |
| `registry` | Dynamic type registry. |
| `decimal` | Decimal arithmetic. Audit for allocate-vs-pure; may be lang. |
| `alloc` | Allocation primitives. **Could** be lang since the *interface* is allocator-agnostic, but the *implementations* hand out heap chunks. Default to mem; revisit if needed. |

---

## View split — concrete plan (template for other splits)

Current `stdlib/std/hermes/view/view.logos` has:
- `struct HermesView { base: *const u8, size: u64 }` ← lang
- `struct HermesStatic { ptr: *const u8 }` ← lang
- `trait HermesRead` with `base()`/`size()` ← lang
- `impl HermesRead for HermesView/HermesStatic` ← lang
- `fn to_string(&self) -> String` ← mem (allocates `String::with_capacity(256)`)
- `fn stringify_into(&self, buf: &mut String)` ← lang (writes into caller buffer)
- `fn get_str_view(&self, val: AnyVal) -> StringView` ← lang (returns view, no alloc)

**Split into two files** under the same logical hierarchy:
- `stdlib/lang/hermes/view/view.logos` — struct definitions, HermesRead trait, base/size, view-returning methods.
- `stdlib/mem/hermes/view/view_owned.logos` — `impl HermesView` adding `to_string()` etc. that allocate Strings. Package `logos.mem.hermes.view_owned` (sub-package) OR via trait `IntoString` in mem with `impl IntoString for HermesView`.

**Decision:** prefer the trait approach (`impl ToHermesString for HermesView`)
over splitting inherent impls — keeps Logos's orphan-rule story clean
and matches Rust's pattern (impl ToString for &str in alloc, not in core).

If Logos does NOT enforce an orphan rule (verify before committing),
the simpler inherent-impl-across-packages approach is open.

---

## Excludes-aware loader (precondition)

Without this fix, monolith's recursive `stdlib/` glob absorbs the new
layer trees as user-source via transitive `use` resolution, defeating
the manifest `exclude` directive. The loader's `build_package_index`
walks search_paths and indexes any `.logos` file it finds — it doesn't
know about excludes.

**Fix** (~30 lines, attempted last session, reverted with this work):

```cpp
// build_package_index now takes abs_excludes; filters at the walk.
static PackageIndex build_package_index(
    const std::vector<std::string>& search_paths,
    const std::vector<std::string>& abs_excludes = {});

// load_modules signature extended:
std::vector<ParsedModule> load_modules(
    ...,
    const std::vector<std::string>& abs_excludes = {});

// emit_module passes abs_excludes (already computed) to load_modules.
```

Once this lands, the monolith's `exclude lang|mem|std-new` directives
actually drop the layer trees, and the chain becomes:
- Monolith = stdlib/std/* only (unmigrated content).
- Layer-lang = logos.lang.* sources, with `-L lib/logos/` for transitional
  std.* dependencies.
- Layer-mem = logos.mem.* sources, depends layer-lang + monolith.
- Layer-std = logos.std.* sources, depends layer-mem + layer-lang + monolith.

---

## Migration sequence

In dependency order — each step verified with full ctest before the next.

### Step 0 — Land excludes-aware loader (PLUMBING ONLY)

Standalone commit. Adds the `abs_excludes` parameter to
`load_modules` and threads it through `build_package_index` and
`emit_module`. **Excludes are not activated** — the monolith
manifest still does NOT carry `exclude lang|mem|std-new` because
activating it now would create an immediate broken-build window:
monolith has 37 files using `use logos.lang.X;` (needs layers),
and layer-lang's `any.logos`/`cmp.logos` use `std.lang.text` /
`std.hermes.view` (needs monolith) — circular.

Revised sequencing: do steps 1-7 first (which break the cycle by
migrating the contested packages out of monolith). Activate excludes
in step 8 once layer-lang truly stands alone.

### Step 0.5 — Break monolith → layer cycle

Monolith files currently use `use logos.lang.option;` etc. — they need
layer archives at build time. But layer-lang's `any.logos`/`cmp.logos`
use `use std.lang.text;` / `use std.hermes.view;` (in monolith). Circular.

Options:
- (A) Build layer-lang WITHOUT `any.logos` and `cmp.logos`. Move those
  files temporarily into an "extras" location built separately after
  monolith. (Yes, ugly two-pass.)
- (B) Pre-migrate `text` and the necessary `hermes.view`/`anyval`/`string`
  pieces to logos.lang.* (this whole doc).
- (C) `pub use` shims at old `std.X` paths in monolith → re-export
  from layer. Monolith files keep `use std.lang.text;` (resolves via
  shim). Defeats "atomic per-package no shim" policy slightly but is
  pragmatic.

**Recommend (B).** Doing (A) for two files is hacky; (C) introduces
shims we'd want to clean up later. (B) is the right architectural
move and this doc IS its plan.

### Step 1 — True leaves

Migrate the 4 zero-dep hermes packages:
- `datatag` → `logos.lang.hermes.datatag`
- `tags` → `logos.lang.hermes.tags`
- `typetag` → `logos.lang.hermes.typetag`
- `relptr` → `logos.lang.hermes.relptr`

One batch commit. Verify ctest.

### Step 2 — Lifecycle layer

- `mem_holder` (deps: datatag) → `logos.lang.hermes.mem_holder`
- `zone`/`own`/`release` (deps: mem_holder) → `logos.lang.hermes.*`

One batch commit.

### Step 3 — Lang-tier text + str

- Split `std.lang.text` into `logos.lang.str` (read) and `logos.mem.string` (owned).
- `regex/` separately per catalog.

Per-file analysis required — text has 3 files + regex/ subdir.

### Step 4 — View / anyval / typed_value / scalar / relptr_traits

These need careful per-file split (struct + read methods in lang; owning methods in mem). Treat one at a time with full ctest between.

### Step 5 — stringify / pat / check / equal / hashing / hbs_read

**Blocked.** Tried 2026-05-17 — these packages access raw fields
(`map.size`, `map.keys`, `map.vals`) on `Map<Bitmap, AnyVal>` and
similar partial specialisations of structs that live in mem-tier
(std.hermes.map). When the file moves to lang-tier and the Map
struct stays in monolith, mono fails to materialize the specialised
fields from the binary AST:

    mlir_gen: struct 'std.hermes.map.Map$G2$Bitmap$AnyVal'
              has no field 'size'

This is a real compiler limitation — cross-archive specialisation
of structs with explicit field access is not yet supported.

### Step 6 — mem-tier batch (rename to logos.mem.hermes.*)

**Attempted 2026-05-17, reverted.** Renaming the 21 mem-tier
packages (string/array/map/objectmap/ctr/document/parser/clone/
hbs_write/decimal/tag_system/registry/alloc/release plus
stringify/equal/hashing/check/pat/hbs_read/view-trait-surface)
was straightforward as a sed, but the build broke on two distinct
fronts:

1. **Duplicate datatype clobber.** Monolith's recursive glob root
   (`stdlib/`) absorbs `stdlib/mem/hermes/*` into liblstdlib.a
   while layer-mem's text walk does the same. When layer-mem
   builds, its transitive `use` triggers monolith binary load
   via the `pkg_in_prelude` auto-load mechanism — which pulls in
   the duplicate `logos.mem.hermes.map.Map` from monolith, racing
   the text-walk's own definition. Sema: "duplicate datatype 'Map'".

   Attempted fix: dedup in `visit_binary_module` — skip prelude
   packages that the consumer's text-walk already has. The fix
   works for layer-mem but breaks metaprog JIT (which calls
   load_modules at runtime, and the filter excludes packages it
   needs from a transitive lookup angle).

2. **lang ↔ mem cycle.** lang has files with transitive mem deps:
   - `stdlib/lang/hermes/typed_value/` declares
     `struct TypedValue { type_off: RelPtr<HermesString>, ... }` —
     needs HermesString from mem.hermes.string for name resolution.
   - `stdlib/lang/hermes/fabric/` `use logos.mem.mem;` for alloc.
   - `stdlib/lang/hermes/relptr_traits/` impls mem-tier traits.
   - `stdlib/lang/any/` calls HermesRead methods (mem-tier trait).

   Currently these resolve because monolith builds FIRST (with no
   excludes) and absorbs both mem and lang content. Layer-lang
   loads its missing pieces from monolith binary. Activating
   excludes on monolith breaks this — monolith no longer has the
   mem content, and flipping build order (layers → monolith) means
   layer-lang's transitive mem deps can't resolve.

Two ways forward, both substantial:

- **(A)** Move the boundary-crossing lang files out of lang. They
  are mem-coupled in reality:
  - `any.logos` → `logos.mem.any`
  - `typed_value` → `logos.mem.hermes.typed_value`
  - `relptr_traits` → `logos.mem.hermes.relptr_traits`
  - `fabric` → split its mem-dependent pieces out

  Plus a refactor of `fabric.logos` to separate allocator-dependent
  helpers (OwningStorage with `alloc`) from pure lang surfaces.

- **(B)** Compiler patch: teach mono to instantiate cross-archive
  struct specialisations from the source's binary AST. Non-trivial
  — touches mono's struct-template-realisation pipeline.

Recommend either (A) as a careful per-file follow-up — accepting
that lang-tier is smaller than the original design imagined — or
(B) as the architecturally correct fix that unlocks both Step 5
and Step 6 cleanly.

### Post-mortem: (B) was misframed

Empirical follow-up 2026-05-17 disproved the audit's premise.
A minimal repro (a single function in lang-tier accessing
`Map<Bitmap, AnyVal>.header` from mem-tier source) compiles fine
under current code. Diagnostic dump of mono's `struct_templates_`
confirms the base `Map<K, V>` template IS loaded from monolith
binary AST when needed.

Re-analysis of the original "no field 'size'" error:
- `Map<Bitmap, AnyVal>` matches TWO partial specs:
  - `Map<Bitmap, V>` → fields `{header, schema_type_code, data}`
  - `Map<K, AnyVal>` → fields `{size, capacity, keys, vals}`
- `find_best_struct_spec` (mono_clone.cpp:3691) picks the more
  specific via `specificity_vec`, but ties exist for {1 pinned,
  1 free} on both. Tie-break appears stable single-archive.
- Mangling collapses both into `Map$G2$Bitmap$AnyVal` — one
  registration wins last-write. When `impl HermesStringify for
  Map<Bitmap, V>` is processed cross-archive, the surviving
  registration may have come from `Map<K, AnyVal>` (different
  field layout), so the impl's `map.size` access fails.

This isn't a cross-archive bug, it's a **partial-spec mangling
collision** that monolith builds happen to resolve consistently
(single mono pass with stable iteration order). Step 5/6 split
exposed it because cross-archive load order changes which spec
gets registered first.

The actual fixes for the Hermes split are therefore:
- **(C) Loader-level dedup** — when monolith absorbs mem/ AND
  layer-mem text-walks the same package, prevent the auto-load
  prelude path from re-registering. Attempted as a `wanted()`
  predicate update, but broke metaprog JIT (which also calls
  load_modules at runtime with different visibility expectations).
  Needs careful per-call-site analysis.
- **(D) Mangling fix** — disambiguate concrete specialisations
  by the chosen partial-spec identity, not just type args. E.g.
  `Map$G2$BitmapPart$Bitmap$AnyVal` vs `Map$G2$AnyValPart$Bitmap$AnyVal`.
  Avoids collision but ripples through dispatch tables and the
  trait-impl registry.
- **(A) Surgical moves** still works as a workaround for the
  immediate Step 5/6 deadlock, without touching the underlying
  partial-spec bug.

Recommend (C) as the cleanest fix for the loader-side duplicate
issue, but the metaprog JIT interaction needs investigation
first. (D) is a deeper structural fix worth doing eventually for
correctness regardless of the split.

### Step 6 — Mem-tier remainder

`string`, `array`, `map`, `objectmap`, `ctr`, `document`, `parser`,
`clone`, `hbs_write`, `decimal`, `tag_system`, `registry`, `alloc`
all migrate to `logos.mem.hermes.*`. Most have heavy interconnect
but all stay in the same tier.

### Step 7 — Unblock circulars

- Move `std.lang.any` (was reflect) — its `use std.hermes.view` becomes
  `use logos.lang.hermes.view` (now lang-side). No circular.
- Move `std.lang.cmp` — same for text dependency.

### Step 8 — Activate excludes + resume Phase 4

Now that layer-lang stands alone, add `exclude lang|mem|std-new` to
the monolith manifest. Verify cold build: monolith stops absorbing
the layer trees; its 37 `use logos.lang.X;` files resolve via the
binary archives that built in earlier steps. Flip CMake dependency
order so monolith builds AFTER the layer archives (currently it
builds first and absorbs them).

Then collections, fmt, io, sync, persistent, compiler etc. migrate
cleanly because layer-lang truly stands alone with no monolith
dependency.

---

## Open audits (verify before committing each step)

- **Orphan rule:** does Logos enforce "impl methods only in the type's
  origin package"? Affects view/anyval split strategy. Test with a
  small `impl HermesView { fn extra() }` in a different package.
- **`std.hermes.alloc`:** allocator-agnostic interface or always-heap?
  If interface, lang; if always-heap, mem.
- **`std.hermes.decimal`:** does Decimal arithmetic allocate? Methods
  like `decimal_to_string()` clearly do (mem); core decimal ops might
  not (lang).
- **`std.hermes.hbs_read`:** does the parser ever allocate, or always
  read into pre-allocated structures? Comment says read-only but
  verify.
- **Pure-read AnyVal methods:** which `AnyVal::as_X()` methods just
  cast, which allocate? Splits at method granularity.

---

## What this doesn't do

- Doesn't address the rust-import classification (own vs imported/) —
  that's Phase 5.
- Doesn't add new Hermes features. Pure rearrangement of existing code.
- Doesn't formalize the manifest `tier` enforcement — Phase 6.

---

## Estimated effort

| Step | Estimate |
|---|---|
| 0+0.5 | Half a session (excludes plumbing + cycle break decision) |
| 1 | One session (4 leaves) |
| 2 | Half a session (lifecycle layer) |
| 3 | One session (text split — needs care, 396 consumers) |
| 4 | One-two sessions (per-file splits in view/anyval/typed_value) |
| 5 | One session (lang-tier remainder) |
| 6 | One session (mem-tier remainder) |
| 7 | Half a session (resume any/cmp) |
| 8 | Multiple sessions (resume Phase 4 — collections/fmt/etc.) |

**Total to unblock Phase 4: ~5-7 sessions.** Then Phase 4 finishes in
~3-5 more sessions per current pace (28 packages remaining outside
Hermes).

---

## Session 2 (2026-05-17) — "all-Hermes-in-lang" attempt

Per Victor: pragmatic three-layer with EVERYTHING Hermes-related
(including mem-tier containers + stringify/equal/etc. trait surface)
bundled into lang, and `--no-alloc` enforced at metaprog level
(variable captures in `@{...}` rejected when --no-alloc set). This
bypasses the (C) and (D) blockers since all Hermes code ends up in
a single archive.

Attempted batch:
- 21 hermes packages: std.hermes.X → logos.lang.hermes.X
- view split merged back (struct file + trait surface in one package)
- mem.mem → split into minimal logos.lang.mem (allocator + byte ops
  for Hermes) and logos.mem.mem (generic swap/replace/take helpers)
- All consumers swept (~500+ files), compiler refs updated
  (sema_stmt.cpp helper hints, sema_expr.cpp metacall thunk template,
  module_loader.cpp pkg_in_prelude prefix list)

Encountered, did not resolve cleanly in one session:
- **Trait visibility in multi-file packages.** After merging view's
  structs (view.logos) and traits (view_traits.logos) into one
  package — even keeping them in one file — `impl HermesRead for
  Hermes` in ctr.logos failed with "unknown trait 'HermesRead'",
  despite ctr having `use logos.lang.hermes.view;` and the trait
  declared in the same imported package. Needs targeted sema
  debugging.
- **Stale binary AST interference.** Old liblstdlib.a content
  cached under std.hermes.X names lingered through partial
  rebuilds and confused symbol lookup.
- **Circular import surface.** view ↔ stringify (and friends)
  cycles surface differently when both packages live in the same
  layer vs. spread across monolith.

Reverted; baseline (Steps 0-4, 3197/3197) preserved. The
all-in-lang approach is architecturally sound but requires
focused trait-visibility / multi-file-package sema debugging
before it can be safely batched. Recommended next session:

1. Reproduce trait-visibility issue with minimal 2-file test fixture.
2. Walk sema_collect's handling of multi-file packages to find the
   gap.
3. Fix in sema (or document the constraint as "trait must be in
   single-file package").
4. Re-attempt the all-in-lang move once sema is reliable.

---

## Session 3 (2026-05-17) — collections-to-mem + ROOT CAUSE found

Per Victor: physical move of mem/ packages into stdlib/lang/ (keeping
their logos.mem.* names) so layer-lang archive bundles everything;
layer-mem manifest stays as placeholder. Then add collections
(`std.collections.X` → `logos.mem.collections.X`, physically in
`stdlib/mem/collections/`) so layer-mem has real content.

Attempted. Layer-mem build failed with the now-familiar pattern:

    error [impl Iterator for VecIter]: impl: unknown trait 'Iterator'

But this time, careful instrumentation pinpointed the actual bug.

### Instrumentation findings

Added diagnostic prints to:
- `module_loader.cpp` — log every binary-loaded ParsedModule
- `sema_collect.cpp` — log every trait registration with `cur_from_binary_`

Output for layer-mem build:

    [loader diag] binary load: pkg='logos.lang.iter' path='.../iter/iter.logos'  ×5
    [sema diag] register trait 'Iterator' from pkg 'logos.lang.iter' (from_binary=0)
    error [impl Iterator for VecIter]: impl: unknown trait 'Iterator'

Two anomalies:
- iter.logos loaded 5 times (one per consumer entry file — wasteful
  but not buggy)
- **`from_binary=0` for trait Iterator** — but iter.logos is
  unambiguously a binary-loaded module per loader diag

### Root cause

`SemaChecker::cur_from_binary_` is not set when sema processes
binary-loaded modules. Downstream the `only_binary_vec` filter at
sema.cpp:489 keeps only entries with `from_binary_module=true`.
Since binary-loaded traits register with `from_binary_module=false`,
they get filtered out of the lir_bundle binary cache, and the final
user-sema pass that consumes the bundle has empty `traits_` for
binary-loaded traits → "unknown trait" errors.

**This is the SAME bug** that surfaced in:
- Session 1's Step 5 attempt (cross-archive Map trait field access)
- Session 2's all-Hermes-in-lang trait visibility issue
- Session 3's collections-to-mem layer-mem build

One bug. Three failure modes. Localised in sema's
`cur_from_binary_` propagation when binary modules feed
sema_collect.

### Fix path (next session, ~1 session scope)

1. Find where binary-loaded ParsedModules feed sema_collect (likely
   `lower_program` in sema.cpp or wherever sema iterates `asts`).
2. Set `cur_from_binary_=true` for those iterations based on
   `ParsedModule::from_binary_module` flag.
3. Verify trait registrations get `from_binary=1` post-fix.
4. Re-attempt collections-to-mem; expect layer-mem to build cleanly.
5. Re-attempt all-Hermes-in-lang; expect ctr's `impl HermesRead for
   Hermes` to find HermesRead trait.

Once this lands, all the previously-blocked three-layer split
operations should be unblocked.

3197/3197 baseline preserved. Three sessions of debugging
produced a precise actionable diagnosis.

---

## Session 4 (2026-05-17) — (B) fix landed + (D) partial-spec bug now blocks Step 5

### Fix landed

Implemented the (B) fix surfaced in session 3:
- emit_module.cpp now propagates `ParsedModule::from_binary_module`
  into the `from_binary` vector that feeds `compile_to_object`'s
  sema runs (pre-fix it was hard-coded all-false for binary-loaded
  modules, only ast_only flag fed it).
- sema_collect.cpp pass0 pre-registers trait NAMES alongside the
  existing struct/enum/datatype name pre-registration. Pass2's
  `collect_impl` can now resolve `impl Trait for X` regardless of
  iteration order between the impl's file and the trait's file.

This was the actual root cause discovered in session 3 (the
from_binary flag was real but tangential; the load-bearing issue
was pass2 trait registration happening AFTER impl registration in
iteration order).

**Demonstrated:** collections migrated from `std.collections` to
`logos.mem.collections` and layer-mem now builds with real content.
143 consumer files swept. 3197/3197.

### (D) partial-spec mangling collision — still open

Re-attempted Step 5 (stringify/equal/hashing/check/pat/hbs_read →
lang) with the (B) fix in place. Trait dispatch now works, but
mlir-gen fails for `Map<Bitmap, AnyVal>`:

    mlir_gen: struct 'std.hermes.map.Map$G2$Bitmap$AnyVal' has no field 'size'

Map<...> has TWO matching partial specs for that concrete
instantiation:
- `struct Map<Bitmap, V>` → fields {header, schema_type_code, data}
- `struct Map<K, AnyVal>` → fields {size, capacity, keys, vals}

`find_best_struct_spec` picks one (by specificity). Mono materializes
ONE spec under the mangled name `Map$G2$Bitmap$AnyVal`. Impls on the
OTHER spec (equal/hashing/etc.'s `impl Map<K, AnyVal>`) access fields
absent in the picked spec → mlir-gen error.

In monolith builds, this still works (3197/3197 there). Either
monolith mono picks differently per impl-context, or impl-side field
resolution uses the impl's self-type rather than the canonical struct
registration. Needs investigation.

### Path forward

To unblock Step 5/6/all-Hermes-in-lang:
- **(D1)** Fix `find_best_struct_spec` / mono materialization so each
  impl gets fields from its own declared partial spec. Likely the
  right architectural fix.
- **(D2)** Mangle partial-spec instantiations distinctly (e.g.
  `Map$G2$BitmapPartial$Bitmap$AnyVal` vs
  `Map$G2$AnyValPartial$Bitmap$AnyVal`). Touches dispatch tables.

Both ~1-session compiler work. After (D), Step 5 becomes a sed.

### Sessions to date

| # | Date | Outcome |
|---|------|---------|
| 1 | 05-16 | Steps 0-4 landed (lang carve-out: leaves/lifecycle/text/scalar/view-structs/StringView/anyval/typed_value/relptr_traits) |
| 2 | 05-17 | Step 5+6 attempts blocked, root cause hypothesis |
| 3 | 05-17 | Instrumentation pinpointed bug: from_binary + pass2 order |
| 4 | 05-17 | (B) fix landed + collections-to-mem; (D) now the next blocker |

---

## Session 5 (2026-05-17) — (C) loader dedup landed; (D) needs mono investigation

### (C) loader fix landed

Re-attempt of the "skip prelude auto-load when text has package" fix
from session 3. Narrower predicate (only filter by `index.count()`,
NOT `visited_packages.count()`) avoids the metaprog JIT regression
that broke session 3's attempt. Verified at 3197/3197 baseline.

### (D) still blocks Step 5

With (B) trait pre-registration + (C) loader dedup both in place,
Step 5 (hermes read-side packages → lang) progresses past sema to
mlir-gen and hits the partial-spec issue:

    mlir_gen: struct 'std.hermes.map.Map$G2$Bitmap$AnyVal' has no field 'size'
              (in fn 'Map$G2$Bitmap$AnyVal__check_obj__g__ref_Map$G2$Bitmap$V__pmut_CheckState')
    mlir_gen: struct 'Map$G2$Bitmap$AnyVal' has no field 'keys'
              (in fn '...__check_obj__g__ref_Map$G2$K$AnyVal__pmut_CheckState')

Diagnostics revealed:
- 3 partial Map specs declared: `Map<K, V>` empty base, `Map<Bitmap, V>`
  {header, schema_type_code, data}, `Map<K, AnyVal>` {size, capacity,
  keys, vals}, `Map<Varchar, AnyVal>` {entries_off, capacity, count,
  reserved}.
- For `Map<Bitmap, AnyVal>`, `find_best_struct_spec` picks
  `Map<Bitmap, V>` (specificity wins). Struct registered with
  {header, data} fields.
- check.logos has BOTH `impl<V> HermesCheck for Map<Bitmap, V>` and
  `impl<K> HermesCheck for Map<K, AnyVal>` — neither bound on K/V.
- Mono instantiates BOTH for K=Bitmap, V=AnyVal. The Map<Bitmap, V>
  impl body accesses {header, data} (OK). The Map<K, AnyVal> impl
  body accesses {size, keys, vals} which the registered struct
  doesn't have → mlir-gen error.

In monolith builds this works (3197/3197 there). Either:
- monolith's mono skips the Map<K, AnyVal> instantiation for K=Bitmap
  (some bound or impl-coherence rule fires that doesn't in cross-archive)
- or monolith's mlir-gen does per-impl struct field lookup

Needs targeted mono investigation. The fix is either:
- **(D1)** Per-impl struct field resolution: each impl's body
  reads fields from ITS OWN declared partial spec, not the
  globally-picked one.
- **(D2)** Mono coherence check: when multiple impls of same trait
  apply to a concrete type, instantiate ONLY the one whose
  self-type matches the winning struct spec.

Both ~1-session compiler work.

### Sessions to date

| # | Date | Outcome |
|---|------|---------|
| 1 | 05-16 | Steps 0-4 landed |
| 2 | 05-17 | Step 5+6 attempts blocked, root cause hypothesis |
| 3 | 05-17 | Instrumentation pinpointed bug |
| 4 | 05-17 | (B) fix landed + collections-to-mem |
| 5 | 05-17 | (C) loader dedup landed; (D) partial-spec mangling = next blocker |
