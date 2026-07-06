# `docs.json` — API documentation interchange format (v1)

Status: **v1 (draft; producer stable, contract not yet frozen)** · Date: 2026-07-06 · Owner ADR: [0014](../adr/0014-doc-extraction-deem-native.md)

`docs.json` is the machine-readable API-documentation model the Logos toolchain
extracts from source `///` / `//!` / `/** */` doc comments. It is the **contract
between the extractor (this repo) and the renderer (`logos-site`)**: the tools
here produce it; `logos-site`'s `build.mjs` consumes it and renders HTML in the
site's style. **Rendering, markdown→HTML, and styling are NOT the extractor's
job** — doc bodies travel as raw markdown strings; the site renders them.

This document specifies (1) how `logos-site` invokes the tools to produce
`docs.json` and (2) the format itself.

---

## 1. Producing `docs.json`

The binaries live under `build/bin/` after a build; the stdlib archives the
compiler needs live under `build/lib/logos` (referred to below as `$LIBDIR`).
`logosc` locates the stdlib via `$LOGOS_LIB_DIR` (or `-L <dir>`).

There are two entry points. `logos-site` picks by what it is documenting.

### 1a. `lforge doc` — for an lforge package

For any directory that is an lforge project (has an `lforge.writ` manifest):

```sh
cd <package-dir>
LOGOSC=build/bin/logosc LOGOS_LIB_DIR=build/lib/logos build/bin/lforge doc
```

Writes **one file per lib target** to `.lforge/<profile>/doc/<target-name>.json`
(`<profile>` = `debug` unless `--release` is passed). Builds the lib targets
first, so it also surfaces compile errors.

### 1b. `logosc --emit-docs` + `docgen` — for arbitrary modules (e.g. the stdlib)

The stdlib is not an lforge project; document any module manifest directly:

```sh
# 1. Extract doc facts → <prefix>.docwr  (also writes <prefix>.o/.wr0 — ignore them)
LOGOS_LIB_DIR=build/lib/logos \
  build/bin/logosc --emit-module stdlib/lang/logos.module --emit-docs -o /tmp/lang

# 2. Resolve facts → docs.json
build/bin/docgen /tmp/lang.docwr docs/lang.json
```

Stdlib manifests: `stdlib/lang/logos.module`, `stdlib/mem/logos.module`,
`stdlib/std/logos.module` — one `docs.json` per manifest.

`docgen <in.docwr> <out.json>` is a standalone converter: it performs the exact
same fact→model resolution that `lforge doc` does in-process, so both paths emit
byte-compatible `docs.json`.

### Notes for `logos-site`

- The two-step Path 1b is the one to use for the language reference (stdlib).
- `--emit-docs` re-runs a full module compile; expect it to take as long as
  building that module. Cache on source mtime.
- Exit codes: `logosc`/`lforge`/`docgen` return non-zero on failure; treat any
  non-zero as "no valid `docs.json`".

---

## 2. Format

A `docs.json` is a single JSON object describing **one package**. UTF-8, no BOM.

```json
{
  "schema_version": 1,
  "package": "logos.lang.option",
  "items": [ <Item>, … ],
  "undocumented": [ "<path>", … ]
}
```

| Top-level key    | Type       | Meaning                                                            |
|------------------|------------|-------------------------------------------------------------------|
| `schema_version` | integer    | Format version. Currently `1`. Bumped on any breaking shape change. |
| `package`        | string     | Module/label name from the producing manifest — the target name for `lforge doc` (e.g. `core`), the module name for a bundled manifest (e.g. `logos-lang` for `stdlib/lang`). A display label; item `path`s carry the authoritative dotted namespace (`logos.lang.option.Option`). |
| `items`          | array      | Every documented entity, flat (see §2.1). Order is emission order, not significant. |
| `undocumented`   | `string[]` | `path` of every **public** item whose `doc` is empty. A coverage report. |

### 2.1 Item

```json
{
  "kind": "method",
  "path": "logos.lang.option.Option::unwrap",
  "name": "unwrap",
  "parent": "logos.lang.option.Option",
  "visibility": "pub",
  "signature": "fn unwrap(self: Option) -> T",
  "doc": "Returns the contained `Some` value.\n\n## Panics\n…",
  "src_file": "stdlib/lang/option/option.logos",
  "implements": [ … ],      // struct/enum only
  "implementors": [ … ]     // trait only
}
```

| Field        | Type       | Present on         | Meaning                                                                                  |
|--------------|------------|--------------------|------------------------------------------------------------------------------------------|
| `kind`       | string     | all                | One of §2.2.                                                                              |
| `path`       | string     | all                | Fully-qualified, stable id. Free items: `<package>.<name>`. Members: `<parent-path>::<name>`. Unique within the file. |
| `name`       | string     | all                | Leaf name (`unwrap`, `Option`, `x`).                                                      |
| `parent`     | string     | all                | `path` of the enclosing item; `""` for top-level items (`fn`/`struct`/`enum`/`union`/`trait`). |
| `visibility` | string     | all                | `"pub"` or `"priv"`.                                                                      |
| `signature`  | string     | all                | Rendered source signature (see §2.3 for v1 caveats).                                      |
| `doc`        | string     | all                | **Raw markdown** doc text, comment markers stripped; `""` when undocumented. The site renders it. |
| `src_file`   | string     | all                | Source file. Populated for `fn`/`method` (currently an absolute path); `""` for types/fields/variants in v1 (see §4). |
| `implements` | `string[]` | `struct`, `enum`   | Names of traits this type implements (see §2.4).                                          |
| `implementors`| `string[]`| `trait`            | Names of types that implement this trait (see §2.4).                                      |

### 2.2 `kind` values (v1)

`fn` · `method` · `struct` · `union` · `enum` · `variant` · `field` · `trait` · `macro`

- `fn` — free function (`parent: ""`).
- `method` — inherent or trait-impl method, and `self`-less associated functions
  (e.g. `Vec::new`); `parent` is the owning type's `path`.
- `field` — a struct/union field; `parent` is the type.
- `variant` — an enum variant; `parent` is the enum. `signature` is the variant
  form (`Some(T)`, `None`).
- `struct`/`union`/`enum`/`trait` — the type/trait declaration itself.
- `macro` — a `#[token_macro]`/`#[fn_macro]` handler function; the user-facing
  surface is the macro `name!(…)` (e.g. `deem`, `trama`, `schema_catalog`).
  `signature` is `macro <name>!`; `doc` documents the macro invocation form.
  Macro items are emitted even from engine-internal packages (the macro is the
  one consumer surface there).

Not emitted in v1: `module` (module-level `//!` docs), `const`, `static`,
`type_alias`, `assoc_type`, `assoc_const`, per-trait method signatures. See §4.

### 2.3 `signature`

The rendered source-level signature, e.g. `fn map(self: Option, f: fn(T) -> U) -> Option`,
`struct Point`, `Some(T)`. **v1 caveat:** generic type-parameter lists are **not**
rendered — `Option` appears as `enum Option`, not `enum Option<T>`; a method's
generic params are likewise omitted. Treat `signature` as a display string, not a
parseable type.

### 2.4 Cross-references (`implements` / `implementors`)

Derived by folding the source's `impl Trait for Type` edges:

- On a `trait` item, `implementors` lists the **type names** that implement it.
- On a `struct`/`enum` item, `implements` lists the **trait names** it implements.

**v1 caveat:** these arrays hold bare **names** (e.g. `"Point"`, `"Eq"`), matching
the item `name` field, **not** fully-qualified `path`s. Within a single-package
`docs.json` this resolves unambiguously by matching `name`; cross-package
resolution is a later (Deem-backed) concern — see ADR 0014 §11. Names may include
primitives (`i32`, `u8`) as implementing types (e.g. `Default`).

### 2.5 `doc` is markdown

`doc` is the verbatim doc-comment body with markers stripped (`///` + one leading
space removed; `/** */` frame and per-line `*` removed). It is **markdown source**.
The site renders it with its own `markdown-it` pipeline (code fences tagged
`logos` get syntax highlighting; `$…$` / `$$…$$` get KaTeX). The extractor never
parses or renders markdown.

---

## 3. Example

Input (`core.logos`):

```logos
package core;

/// The universal answer.
pub struct Answer { pub value: i32 }

/// Types that can produce a total.
pub trait Summable { fn total(&self) -> i32; }

impl Summable for Answer { fn total(&self) -> i32 { return self.value; } }

/// Return the answer.
pub fn answer() -> i32 { return 42; }

pub fn helper_undoc() -> i32 { return 0; }
```

Output `docs.json`:

```json
{
  "schema_version": 1,
  "package": "core",
  "items": [
    { "kind": "fn", "path": "core.answer", "name": "answer", "parent": "",
      "visibility": "pub", "signature": "fn answer() -> i32",
      "doc": "Return the answer.", "src_file": "…/core.logos" },
    { "kind": "method", "path": "core.Answer::total", "name": "total",
      "parent": "core.Answer", "visibility": "pub",
      "signature": "fn total(self: &Answer) -> i32", "doc": "", "src_file": "…/core.logos" },
    { "kind": "struct", "path": "core.Answer", "name": "Answer", "parent": "",
      "visibility": "pub", "signature": "struct Answer",
      "doc": "The universal answer.", "src_file": "",
      "implements": ["Summable"] },
    { "kind": "field", "path": "core.Answer::value", "name": "value",
      "parent": "core.Answer", "visibility": "pub", "signature": "value: i32",
      "doc": "", "src_file": "" },
    { "kind": "trait", "path": "core.Summable", "name": "Summable", "parent": "",
      "visibility": "pub", "signature": "trait Summable",
      "doc": "Types that can produce a total.", "src_file": "",
      "implementors": ["Answer"] }
  ],
  "undocumented": [ "core.Answer::total", "core.Answer::value", "core.helper_undoc" ]
}
```

---

## 4. Versioning & stability

- `schema_version` is the compatibility gate. `logos-site` should pin the versions
  it understands and refuse (or warn on) an unknown one.
- Additive changes (new optional fields, new `kind` values) **do not** bump the
  version — consumers must ignore unknown fields/kinds. A breaking change (removing
  or retyping a field) bumps `schema_version`.
- The format is a **draft**: v1 is not yet frozen. The known-incomplete areas
  below may change before freeze. Raise contract changes against ADR 0014.

### Known v1 limitations

| Area | v1 behaviour | Planned |
|------|--------------|---------|
| Item kinds | no `module`/`const`/`static`/`type_alias`/`assoc_*` items | emit them |
| `signature` | generic params omitted (`enum Option`) | render `<T,…>` |
| `src_file` | absolute path; only on `fn`/`method` | repo-relative; on all items |
| Cross-refs | bare names, not paths; per-file only | resolved paths / ids (Deem) |
| `used_by` / reachability | absent | add (needs signature-mention facts) |
| Multi-target workspace | one `<name>.json` per lib target | combined index |
| Doc-tests | not extracted | extract fenced `logos` blocks |

---

*See ADR [0014](../adr/0014-doc-extraction-deem-native.md) for the design, the
extraction pipeline, and the rationale behind these choices.*
