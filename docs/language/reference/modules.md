# Modules & Visibility

A Logos compilation unit is a tree of *packages*. Each `.logos` file declares its package via `package <dotted.path>;` on the first non-comment line. Multiple `.logos` files can share a package — the compiler treats them as one logical unit.

This page covers:
- The `package` / `use` / `pub` / `pub use` triad
- Multi-file packages and the `logos.module` manifest
- Name-resolution algorithm (the order in which a bare name is resolved)
- Re-export propagation and transitive visibility
- Common pitfalls (cross-package collisions, import-order ambiguity)

## Sources

- Grammar: [tools/peg_gen/grammars/logos.peg](../../../tools/peg_gen/grammars/logos.peg) — `module`, `any_use_decl`, `pub_use_decl`, `use_decl`, `path_dot_ident`
- Sema: [src/compiler/sema_collect.cpp](../../../src/compiler/sema_collect.cpp), [src/compiler/sema_impl.hpp](../../../src/compiler/sema_impl.hpp) (`cur_package_`, `cur_imports_`, `pkg_reexports_`, `find_*_by_name`, `effective_import_pkgs`, `check_pub_access`)
- Manifest: [src/compiler/module_manifest.cpp](../../../src/compiler/module_manifest.cpp), [src/compiler/module_loader.cpp](../../../src/compiler/module_loader.cpp)

## File-level form

```logos
package foo.bar.baz;     // one per file; first non-comment statement

use std.lang.cmp;        // make all `pub` items of std.lang.cmp visible
use other.module;
pub use std.hermes.view; // re-export to consumers of foo.bar.baz

// items follow…
pub struct Quux { … }
fn helper() -> i32 { … }
```

The grammar:

```peg
module        <- KW_PACKAGE IDENT path_dot_ident* SEMI any_use_decl* item*
any_use_decl  <- pub_use_decl / use_decl
pub_use_decl  <- KW_PUB KW_USE IDENT path_dot_ident* SEMI
use_decl      <- KW_USE IDENT path_dot_ident* SEMI
```

A package path is one or more dotted identifiers. Both `package` and `use` accept the same form. There is no leading-dot or relative-path syntax; all paths are absolute.

## Visibility

Three default visibilities cover all items:

| Form | Scope |
|---|---|
| (no marker) | Private to the package. Not visible from importers. |
| `pub` | Public. Importers of the package see this item. |
| `pub(crate)` / `pub(super)` | **Not implemented**. Logos has no sub-package privacy modifier today. |

`pub` applies to: `pub fn`, `pub struct`, `pub enum`, `pub eidos`, `pub trait`, `pub genos`, `pub use`, `pub const`, `pub type`, `pub instantiate`, `pub static fn`. Struct fields use a separate `pub` marker per field; trait methods are public-by-default within a public trait.

The visibility check fires inside `find_*_by_name`:

```cpp
// sema_collect.cpp:343
void check_pub_access(bool is_pub, const std::string& def_package,
                      std::string_view item_name) {
    if (is_pub || def_package.empty() || cur_package_.empty()) return;
    if (def_package != cur_package_)
        error(std::format("'{}' is private to package '{}'", item_name, def_package));
}
```

A non-`pub` item is silently usable inside its own package and rejected with a clear error from any other package. Items with empty `def_package` (host-injected types, primitives) bypass the check.

## Multi-file packages: `logos.module` manifests

A directory of `.logos` files becomes a multi-file package via a sibling `logos.module` manifest:

```
module foo.bar
version 0.1
root path/to/source/dir
depends std.hermes
exclude tests/
```

Recognized directives (parsed in [src/compiler/module_manifest.cpp](../../../src/compiler/module_manifest.cpp)):

| Key | Required | Effect |
|---|---|---|
| `module` | yes | Package name. Every file under `root` must declare `package <module>;`. |
| `root` | yes | Filesystem path to the source tree (may be relative to manifest). |
| `version` | no (default `0.0`) | Informational. |
| `depends` | no, multi | Other manifests this module imports as a unit. Repeat `depends`. |
| `exclude` | no, multi | Glob/path to skip when scanning `root`. Repeat `exclude`. |

Lines starting with `#` are comments. Unknown keys are ignored (forward-compat).

A package built from a manifest produces a single binary archive (`libfoo.a` + `foo.hermes` interface metadata) — see [feat_modules_delivery](../../../.claude/projects/-home-victor-devel-logos/memory/project_modules_delivery.md). Consumers `use foo.bar;` and the compiler links against the archive plus the metadata.

## Name resolution

A bare identifier `Foo` (struct/datatype/enum/trait) is resolved by the four-step procedure in [src/compiler/sema_impl.hpp](../../../src/compiler/sema_impl.hpp):

```
1. Try cur_package_                 (sema_key(cur_package_, "Foo") in structs_/datatypes_/…)
2. Try effective_import_pkgs()      (each direct `use`d pkg + transitive pub-use)
3. Try unqualified key              ("Foo" with empty pkg — host-injected types only)
4. Fail — emit "unknown type 'Foo'" diagnostic
```

`effective_import_pkgs()` walks `cur_imports_.wildcard_packages` (the directly `use`d packages) and follows the `pkg_reexports_` graph transitively, deduping with a visited-set.

```cpp
// sema_impl.hpp:824
std::vector<std::string> effective_import_pkgs() const {
    std::vector<std::string> result; StrSet visited;
    for (auto& pkg : cur_imports_.wildcard_packages) {
        if (visited.insert(pkg).second) {
            result.push_back(pkg);
            collect_reexports(pkg, visited, result);
        }
    }
    return result;
}
```

### First-import-wins

Step 2 iterates packages in `use` order. **The first package that contains the name wins**; later same-name imports are silently shadowed. There is no ambiguity diagnostic. Consequences:

- If `pkg_a` defines `struct Foo` and `pkg_b` also defines `struct Foo`, then `use pkg_a; use pkg_b; let f: Foo = ...;` resolves to `pkg_a::Foo`. Reordering to `use pkg_b; use pkg_a;` would resolve to `pkg_b::Foo`.
- Currently no syntax exists to disambiguate (`pkg_b.Foo` qualified path is **not** parsed as a type-ref). Renaming or alias-import (`use pkg_b.Foo as FooB;`) is also **not** supported.
- `find_*_by_name` does correctly carry the resolved package on the returned `TypeRef.pkg_name` — so downstream sema sees the right pkg, even though source-level disambiguation isn't possible. See [feat_type_uid_pkg_skip_bug](../../../.claude/projects/-home-victor-devel-logos/memory/feat_type_uid_pkg_skip_bug.md) for the multi-month arc that surfaced this asymmetry.

### Transitive re-exports

`pub use foo.bar;` inside package `baz` adds `foo.bar` to `pkg_reexports_["baz"]`. Consumers that `use baz;` then see all of `baz`'s own pub items **plus** all pub items of `foo.bar`, recursively. `collect_reexports` walks the chain depth-first and dedupes — cycles are tolerated.

Re-export edges propagate visibility but do **not** rename — a `pub use foo.bar.Quux;` in `baz` does NOT make `Quux` resolvable as `baz.Quux`; it just makes `Quux` visible to consumers of `baz` under the bare name `Quux`. (Aliasing `pub use ... as ...` is **not** implemented.)

### Item-vs-type lookup

The same algorithm applies to functions (`find_fn_by_name`), enums, traits, and modules-as-namespaces. There is no separate lookup table per kind — everything is package-qualified by the same `sema_key(pkg, name)` convention.

## What's NOT supported (intentionally or not)

- **Path-qualified type-refs**: `pkg_b.Foo` at type position does not parse. The grammar's `simple_type` accepts `IDENT (DOT IDENT)*` only for module-as-namespace function paths (e.g. `std.lang.cmp.compare(x, y)`), not for type names.
- **Alias imports**: `use foo.Bar as B;` — not parsed.
- **Sub-package privacy**: no `pub(crate)`, `pub(super)`, `pub(in path)`. Items are either fully private to their package or fully public.
- **Wildcard exclusion**: `use foo.{a, b};` and `use foo::*;` — not parsed (always wildcard implicitly).
- **Nested module declarations**: `mod sub { ... }` — Logos has no in-file sub-modules. Each package is exactly one file, or one directory under one manifest.

The roadmap for these features is in [docs/language/reference/roadmap.md](roadmap.md).

## Common pitfalls

- **Same-name structs in two imported packages** silently resolve to whichever was imported first (see "first-import-wins" above). The compiler doesn't warn. Until `pkg.Type` qualified syntax lands, the workaround is to ensure name uniqueness across imports.
- **`pub use` to lift internal helpers** is fine, but loops are tolerated only because of the visited-set guard; circular re-exports won't cause infinite recursion but will still bloat the search space and slow resolution.
- **A non-`pub` item leaks** if you forget the visibility marker. `find_*_by_name` will reach it from outside its package via step 3 (unqualified-bare fallback) only for host-injected items; user items always carry a non-empty `def_package`, so the check fires correctly.
- **Manifest `root` is interpreted relative to the manifest file**, not the build directory. A manifest at `stdlib/std/hermes/logos.module` with `root .` discovers files under `stdlib/std/hermes/`.

## See also

- [items.md](items.md) — item kinds and forward-declaration rules
- [generics-traits.md](generics-traits.md) — visibility of trait impls (currently global)
- [feat_type_uid_pkg_skip_bug](../../../.claude/projects/-home-victor-devel-logos/memory/feat_type_uid_pkg_skip_bug.md) — pkg-name threading across the compiler
- [project_modules_delivery](../../../.claude/projects/-home-victor-devel-logos/memory/project_modules_delivery.md) — binary module distribution format
