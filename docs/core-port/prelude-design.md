# Prelude Design

Authoritative contents of `logos.lang.prelude`, `logos.mem.prelude`,
and `logos.std.prelude`. Companion to
[three-layer-split.md](three-layer-split.md) and
[layer-assignment.md](layer-assignment.md).

Status: **modules landed + R1/R2 default-on blockers fixed (2026-05-21);
default-on still gated on R3 (B-mv-02 trait-collision).**

Implementation progress:
- ✅ Injection mechanism (manifest `prelude` directive, `#![no_implicit_prelude]`
  grammar/sema, loader + sema wildcard injection) — already wired.
- ✅ The three prelude modules `logos.{lang,mem,std}.prelude` now exist
  (`stdlib/{lang,mem,std}/prelude/prelude.logos`, commit 980e6b11) and work as
  EXPLICIT one-line imports: `use logos.lang.prelude;` brings Option/Some/None/
  Result/Ok/Err + core traits; `use logos.mem.prelude;` adds String/Vec; the
  3-level transitive re-export (std→mem→lang→option) resolves.
- ✅ IMPLICIT injection MECHANISM works (verified 2026-05-21). It must be wired
  at THREE sema entry points in `main.cpp` (not one): the loader
  (`load_modules(..., implicit_prelude)` → dependency edge), the metaprog
  dispatch (`run_metaprog_dispatch`'s `meta_opts.implicit_prelude`), and the
  FINAL non-metaprog `sema_lower` (`default_opts.implicit_prelude`). The last is
  essential: the metaprog passes silently DEFER unknown types (sema.cpp:4114),
  so the real type-resolution happens (and previously failed) in the final pass.
  With all three set, a zero-`use`-line program compiles + runs: `Option`/`Some`/
  `None`/`match`/`String` resolve via `effective_import_pkgs()`'s transitive
  `pub use` expansion (std→mem→lang→option). Confirmed end-to-end.
- 🟡 DEFAULT-ON regressed **99 / 3586** (2026-05-21). Diagnosed into THREE
  independent roots (NOT one); two now fixed:
  - **R2 FIXED** (commit 47bab8c1) — consumer template-lowering produced
    type-param→`<error>`: sema struct-lit type-arg inference didn't recurse into
    a type-param NESTED inside a compound field (`HashMapKeys<K,u8>`,
    `fn(T)->bool`), so with no return hint (template / force-lower path) it fell
    to `error_t()` → `Foo<<error>>`. Added recursive `unify_field_tv`. Fixes
    HashSet__iter / RevIter__filter / MapIter__filter / FilterIter.
  - **R1 FIXED** (commit 962f53c3) — loader over-drag: `visit_binary_module`'s
    `pkg_in_prelude` dragged the WHOLE `logos.mem.*` layer (collections/encoding/
    mem.hermes) out of liblogos-mem.a whenever a compile touched Vec/String, then
    re-lowered it every compile (perf regression ~26%) and force-lowered its
    generics in trait-bound/HRTB programs (the `ObjectMap::init` / `Array__equal`
    artifacts). Narrowed `pkg_in_prelude` to `logos.lang.*` only — the genuine
    no-`use` cross-cutting foundation. Mem packages now load only via the
    use-closure.
  - Combined: prelude-on 99 → **60**; base suite 3586/3586; the entire
    force-lower-artifact cluster gone.
  - **R3 STILL OPEN** (last blocker for default-on) — ~45 tests redefine prelude
    trait names (Drop/Default/From/Into/Add/Div/PartialEq) → "trait X defined in
    both packages" (B-mv-02 cross-pkg same-name trait clobber). Fix B-mv-02
    (local-shadows-prelude) OR migrate the tests to drop their redefinitions.
    Then update 6 negative-test `.expected`, triage 2 pre-existing stragglers
    (vec_usage runtime; generic_method_infer_struct mono), re-apply the 3-point
    wiring + tier-awareness + keep stdlib emit_module opted-out.

---

## Design rules

1. **Append-only, no versioning.** Adding to a prelude is safe;
   removing or renaming is a hard cut (no `v1`/`v2` shims).
   Matches the HARD RULE for tier surfaces in
   `discuss_runtime_stdlib_tiers_2026_05_07.md`.
2. **Higher prelude depends on lower prelude.** `logos.mem.prelude`
   declares `pub use logos.lang.prelude;`; `logos.std.prelude`
   declares `pub use logos.mem.prelude;`. Consumer sees the
   transitive surface.
3. **Tier-aware injection.** Manifest `prelude <pkg>` directive
   selects which prelude is implicitly injected. Default per tier:
   - `tier lang` → `logos.lang.prelude`
   - `tier mem` → `logos.mem.prelude`
   - `tier std` (default) → `logos.std.prelude`
4. **Opt-out.** `#![no_implicit_prelude]` skips injection
   entirely (used by the prelude packages themselves + by
   stdlib-internal files that would create import cycles).
5. **Explicit `use` beats prelude.** A file-level `use ...` for
   a name that's also in the prelude resolves to the explicit
   one (Rust convention).
6. **No macros in prelude (initially).** Rust's preludes include
   built-in macros (`println!`, `vec!`, etc.). Our metacall-based
   macros don't need preluding — they're invoked via metacall
   syntax which doesn't conflict with name resolution. Revisit if
   user pressure surfaces.

---

## logos.lang.prelude

Re-exports the no-alloc, no-OS surface that every Logos file
needs by default.

```
package logos.lang.prelude;

// Marker traits
pub use logos.lang.marker;       // Copy, Sized, Send, Sync, Unpin

// Operator + drop traits
pub use logos.lang.ops;          // Drop, Fn, FnMut, FnOnce
                                 // (post-merge: also Index, Deref,
                                 //  Add/Sub/Mul/Div/Rem,
                                 //  Range/RangeFrom/RangeTo)

// Conversion
pub use logos.lang.convert;      // AsRef, AsMut, From, Into, TryFrom, TryInto

// Default
pub use logos.lang.default;      // Default

// Cloning + equality + ordering
pub use logos.lang.clone;        // Clone
pub use logos.lang.cmp;          // PartialEq, Eq, PartialOrd, Ord, Ordering
                                 // (post-merge: ord/cmp combined)

// Hashing
pub use logos.lang.hash;         // Hash, Hasher

// Iteration
pub use logos.lang.iter;         // Iterator, IntoIterator, Extend,
                                 // DoubleEndedIterator, ExactSizeIterator

// Option / Result + bare variants
pub use logos.lang.option;       // Option, Some, None
pub use logos.lang.result;       // Result, Ok, Err

// Free fns from lang.mem
pub use logos.lang.mem.drop;     // drop(value) — explicit-drop fn
                                 // (NOTE: Drop trait is in lang.ops; drop
                                 //  fn lives separately in mem-like
                                 //  module within lang for symmetry with
                                 //  Rust core::mem::drop)
```

### Items NOT in lang prelude (intentional)

- `Box`, `Rc`, `Arc` — alloc-tier (require heap).
- `String`, `Vec`, `HashMap`, etc. — alloc-tier.
- `format!`, `ToString` — alloc-tier.
- IO traits, time, threads — std-tier.
- All Hermes types except `HermesStatic`/`HermesView` types
  (those are accessible via `use logos.lang.hermes.view;` —
  not implicit because most lang-tier files don't touch them).

---

## logos.mem.prelude

Re-exports the heap-allocating surface on top of lang.

```
package logos.mem.prelude;

// Lang prelude — implicit transitive surface
pub use logos.lang.prelude;

// Owning smart pointers
pub use logos.mem.boxed;         // Box
pub use logos.mem.rc;            // Rc
pub use logos.mem.sync;          // Arc (mirrors Rust's alloc::sync::Arc)

// Owning string + conversion traits
pub use logos.mem.string;        // String, ToString, ToOwned

// The one heap container that's prelude-worthy
pub use logos.mem.vec;           // Vec

// Format runtime (companions to lang.fmt traits)
// (NOTE: only Display/Debug TRAITS are in lang.prelude indirectly via lang.fmt;
//  format!() runtime + the Formatter type are mem-only.)
```

### Items NOT in mem prelude (intentional)

- `HashMap`, `BTreeMap`, `HashSet`, `VecDeque` — used commonly
  enough to consider, but Rust's prelude doesn't include them
  (Rust users `use std::collections::HashMap`). Following Rust.
- Hermes mutable types (`MemHolder`, `Zone`, `Own`) — explicit
  `use` always. Too domain-specific for blanket prelude.
- Persistent data structures — explicit `use`.

---

## logos.std.prelude

Re-exports the OS-touching surface on top of mem. Currently
**identical to mem.prelude** in items — Rust's std prelude
adds nothing of its own beyond what's already from alloc/core.
File exists so the layered structure stays uniform (consumer
selects `logos.std.prelude` and gets everything).

```
package logos.std.prelude;

pub use logos.mem.prelude;
// (No std-only additions at this time. Future additions —
//  e.g. `println!` macro shorthand if/when we add an OS-bound
//  print macro that should be implicit — append here.)
```

---

## Implementation notes (Phase 3)

### Manifest directive

```
# in logos.module
module logos-lang
tier lang
prelude logos.lang.prelude
```

The `prelude` directive is optional — if absent, the tier default
applies. Useful when a custom prelude (e.g. for a test module
that wants a hermetic subset) is desired.

### Sema implicit-use injection

In `sema_collect.cpp`, during the per-file collect phase:

```
for each file in module:
    if file has #![no_implicit_prelude]:
        continue
    prelude_pkg = file's module's manifest.prelude
    if prelude_pkg is empty:
        prelude_pkg = default_prelude_for_tier(manifest.tier)
    inject `use <prelude_pkg>;` as implicit at AST head
```

The injection happens BEFORE the file's own `use` statements are
processed, so explicit `use` resolution wins on name conflicts.

### Files that must carry `#![no_implicit_prelude]`

- `logos.lang.prelude`, `logos.mem.prelude`, `logos.std.prelude`
  themselves (would re-export their own re-exports otherwise).
- Anything in `logos.lang.marker`, `logos.lang.ops`,
  `logos.lang.option`, `logos.lang.result`, `logos.lang.iter`,
  `logos.lang.cmp`, `logos.lang.clone`, `logos.lang.convert`,
  `logos.lang.default`, `logos.lang.hash` — they define the
  items that the prelude re-exports, so importing the prelude
  in them creates a cycle.
- Verified per-file during Phase 4 — circular `use` is a clear
  diagnostic from the existing module loader.

### Replacing compiler hardcodes (Phase 7 cleanup)

Once prelude is the canonical source of well-known type names,
the following hardcoded lookups in the compiler can be replaced:

- `sema_expr.cpp` — `find_enum_by_name("Option")` for Some/None
  shorthand → resolve through active prelude.
- Similar for `Result`/`Ok`/`Err`, `Vec`, `Box`, `String`.
- Mangling references to `Iterator`, `Display`, `Debug`, etc. can
  stay as literal strings (they're trait names, not subject to
  user shadowing).

Migration is per-call-site; do it after prelude is stable and
end-to-end-tested.

---

## Comparison to Rust

Differences from `core::prelude::v1` / `alloc` / `std::prelude::v1`:

| Item | Rust | Logos |
|---|---|---|
| Async traits (`AsyncFn`, `Future`, etc.) | In prelude | **Absent** — async deferred indefinitely |
| `size_of`/`align_of` fns | Added 1.80+ | Add later or expose via `metacall` |
| Built-in macros (`println!`, `vec!`, ...) | In prelude | **Absent** — invoked via metacall syntax |
| `derive` macro names | In prelude | **Absent** — metacall handles derives |
| `Hash` macro | In prelude | **Absent** — `#[derive_hash]` metacall instead |
| `Box`, `String`, `Vec`, `ToString`, `ToOwned` | std-prelude (from alloc) | mem-prelude (visible at mem tier and above) |
| `HashMap` / `BTreeMap` | Not in prelude (`use std::collections::*`) | Same — not in prelude |

The Logos preludes are intentionally smaller and more focused
than Rust's — no macro re-exports (we route through metacall),
no async surface, and `Box`/`Vec` are mem-tier rather than only
std-tier (since they're available the moment heap is).
