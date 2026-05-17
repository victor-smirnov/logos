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
of structs with explicit field access is not yet supported. Step 4
moves worked because they touched their own structs (HermesView,
AnyVal, TypedValue) or used method-dispatch (relptr_traits's
delegate-via-resolve pattern). Step 5 hits the limit directly.

Two ways forward:
- **(A)** Move all mem-tier container types (Map/ObjectMap/Array/
  HermesString/Decimal/...) to the same tier first (Step 6). Then
  re-attempt Step 5. Cross-archive issue is gone because the new
  layer.a contains both the impl AND the struct.
- **(B)** Compiler patch: teach mono to instantiate cross-archive
  struct specialisations from the source's binary AST. Non-trivial
  — touches mono's struct-template-realisation pipeline.

Recommend (A). Step 6 stands alone (renames only), and after it
Step 5 becomes a simple intra-tier move.

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
