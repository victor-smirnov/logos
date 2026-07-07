# Modules

Scope: package declarations, `use` imports (including the Logos-specific `use ... from <module>` form), visibility (`pub` / `pub(module)`), name resolution, the implicit prelude, module manifests, loading/ordering, symbol mangling, binary `.writ0` archives, export catalogs, and ABI compatibility. Source layers: grammar (`tools/peg_gen/grammars/logos.peg`), sema (`sema_collect` / `sema_impl` / `sema` / `sema_expr`), MLIR codegen (`mlir_gen_*`), and the module loader + manifest (`module_loader`, `module_manifest`). Each rule id below is reproduced verbatim from the extracted artifacts and is the permanent linkable address of that rule.

## Package declaration

### `module.package.decl` — Package declaration header

A compilation unit begins with `package NAME ('.' IDENT)* ';'`, optionally preceded by inner doc-comments (`//!`, `/*! */`) and inner attributes (`#![...]`). The dotted path gives the package's full name to arbitrary depth (first component = NAME, remaining components = PATH_PARTS). After the package line come zero-or-more use-declarations, then zero-or-more items.

```logos
package a.b.c;
```

```logos
//! crate doc
#![no_implicit_prelude]
package app;
```

**Divergence:** Rust uses no `package` header; module name is path-derived. Logos requires an explicit `package` line with a dotted package path.

**Source:** tools/peg_gen/grammars/logos.peg#L489-L490

### `module.package.decl-syntax` — package declaration

A file's package is declared by a leading `package <dotted-ident>;` statement, where `<dotted-ident>` is a sequence of `[A-Za-z0-9_.]` characters. The declaration may be preceded only by comments, blank lines, and inner attribute lines (`#![...]`); the first non-trivia token must be `package` or the file has no package declaration.

**Source:** src/compiler/module_loader.cpp#L403-L455

### `module.path.package-name` — Package name is dot-joined module path

A package's fully-qualified name is its module NAME with each PATH_PART name appended joined by `.` (e.g. `my.cool.pkg`).

**Divergence:** A9

**Source:** src/compiler/sema_collect.cpp#L731-L744

### `module.identity.package-dotted-name` — Module package is a dotted name; may be empty

Each module carries a dotted package name (e.g. `std.io`); a plain user program with no package declaration has an empty package name.

**Source:** src/compiler/module_loader.hpp#L19, src/compiler/module_loader.hpp#L17-L19

### `module.decl.package-required-for-import` — A `.logos` file is importable only if it declares a package

Only `.logos` files carrying a `package` declaration are indexed and thus reachable via `use`; a file with no package declaration is silently skipped and cannot be imported by name.

**Source:** src/compiler/module_loader.cpp#L1254-L1256, src/compiler/module_loader.cpp#L1217-L1219

## Module identity and mangle id

### `module.identity.empty-disables-qualified-mangling` — No owning module ⇒ no module-qualified mangling

A module's owning-MODULE identity is the unit of distribution it belongs to: `module_id` is the mangle key and `module_name` the canonical handle. A plain user program (no module) has empty identity, which disables module-qualified symbol mangling for its items.

**Source:** src/compiler/module_loader.hpp#L23-L30

### `module.id.explicit-or-derived` — Effective module id: explicit id else path hash

The effective module identifier used in symbol mangling is the explicit manifest `id` (sanitized) when set; otherwise it is derived as the FNV-1a 64-bit hash of the module's target install path, formatted as 'm' followed by 16 lowercase hex digits. The 'm' prefix guarantees the token never begins with a digit.

**Source:** src/compiler/module_manifest.cpp#L85-L98, src/compiler/module_manifest.hpp#L73-L79

### `module.id.mangle-legal-sanitize` — Module id sanitized to mangle-legal token

An explicit module `id` is sanitized into a mangle-legal token: every character that is not [A-Za-z0-9_] is replaced by '_'. The resulting token is what is baked into symbol mangling to disambiguate same-named packages from different modules/versions.

**Source:** src/compiler/module_manifest.cpp#L76-L83, src/compiler/module_manifest.cpp#L88

### `module.id.one-module-per-archive` — A binary archive carries a single owning module identity

Each binary archive declares at most one owning module via its embedded index header, giving a canonical module name and a mangling id; this identity is stamped onto every item decoded from the archive and is used downstream to qualify the items' symbols.

**Related:** `module.coexist.type-module-qualification`

**Source:** src/compiler/module_loader.cpp#L1533-L1548, src/compiler/module_loader.cpp#L1063-L1067

## `use` declarations

The brace-group form is disambiguated by the first character of the group head; three layers (grammar, loader, sema) state the same convention — `module.use.variant-shorthand-vs-subpackage`, `module.use.variant-vs-subpackage-by-case`, and the desugaring rules below are consistent restatements at each layer, not conflicts.

### `module.use.path` — Plain use declaration

`[pub] use pkg('.'IDENT)* ';'` brings a dotted package path into scope. `pub use` re-exports it. Path components after the head use a leading-dot separator (`.IDENT`).

```logos
use std.collections.HashMap;
```

```logos
pub use core.Option;
```

**Source:** tools/peg_gen/grammars/logos.peg#L500-L516, tools/peg_gen/grammars/logos.peg#L526-L527

### `module.use.dotted-path` — Dotted use path

`use a.b.c;` names the package whose dotted path is the concatenation of the head identifier and successive `.`-separated path parts: `a.b.c`. Package paths use `.` as the separator.

**Related:** `module.path.package-name`

**Source:** src/compiler/module_loader.cpp#L135-L158

### `module.use.resolution-on-search-paths` — `use` resolves a dotted package to a source file via search paths

A `use <pkg>;` declaration is resolved by locating the file for the dotted package name `pkg` within the configured search directories; failure to resolve any `use` is an error (B-mv-03/04) reported per declaration.

**Source:** src/compiler/module_loader.hpp#L126-L127, src/compiler/module_loader.hpp#L144-L145

### `module.use.duplicate-warn` — repeated use of same package warns

A `use pkg;` whose package is already in the module's wildcard import scope is a warning (duplicate import); it is otherwise a no-op.

**Source:** src/compiler/sema_collect.cpp#L176-L183

### `module.use.self-import-noop` — self-import is a no-op

`use P;` where P is the current module's own package is a no-op (own-package symbols always resolve first) and produces a redundancy warning.

**Source:** src/compiler/sema_collect.cpp#L184-L190

### `module.use.variant-shorthand-vs-subpackage` — use {..} disambiguated by first-character case

In `use pkg.Path.X.{V1, V2, ...};` the last dotted segment `X` disambiguates by its first character's case: uppercase ⇒ enum-variant bare-name shorthand import; lowercase ⇒ grouped sub-package import.

**Source:** tools/peg_gen/grammars/logos.peg#L309

### `module.use.variant-vs-subpackage-by-case` — Group target classified by first-character case

In a USE_VARIANTS group `use pkg.X.{...};`, the bracketed target `X` is classified by its first character: lowercase-leading `X` is treated as a grouped sub-package import (each member becomes `pkg.X.<member>`); uppercase-leading `X` is treated as an enum-variant import, importing the enclosing package `pkg` as a wildcard so the type is in scope. This relies on the convention that enum/type names are capitalized.

**Divergence:** Disambiguation by identifier capitalization is a Logos convention, not a Rust rule.

**Source:** src/compiler/module_loader.cpp#L167-L204

### `module.use.brace-group-import` — brace-group use desugars to per-item wildcard imports

`use pkg.{a, b, c};` (lowercase group head) desugars to wildcard imports `pkg.<head>.a`, `pkg.<head>.b`, `pkg.<head>.c` — bringing each listed package/item into wildcard scope. Distinguished from the enum-variant form by the lowercase first letter of the group head.

**Source:** src/compiler/sema_collect.cpp#L115-L143

### `module.use.brace-group-desugar` — `use pkg.{a, b, c}` with a lowercase head desugars to N wildcard imports

A grouped use whose head segment begins with a lowercase letter is treated as a package path: `use pkg.{a, b, c}` desugars to wildcard imports `pkg.a.*`, `pkg.b.*`, `pkg.c.*`. A head segment beginning uppercase is instead the enum-variant import form.

**Divergence:** note — Logos path model uses `.` for packages, `::` for items.

**Source:** src/compiler/sema.cpp#L6835-L6861

### `module.use.group-desugar` — use group desugars to N imports

`use pkg.{a, b, c};` (USE_GROUP / USE_VARIANTS form) desugars to N separate package imports, one per group member, where each lowercase-leading group prefix forms a sub-package: `use pkg.sub.{a,b}` yields imports `pkg.sub.a`, `pkg.sub.b`.

**Source:** src/compiler/module_loader.cpp#L159-L196

### `module.use.variant-shorthand` — Enum-variant bare-name import

`use pkg.Path.Type.{V1, V2, ...} ;` brings the named variants of enum `Type` into bare (unqualified) scope. The last dotted component before `.{...}` is the enum type name; the brace-list (trailing comma allowed) names the variants.

```logos
use core.Option.{Some, None};
```

**Divergence:** Uses `.`-separated path with `.{}` variant group; Rust spells this `use core::Option::{Some, None};` (A: `::`-item / `.`-pkg path model).

**Source:** tools/peg_gen/grammars/logos.peg#L506-L511, tools/peg_gen/grammars/logos.peg#L523-L527

### `module.use.enum-variant-alias` — use of enum variants brings bare variant names into scope

`use pkg.Path.Type.{V1, V2, …};` (capitalised Type) registers each Vi as a bare-name alias resolvable unqualified, AND brings the enum type itself into scope (so both `Type::Vi` and bare `Vi` resolve). The bare variant resolves against any in-scope enum that declares it.

```logos
use std.lang.ord.Ordering.{Less, Equal, Greater};
```

**Source:** src/compiler/sema_collect.cpp#L90-L174

### `module.use.variant-alias` — `use Enum::{V, W}` brings bare variant names into scope aliased to their enum

An enum-variant use form records each listed bare variant name as an alias to its qualifying enum type, so the variant may be referred to unqualified within the module.

**Source:** src/compiler/sema.cpp#L6862-L6881

### `module.use.variant-alias-into-scope` — `use Type::{V1, V2}` brings enum variants into bare scope

A `use pkg.Path.Type::{V1, V2, ...};` import brings the named enum variants into bare scope, so a bare `V1` resolves as `Type::V1`; on name collision, last write wins.

**Source:** src/compiler/sema_impl.hpp#L1111-L1115

## `use ... from <module>` imports

Logos addition (no Rust analog): a `use` may pin the providing module, enabling same-named packages from different modules to coexist. Stated per layer: grammar, collection, resolution, lowering, loader.

### `module.use.from-module` — use with explicit source module

`[pub] use pkg('.'IDENT)* IDENT use_module ';'` imports `pkg.path` from a named module; the trailing bare IDENT is the contextual `from` keyword and `use_module` is the source (a bare name or a quoted string for hyphenated ids, with quotes stripped). The from-bearing alternative is tried before the plain form.

```logos
use foo.Bar from "logos-lang";
```

```logos
pub use a.b.C from othermod;
```

**Divergence:** `use ... from <module>` clause has no Rust analog.

**Source:** tools/peg_gen/grammars/logos.peg#L498-L521

### `module.use.from-clause-syntax` — `use pkg from <module>` clause: operand syntax and extraction

The optional `from <module>` clause of a `use` names the providing module as its operand. The operand may be a bare identifier or a double-quoted string literal; surrounding quotes are stripped to yield the module name. Absence of the `from` clause records an empty module name, selecting default resolution. The clause is recorded as a (package dotted-path, from-module) pair.

**Divergence:** Logos-specific: type/package coexistence across modules sharing a package name (no Rust analog).

**Related:** `module.use.from-module-disambiguation`

**Source:** src/compiler/module_loader.cpp#L115-L133, src/compiler/module_loader.cpp#L206-L207

### `module.use.from-module-restricts-candidates` — use pkg from module restricts candidates

`use pkg from <module>;` restricts the candidates of `pkg` to the named module. The `from` keyword is contextual (matched as a bare identifier, so `From::from` stays valid); a missing/incorrect `from` keyword, a missing module name, or a module name matching no loaded module is an error — unless the module-name-to-id map isn't primed yet (e.g. an early metaprog discovery pass), in which case the restriction is silently skipped for that pass.

**Divergence:** Logos addition: per-import module qualification (no Rust equivalent).

**Source:** src/compiler/sema_collect.cpp#L192-L225

### `module.use.from-module-disambiguation` — `use pkg from <module>` restricts candidate visibility to the named module's id

A `use pkg from <module>;` import makes a candidate for that package visible only if the candidate's owning-module id equals the named module's id; a plain `use pkg;` accepts a candidate from any module. The `<module>` name is resolved via the canonical module-name→id table; if that table is absent/empty or the name is unknown, the `from` clause is unresolvable.

**Divergence:** Logos-specific: type/package coexistence across modules sharing a package name (no Rust analog).

**Related:** `module.use.from-clause-syntax`

**Source:** src/compiler/sema_impl.hpp#L1093-L1110

### `module.import.use-pkg-from-module` — use pkg from module restricts type/enum/trait visibility

`use pkg from <module>;` restricts the package's types, enums, and traits (not only its free functions) to the named owning module; a candidate whose package is owned by a different module than the one specified is skipped during resolution.

**Source:** src/compiler/sema_impl.hpp#L3130-L3142

### `module.use.from-module-restriction` — `use pkg from "module"` restricts the import to a specific module id

A use of the form `use pkg from "module";` resolves the quoted module name to a module id and restricts the imported package's symbol resolution to that module's exports; the restriction is in force during lowering, not only collection.

**Divergence:** note — part of Logos's C++-style module-linkage system; no direct Rust equivalent.

**Source:** src/compiler/sema.cpp#L6882-L6905

### `module.import.from-pins-module` — `use pkg from <M>` pins resolution to module M's archive

An import `use pkg from <M>;` resolves `pkg` from the archive whose embedded module canonical-name is `M`, independent of which other archive(s) also provide a package named `pkg`. This lets two distinct modules supplying a same-named package coexist; a bare `use pkg;` and `use pkg from M;` are keyed independently and both load.

**Divergence:** Logos addition (`from <module>` import selector); no Rust equivalent.

**Related:** `module.coexist.type-module-qualification`

**Source:** src/compiler/module_loader.cpp#L1594-L1611, src/compiler/module_loader.cpp#L1280-L1282

### `module.import.from-unknown-falls-through` — `from <M>` with unknown module name falls through to default resolution

If `use pkg from <M>;` names a module `M` for which no loaded archive is registered, resolution falls through to the default text-then-binary path; the precise 'no loaded module' diagnostic is emitted by later semantic analysis rather than the loader.

**Uncertainty:** Loader behavior only; the actual error is emitted in a separate sema unit.

**Source:** src/compiler/module_loader.cpp#L1603-L1611

## Re-exports

### `module.use.pub-reexport` — pub use re-exports a package

`pub use pkg;` registers pkg as a re-export from the current package, making it visible to importers of the current package.

**Source:** src/compiler/sema_collect.cpp#L226-L235

### `module.reexport.transitive-pub-use` — Transitive pub-use re-export resolution

Resolving a name in the context of imported packages searches each imported package plus, transitively and cycle-safely, all packages reachable through their `pub use` re-exports.

**Source:** src/compiler/sema_impl.hpp#L2862-L2864, src/compiler/sema_impl.hpp#L3083-L3109

## Name resolution

### `module.resolve.scope-order` — Name resolution order: current package, imports, bare

An unqualified struct/datatype/enum/trait name resolves by trying the current package's qualified key, then each imported package (and its transitive `pub use` re-exports), then the bare (legacy/unqualified) key.

**Source:** src/compiler/sema_impl.hpp#L3120-L3176

### `module.resolve.text-over-binary` — Source package resolution precedes binary archive resolution

A `use pkg;` import resolves `pkg` by first consulting the source (text) package index built from `.logos` files; only if no source package declares `pkg` is the binary archive index consulted. If neither supplies `pkg`, compilation fails with a 'cannot find package' error.

**Source:** src/compiler/module_loader.cpp#L1613-L1641

### `module.lookup.unqualified-name-scope` — Known-type-name check: scope, package, then wildcard imports

A name is considered a known type name iff: bound in current_type_params_; OR is one of a fixed builtin primitive spellings {i32,i64,f64,f32,bool,u8,i8,u32,u64,void,i16,u16,i56,u56,i128,u128}; OR appears in structs_/datatypes_/enums_/type_aliases_ under the bare name, OR under the current-package-qualified key, OR under a key qualified by any package the current file wildcard-imports.

**Uncertainty:** This builtin list (used by is_known_type_name, which gates typevar extraction) omits usize/isize/char/i24/u24 that try_resolve_as_known_type accepts — those spellings are known-type here only via the struct/datatype/enum/alias lookups, not the primitive fast path.

**Source:** src/compiler/sema_collect.cpp#L4384-L4406

## Package-qualified paths and calls

### `module.path.qualified-call` — Package-qualified call scopes free-fn resolution to that package

A package-qualified call `pkg.path::fn(args)` carries RECEIVER + QUAL_PARTS (the dotted package segments); for the duration of lowering this call, free-function-name resolution is constrained to that package via `call_pkg_qualifier_` (RAII-restored to the prior value on return, supporting nesting). The qualified form's ARGS may be wrapped in a `call_arg_list` node (`{ITEMS:[...]}`) instead of a raw array; both shapes are accepted, gated on QUAL_PARTS so the (overwhelmingly common) unqualified raw-array shape is never reinterpreted as a map.

**Divergence:** A9

**Source:** src/compiler/sema_expr.cpp#L2760-L2803

### `module.path.qualified-member-fallback` — Qualified call with no matching free fn falls back to a type-member static call

A qualified call `pkg.path::name(args)` whose package has no matching free function and no matching generic free function is instead treated as a type-member static call `pkg.path.Type::method(...)` (the trailing dotted segment names the type) and delegated to `lower_static_call`, which re-derives the class from QUAL_PARTS. A free function of that name in the qualified package always takes precedence over this fallback.

**Divergence:** A9

**Source:** src/compiler/sema_expr.cpp#L2805-L2814

### `module.call.package-qualifier-disambiguates` — Explicit package qualifier restricts free-fn candidate lookup to that package

A call written with an explicit package qualifier (`pkg.sub::fn(...)`) restricts free-function candidate resolution to functions whose declared `.package` matches the dotted qualifier exactly (disambiguating same-named free fns across packages, e.g. `mem::replace` vs `ptr::replace`); an empty qualifier performs ordinary unqualified, import-based scope resolution.

**Source:** src/compiler/sema_impl.hpp#L3732-L3746

## Visibility (`pub` / `pub(module)`)

### `module.visibility.same-package` — Same-package access is always permitted

Visibility checks are skipped when scope context is absent (empty defining or current package); access to any item whose defining package equals the current package is always allowed regardless of `pub`.

**Source:** src/compiler/sema_collect.cpp#L746-L750

### `module.visibility.private-cross-package` — Non-pub items are private across packages

An item not marked `pub` (and not `pub(module)`) is inaccessible from a different package; cross-package reference is an error.

**Source:** src/compiler/sema_collect.cpp#L760-L761

### `module.visibility.bare-cross-package-pub-check` — Cross-package items checked even via bare key

An item resolved through the bare/unqualified key is subject to the same pub-access check as a package-qualified hit when it belongs to a package different from the current one; only own-package bare entries (primitives/builtins) bypass the check.

**Source:** src/compiler/sema_impl.hpp#L3157-L3175

### `module.visibility.pub-marker-only-module` — Restricted-visibility marker accepts only pub(module)

An item's restricted-visibility marker `pub(W)` is accepted only when W is the contextual word `module` (module-linkage). Plain `pub` and no marker are non-module. Any other word (e.g. `pub(crate)`, `pub(super)`, `pub(in path)`) is rejected with the diagnostic "unsupported visibility `pub(W)` — only `pub(module)` is recognised".

```logos
pub(module) fn f() {}
```

```logos
pub(crate) fn g() {} // error
```

**Divergence:** Logos has only `pub` and `pub(module)`; Rust's `pub(crate)`/`pub(super)`/`pub(in path)` are not recognised.

**Related:** `module.visibility.pub-module-only`

**Source:** src/compiler/sema_impl.hpp#L1176-L1191

### `module.visibility.pub-module-only` — pub(module) exports within the owning module only

An item marked `pub(module)` is exported within its owning module only (module-linkage: visibility crosses package boundaries inside the same module) but is not visible to external consumers; `pub(module)` implies `pub` at the package level.

**Related:** `module.visibility.pub-marker-only-module`

**Source:** src/compiler/sema_impl.hpp#L2428, src/compiler/sema_impl.hpp#L2548-L2551

### `module.visibility.pub-module-linkage` — pub(module) has module-linkage across packages

A `pub(module)` item has module-linkage: across package boundaries it is visible iff the defining module id equals the consuming (current) module id, otherwise it is module-private and access is an error. The module-linkage check runs before the plain `pub` test (a `pub(module)` item also sets is_pub), and the package's owning module id is consulted on each cross-package resolution.

**Source:** src/compiler/sema_collect.cpp#L751-L759, src/compiler/sema_impl.hpp#L3119-L3129

### `module.visibility.pub-module-scope` — pub(module) items are module-private across module boundaries

A type/enum/trait/struct marked module-only-public, resolved through an imported package, is checked against the accessing site's owning module: if the item's owning module differs from the accessing module, resolution reports a module-private diagnostic even though the item is otherwise `pub`.

**Source:** src/compiler/sema_impl.hpp#L3143-L3153

### `module.visibility.trait-lookup-deferred` — Trait name lookup does not enforce pub-visibility

Resolving a trait name does not run the pub-access check that struct/datatype/enum lookup runs; a private/foreign trait name still resolves at general reference sites (bound checks, fallback probes). Visibility for a foreign trait is instead enforced explicitly at the site that introduces it into an impl.

**Uncertainty:** The enforcement site (collect_impl) is outside this slice; only the deferral is directly observed here.

**Source:** src/compiler/sema_impl.hpp#L3202-L3209

### `module.vis.struct-pub-and-module-only` — Struct/datatype/schema items record pub and pub(module) independently

Every struct, datatype, schema, and schema-enum item independently records an is_pub flag (parsed from an IS_PUB value node, default false) and an is_module_only flag (`pub(module)`, via read_module_vis) — the two visibility axes are orthogonal per item.

**Source:** src/compiler/sema_collect.cpp#L3913-L3917, src/compiler/sema_collect.cpp#L4027-L4031, src/compiler/sema_collect.cpp#L4123-L4127, src/compiler/sema_collect.cpp#L4193-L4197

## Inner attributes and module docs

### `module.attr.inner-vs-item-attribute` — Inner attribute applies at file/module level

`#![name]` / `#![name(args)]` / `#![name=val]` is a file/module-level inner attribute, distinct from per-item `#[...]` attributes.

**Source:** tools/peg_gen/grammars/logos.peg#L310

### `module.doc.inner-commit` — Module inner doc (//! and /*! */) commits once per module

`//!` line and `/*! ... */` block inner doc-comments accumulate (leading marker stripped, blocks joined by newline) into a per-module buffer over the whole item-lowering pass; after all items are processed, if non-empty, the buffer is committed as a single ModuleInnerDoc record (keyed by the module's file path) on the program and reset.

**Source:** src/compiler/sema.cpp#L7441-L7457, src/compiler/sema.cpp#L8059-L8067

## Implicit prelude

### `module.prelude.implicit-auto-import` — Implicit prelude auto-imported per file

Every source file implicitly imports the prelude package in addition to its explicit `use` declarations, unless the file opts out. The implicit prelude is deduplicated against explicit uses (no duplicate import if already named).

**Divergence:** Logos uses a named prelude *package*; the model parallels Rust's std prelude but is package-granular.

**Source:** src/compiler/module_loader.cpp#L95-L103

### `module.prelude.implicit-injection` — Implicit prelude is wildcard-injected into source modules unless opted out

The configured implicit-prelude package is implicitly wildcard-imported into every source-side module loaded for the current compilation, making prelude names resolvable unqualified, EXCEPT: (1) modules loaded from a binary archive (their producer already applied its own prelude when the archive was built), (2) the prelude package itself (self-import skipped), and (3) any file carrying the inner attribute `#![no_implicit_prelude]`. The injection is deduplicated against an explicit `use` of the same package, and an empty implicit-prelude setting injects nothing.

```logos
#![no_implicit_prelude]
```

**Source:** src/compiler/sema_collect.cpp#L240-L266

### `module.prelude.implicit-unless-opted-out` — Implicit prelude visible to non-binary ASTs without no_implicit_prelude

An implicit-prelude package, when configured, is added to the wildcard import scope of every non-binary compilation unit that does not carry `#![no_implicit_prelude]`; an empty prelude name means no implicit prelude.

**Source:** src/compiler/sema_impl.hpp#L786-L792

### `module.prelude.opt-out-attribute` — #![no_implicit_prelude] suppresses implicit prelude

A file-level inner attribute `#![no_implicit_prelude]` suppresses the implicit prelude import for that file. It is recognized as an INNER_ANNOTATION item with name `no_implicit_prelude`.

**Source:** src/compiler/module_loader.cpp#L57-L78, src/compiler/module_loader.cpp#L96

### `module.prelude.cross-cutting-auto-load` — Cross-cutting foundation packages auto-load without explicit `use`

Foundation packages under prefixes `std.lang`, `std.writ`, or `logos.lang` (excluding the `logos.lang.writ` substrate) are implicitly available to every compilation: when an archive is loaded for a requested package, sibling packages with these prefixes are also loaded so cross-cutting traits and types (Default, Ord, Send, Clone, etc.) resolve without an explicit import edge.

**Divergence:** Logos addition: implicit prelude is prefix-scoped to the lang tier (transitional; manifest-tier system intended).

**Uncertainty:** Exact prefix set is a transitional heuristic per source comments.

**Source:** src/compiler/module_loader.cpp#L1397-L1432, src/compiler/module_loader.cpp#L1566-L1571

### `module.prelude.no-double-load` — A prelude sibling already supplied by source is not re-loaded from binary

An auto-loaded prelude sibling package is skipped when the source (text) index already provides that package, preventing the same package's items/impls from being registered twice (which would otherwise produce duplicate-definition / conflicting-impl errors). The explicitly requested package is always loaded.

**Source:** src/compiler/module_loader.cpp#L1566-L1571, src/compiler/module_loader.cpp#L1555-L1565

### `module.prelude.binary-modules-not-augmented` — Binary-archive modules keep their original prelude

Files loaded from a binary archive are never re-augmented with the consumer's implicit prelude; the prelude in effect at their original build time is final.

**Source:** src/compiler/module_loader.hpp#L131-L134

## Module manifest

### `module.manifest.line-oriented-kv` — Manifest is line-oriented key/value

A module manifest is parsed line by line. Each non-empty, non-comment line is split into a key (first whitespace-delimited token) and a value (remainder, trimmed). Surrounding whitespace on the line is trimmed before parsing.

**Source:** src/compiler/module_manifest.cpp#L24-L33

### `module.manifest.blank-and-comment-skip` — Blank and # comment lines ignored

A manifest line that is empty after trimming, or whose first non-whitespace character is '#', is ignored.

**Source:** src/compiler/module_manifest.cpp#L26-L27

### `module.manifest.directive-set` — Recognized manifest directives

Recognized manifest directives are: `module` (canonical name), `version`, `id` (mangle key), `root` (source directory), `depends`, `exclude`, `ast_only`, `lowering`, `tier`, `prelude`. `depends`/`exclude`/`ast_only` accumulate one value per occurrence (empty values skipped); the rest set a single value.

**Source:** src/compiler/module_manifest.cpp#L35-L65

### `module.manifest.unknown-key-ignored` — Unknown directives ignored for forward compatibility

A manifest key that matches no known directive is silently ignored (forward compatibility); it is not an error.

**Source:** src/compiler/module_manifest.cpp#L66

### `module.manifest.required-module-and-root` — module and root are required

A manifest is invalid (parse fails with an error) unless it declares a non-empty `module` name and a non-empty `root` directory.

**Source:** src/compiler/module_manifest.cpp#L69-L70

### `module.manifest.version-default` — Version defaults to 0.0

If no `version` directive is present, the module version defaults to "0.0".

**Source:** src/compiler/module_manifest.cpp#L71

### `module.manifest.lowering-eager-default` — lowering directive: lazy|eager, default eager

The `lowering` directive must be exactly `lazy` or `eager`; any other value is an error. Absent the directive, lowering is eager (emit .o + LIR blob); `lazy` emits only the parsed-AST artifact and defers lowering to the consumer.

**Source:** src/compiler/module_manifest.cpp#L42-L49

### `module.manifest.tier-closed-set` — tier directive restricted to lang|mem|std

The `tier` directive must be exactly one of `lang`, `mem`, or `std`; any other (including empty) value is an error. lang = no-alloc/no-OS, mem = heap/no-OS, std = full. An absent tier means tier-not-declared (no availability enforcement).

**Source:** src/compiler/module_manifest.cpp#L50-L58, src/compiler/module_manifest.hpp#L52-L60

### `module.manifest.prelude-nonempty` — prelude requires a package name

If a `prelude` directive is present, its value must be a non-empty (dotted) package name; an empty value is an error. The named package is injected as an implicit `use <pkg>;` at the head of every file in the module that lacks `#![no_implicit_prelude]`.

**Source:** src/compiler/module_manifest.cpp#L59-L65, src/compiler/module_manifest.hpp#L62-L66

## Module loading and ordering

### `module.load.dependency-order` — Modules loaded in dependency order, root last

Compiling a root file transitively resolves and parses all files reachable through `use <pkg>;` declarations; the resulting module sequence is topologically ordered with every dependency preceding any module that uses it and the root module last.

**Source:** src/compiler/module_loader.hpp#L126-L146, src/compiler/module_loader.hpp#L142

### `module.load.post-order-dependency` — Imported packages are loaded before their importer (post-order)

Package dependencies declared via `use` are loaded depth-first in post-order, so a package's transitive imports are emitted before the package itself; the final module sequence is additionally topologically sorted to make dependency-first ordering uniform across source and binary loads.

**Source:** src/compiler/module_loader.cpp#L1626-L1627, src/compiler/module_loader.cpp#L1664-L1665, src/compiler/module_loader.cpp#L1673-L1679

### `module.load.package-atomic-dedup` — Each package loaded at most once; files deduplicated by canonical path

A package is loaded at most once per build (keyed by package name, or by (module,package) under a `from` import); within a load each file is added at most once, identified by its canonical filesystem path.

**Source:** src/compiler/module_loader.cpp#L1598-L1599, src/compiler/module_loader.cpp#L1619-L1620, src/compiler/module_loader.cpp#L1583-L1584

### `module.deps.dependency-first-ordering` — Modules processed dependencies-first

Modules are ordered so that for any `use`-edge from package u to package v, v is processed before u. Ordering is package-granular: all files of a package move together, preserving first-seen order within a package and within an SCC; absent a forcing dep edge, original load order is preserved (stable).

**Source:** src/compiler/module_loader.cpp#L213-L244, src/compiler/module_loader.cpp#L378-L388

### `module.deps.package-cycles-allowed` — Package-level dependency cycles are legal

Cyclic `use` dependencies between packages are legal (e.g. option `<->` result, where each package's methods reference the other). Mutually-dependent packages form a strongly-connected component and are compiled together.

**Source:** src/compiler/module_loader.cpp#L228-L233

### `module.exclude.path-prefix-defers-to-binary` — Excluded path prefixes are not picked up as source

Absolute path prefixes declared as excludes (mirroring the manifest `exclude` directive) remove matching files from the text-package index: a `use` whose resolution begins with an excluded prefix is not loaded from source, deferring that package to a binary archive instead.

**Source:** src/compiler/module_loader.hpp#L135-L141

## Type identity and coexistence across modules

### `module.struct.pkg-qualified-nominal-identity` — Struct identity is package-qualified, not by bare name

Struct type identity for layout, field-embedding, and freeze analysis is keyed by (package, name), not by the bare name alone — two same-named structs declared in different packages are distinct types with independent layouts; a bare-name lookup would incorrectly alias an imported same-named struct's layout onto the wrong definition.

**Source:** src/compiler/mlir_gen_types.cpp#L79-L93, src/compiler/mlir_gen_types.cpp#L483-L488, src/compiler/mlir_gen_types.cpp#L588-L589

### `module.coexist.type-module-qualification` — Type symbols are module-qualified (`$M<id>`) for same-pkg coexistence; stdlib exempt

Every type-keyed symbol embeds the owning module's id as a `$M<module_id>` suffix on the package, so two separately-compiled modules declaring the same pkg::Type do not collide at link. stdlib packages (prefix 'logos.') and an empty/absent pkg-to-module map yield no suffix (byte-identical output).

**Source:** src/compiler/sema.cpp#L1404-L1415, src/compiler/sema.cpp#L1460-L1463

## Symbol mangling

### `module.symbol.mangle-by-module-id` — Symbol mangling is keyed by owning-module id

Each compilation unit carries an owning-module id that is baked into symbol mangling, so identically-named packages from different modules (or versions) produce distinct symbols (C++ module-linkage model). One package maps to one module id within a coherent build.

**Source:** src/compiler/sema_impl.hpp#L1083-L1095

### `module.symbol.function-symbol-name` — Function link symbol = pkg/module-qualified mangle; extern and methods carved out

A function's link symbol is built from {module_id, package, base, signature, is_generic, is_method, is_extern} via the canonical sym::mangle encoder. Extern functions keep their bare ABI/C name; struct methods (base containing '__') are disambiguated by their struct's pkg-qualified name rather than re-qualified here. Two packages defining the same base+signature get distinct symbols.

**Source:** src/compiler/sema.cpp#L1543-L1568

### `module.symbol.method-link-prefix` — Methods are emitted under module-qualified link symbols

A function's emitted link symbol is its module-qualified name: methods gain the module prefix; free functions and extern functions keep their bare name. Symbol identity and forward-declaration deduplication key off this link name, while Logos-level parameter-type/coercion maps used at call sites are recorded under BOTH the qualified link name and the bare name (call sites may reference the bare name during body emission).

**Related:** `item.fn.unique-mangled-name`

**Source:** src/compiler/mlir_gen_fn.cpp#L238-L242, src/compiler/mlir_gen_fn.cpp#L260, src/compiler/mlir_gen_fn.cpp#L271-L282

## Statics across library/executable builds

### `module.static.library-defers-to-executable` — library build declares statics extern; only the executable owns storage + init

In a library build (no `main`), every `static` is emitted as an external declaration with no storage and no startup initializer. The final executable, which transitively re-lowers all used statics from imported module metadata, is the sole owner of each static's storage and its startup initialization. Extern (`extern`-declared / FFI) statics are always external declarations regardless of build kind.

**Source:** src/compiler/mlir_gen_dyn.cpp#L668-L681, src/compiler/mlir_gen_dyn.cpp#L694-L701, src/compiler/mlir_gen_dyn.cpp#L716-L723

## Binary archives

### `module.binary.archive-membership` — Modules may be loaded from binary `.writ0` archives

A module may be loaded from a precompiled `.writ0` member inside a `.a` archive rather than from source; binary modules take their `module_id`/`module_name` from the archive's `@module` `.pkgi` header.

**Source:** src/compiler/module_loader.hpp#L21, src/compiler/module_loader.hpp#L24-L27

### `module.binary.lazy-local-lowering` — Lazy archives are lowered as local code

A lazy-mode binary archive ships only the parsed AST (no object text, no LIR blob); the consumer must lower such a module's items locally (sema/mono/codegen treat them as user code).

**Source:** src/compiler/module_loader.hpp#L32-L37

### `module.tagdispatch.binary-archive-provides-tables` — Fully-binary tag systems are provided by the archive

A tag system whose every registered callee is already present in a linked binary archive is not re-defined; the consuming unit emits only external references to that system's tables, lookup function, and initializer. Tables also present in an archive use weak (deduplicating) linkage rather than triggering a duplicate-definition error.

**Divergence:** Module/separate-compilation model; no direct Rust analogue.

**Source:** src/compiler/mlir_gen_dyn.cpp#L219-L292, src/compiler/mlir_gen_dyn.cpp#L358-L368, src/compiler/mlir_gen_dyn.cpp#L394-L396

## Binary export catalogs

### `module.exports.template-catalog-non-generic-excluded` — Stdlib export catalog lists exactly the generic items

A binary module's exports catalog records precisely the items whose `type_params` is non-empty (generic struct/enum/fn templates); non-generic items are not catalogued as templates.

**Source:** src/compiler/module_loader.hpp#L40-L45, src/compiler/module_loader.hpp#L73-L78

### `module.exports.blanket-impl-catalog` — Blanket impls catalogued by trait and bounds

An exported blanket impl `impl<T: Bound + Extra...> Trait for T` is catalogued as (trait, primary bound, extra bounds), with the target type being the type variable by definition; an unbounded blanket impl has empty primary bound.

**Source:** src/compiler/module_loader.hpp#L79-L86

### `module.exports.concrete-impl-catalog-drops-negative-dst` — Concrete-impl catalog excludes negative and DST-target impls

An exported concrete impl `impl Trait for Target` is catalogued as (trait, target); negative impls and DST target patterns are dropped from the catalog (which is a fast-path lookup index only, not the authoritative impl set).

**Source:** src/compiler/module_loader.hpp#L87-L93

### `module.exports.merge-later-archive-wins` — Export merge across archives is order-preserving, later wins on fn duplicate

When export catalogs from multiple archives are unioned, archive order is preserved and a later archive wins on a duplicate function-template mangled symbol (occurs only when a project redefines a stdlib mangled symbol).

**Source:** src/compiler/module_loader.hpp#L106-L112

## ABI compatibility

### `module.abi.one-directional-minor-compat` — Binary archive ABI compatibility is one-directional within a major version

A compiler may consume a binary library iff (a) the library's language major version equals the compiler's, and (b) for stable releases the library's minor version is `<=` the compiler's minor. A differing major is incompatible; a library built by a newer minor is rejected. An ABI-incompatible archive is not indexed (its packages become unavailable). Identical version strings are always compatible; legacy archives without a version stamp are not enforced.

**Divergence:** Logos addition: semantic-version ABI gate on binary modules (Rust has no stable cross-version library ABI).

**Source:** src/compiler/module_loader.cpp#L1100-L1144, src/compiler/module_loader.cpp#L1182-L1184

### `module.abi.prerelease-no-guarantee` — Pre-release / snapshot builds require exact version match

If either the library or the compiler is a pre-release (`-pre`) or snapshot (`+meta`) build, no ABI guarantee holds: only an exact version-string match is silently accepted; any mismatch is permitted but warned. The check is disabled entirely by environment override.

**Divergence:** Logos addition.

**Source:** src/compiler/module_loader.cpp#L1100-L1113, src/compiler/module_loader.cpp#L1130-L1142, src/compiler/module_loader.cpp#L1110-L1110
