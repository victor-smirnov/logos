# ADR 0014 — Deem-native Documentation Extraction (`logosc --emit-docs` + `lforge doc`)

- Status: **Proposed** (design-only; first slice specified in §8)
- Date: 2026-07-05
- Builds on: ADR 0011 (Writ schemas), ADR 0012 (WQL/Deem), ADR 0013 (incremental DBSP engine).
- Does **not** change the IR, the grammar, or the doc-comment lexing (all already shipped — see §0.1).
- Scope boundary: **the tool extracts and resolves; it does NOT render.** HTML/styling/markdown-rendering is owned by the separate `logos-site` repo (§4). The contract between the two is a versioned `docs.json`.

---

## 0. Context

We want a documentation-extraction tool of the javadoc/rustdoc class — "но помощнее". The thesis of "помощнее" is concrete and is the whole reason this is an ADR and not a script: **API documentation is a relational, incrementally-maintained dataset, not a tree-walk over one file.** javadoc/rustdoc hardcode every cross-reference (implementors, used-by, trait-method resolution, reachability of the public surface) as ad-hoc renderer passes. We already own the two subsystems that turn those passes into declarative one-liners: **Deem** (Datalog, ADR 0012) and the **incremental engine** (DBSP, ADR 0013). So the tool is: **extract → Writ facts (EDB) → Deem cross-reference relations (IDB) → serialize a resolved doc model (`docs.json`)**. Rendering is out of scope — `logos-site` consumes `docs.json`.

### 0.1 What is ALREADY built (do not rebuild)

The entire doc-comment frontend ships today:

- **Lexing**: `///` `//!` `/** */` `/*!` are first-class tokens (`DOC_LINE`/`DOC_INNER`/`DOC_BLOCK`/`DOC_BLOCK_INNER`, `logos.peg:446-456`; carved out of `%skip`), wrapped into AST nodes `DOC_LINE_LIT=237`…`INNER_DOC_BLOCK_LIT=240`.
- **Sema attachment**: outer docs accumulate in `pending_doc_` and flush onto the next item exactly like `pending_annots`; inner docs accumulate into `module_inner_doc_` (`sema_collect.cpp:1425-1453`). Envelope-stripping (rustdoc rules: drop `///`+one leading space; strip `/** */` frame and per-line `*`) already implemented in `sema_impl.hpp:2780-2827`.
- **Persistence + accessors**: the stripped text lands on the sema info structs and is written into the LIR schema per-decl, with **read accessors already present on every decl view**: `StructView::doc()`, `FunctionView::doc()`, field `doc()` (`F_DOC`), trait `doc()`, trait-method `doc()` (`TM_DOC`), assoc-type `doc()` (`AT_DOC`) — `lir_view.hpp:441-1500` — plus `LProgram::module_inner_docs` (`lir.hpp:816`, whose comment already reads *"downstream rustdoc-style tooling consumes this directly"*).

So the extractor **consumes existing structured data via `.doc()`**; it never touches comment lexing.

### 0.2 What is missing

1. A **stable emission surface** — `logosc --emit-docs` — producing a documented doc-facts Writ container. (We must NOT read raw `.wr0`: the LIR schema is deem-internal and explicitly excluded from the ABI allowlist. Same lesson as `--emit-abi`: the *emitted* artifact is the contract, not the internal tables.)
2. An **`lforge doc`** command that drives per-file emission, merges facts, runs the Deem cross-ref rules, and serializes `docs.json`.
3. A **frozen `docs.json` schema** — the cross-repo contract consumed by `logos-site`'s `build.mjs` (§4).

Notably NOT missing / NOT in scope: markdown→HTML (owned by the site, §4.1) and any HTML/CSS/templating (owned by the site). This is a deliberate contraction of scope from an earlier draft.

### 0.3 Precedent to mirror

`--emit-abi` (`main.cpp:2792 emit_abi_spec`, golden `abi/logos.abi`) is the structural model: a compiler-internal emitter that walks the post-sema/post-mono `LProgram` decl views, renders types via the shared `type_str` (`sema.cpp:2074`), and writes a **stable, sorted, contract-grade artifact** that CI diffs. `--emit-docs` is its sibling — the same walk (`emit_module.cpp:577-627`), now also reading `.doc()`, emitting a Writ container instead of TSV.

### 0.4 The consumer — `logos-site`

`/home/victor/devel/logos-site` is a hand-rolled static-site generator (`build.mjs`, Node, no framework): Markdown + front-matter in → templated HTML out, using `markdown-it` (+ `markdown-it-anchor`, `highlight.js` for `logos`-tagged code, KaTeX for math). Today it walks `content/**/*.md` only. **The API reference is a new input class**: a processor step in `build.mjs` ingests `docs.json` and generates API pages in the site's own style, running doc-body markdown through the site's existing `markdown-it` pipeline. That processor + templates live in the `logos-site` repo and are a companion slice there (§8), not part of this repo's deliverable.

---

## 1. Decision (summary)

1. **`logosc --emit-docs [--only-file F] -o out.docwr <module-manifest>`** runs a real `sema_lower` (like `--emit-module`, `emit_module.cpp:520`) and walks the resulting `LProgram`, emitting a **doc-facts Writ container** conforming to the schemas in §2. Per-file mode mirrors `build_lib`'s `--only-file` so emission is incremental-friendly.
2. **The doc-facts container IS the Deem EDB.** Its schemas are designed so `QEnv::bind_source(name, arr)` (`deem.logos:562`) binds each fact table directly — no impedance layer. Dogfoods ADR 0011.
3. **Cross-references are Deem IDB rules** (§3), not renderer code: `implementors`, `used_by`, `reachable_pub`, `undocumented`, `impl_methods`. Adding a report = adding a rule.
4. **The tool's deliverable is `docs.json`** (§4): a *resolved* doc model — items plus their Deem-computed cross-references — serialized via `logos.mem.encoding.json`. **Rendering, styling, and markdown→HTML are out of scope** and owned by `logos-site`. Doc bodies travel as **raw markdown strings**; the site renders them.
5. **`lforge doc`** (§5) orchestrates: build → per-file `--emit-docs` → merge containers → run Deem rules → serialize `docs.json` → write it where the site can pick it up.
6. **Incremental rebuild** (§6) is the DBSP tie-in: per-file fact deltas → Deem re-derives only affected IDB rows → `docs.json` regenerates cheaply. This is the capability rustdoc structurally lacks; here it falls out of ADR 0013.
7. **First slice** (§8): one fixture package, `--emit-docs` emitting `DocItem`+`DocImpl`+`DocSigMention`, a golden fact-set oracle, three Deem rules, and a `docs.json` with a frozen versioned schema + golden — end-to-end, ctest-gated. The site-side processor is a companion slice in `logos-site`.

---

## 2. Data model — the doc-facts Writ schema (EDB)

Illustrative (final key-codes assigned at impl time). All are `pub schema` over TOMs (ADR 0011), so they round-trip through `wbs_write`/`wbs_read` and bind as Deem sources unchanged.

```
// One documented entity. id = FNV(path) — stable across runs, cross-file joinable.
schema DocItem {
    id:       i64  = 0,   // FNV1a(path)
    kind:     i32  = 0,   // module|fn|struct|enum|trait|impl|const|static|type_alias|variant|field|method|assoc_type|assoc_const
    path:     str,        // fully-qualified: "logos.lang.option.Option::map"
    name:     str,        // leaf: "map"
    parent:   i64  = 0,   // enclosing item id (0 = crate root)
    vis:      i32  = 0,   // priv|pub|pub(module)
    sig:      str,        // rendered via type_str: "fn map<U>(self, f: F) -> Option<U>"
    doc:      str,        // stripped doc text (RAW markdown source; may be "")
    src_file: str,
    src_line: i64  = 0,
}

// Impl-block facts — the raw material for `implementors`/`impl_methods`.
schema DocImpl {
    impl_id:    i64 = 0,
    trait_path: str,      // "" for inherent impls
    type_path:  str,      // the Self type
}

// Every type mentioned in an item's public signature — raw material for used-by / reachability.
schema DocSigMention {
    item_id:   i64 = 0,
    type_path: str,
}
```

Container layout: one root map with arrays `items`, `impls`, `sig_mentions`. `--emit-docs` populates it from the `LProgram` walk (`prog.structs`/`enums`/`traits`/`functions`, methods via `sd.methods()`, variants via `ed.each_variant`, fields via `sd.fields()`), reading `.doc()` on each view and reusing the `abi_type` wrapper (`emit_module.cpp:563`, strips `#[repr(transparent)] UnsafeCell<>`) over `type_str` for signatures. `module_inner_docs` → a `DocItem` of kind `module`.

---

## 3. Cross-references as Deem rules (IDB — this is the "помощнее")

Sketch (Deem/WQL surface; final syntax per ADR 0012). Each is a relation the serializer queries to enrich items; none is renderer code.

```
// Who implements this trait, and vice versa.
implementors(Trait, Type)   :- DocImpl(_, Trait, Type), Trait != "".

// Methods a trait impl provides (join impls × methods by parent).
impl_methods(ImplId, MethId) :- DocItem(MethId, method, _, ImplId, _, _, _, _, _).

// Reverse index: every item whose signature mentions a type (rustdoc's "used by").
used_by(Type, Item)         :- DocSigMention(Item, Type).

// Transitive public reachability — which types must appear in docs at all.
reachable_pub(T)            :- DocItem(_, _, T, _, pub, _, _, _, _).
reachable_pub(T2)           :- reachable_pub(T1), item_of(T1, I), DocSigMention(I, T2).

// Doc-coverage report (a query javadoc/rustdoc can't express without bespoke code).
undocumented(Path)          :- DocItem(_, _, Path, _, pub, _, "", _, _).
```

`reachable_pub` is recursive with SEMILATTICE/set semantics — admissible under the ADR 0013 recursion rules (transitive closure, no GROUP-in-recursion). This is the same class the incremental engine already maintains, so these relations are incrementally maintainable for free (§6). The serializer folds each item's `implementors`/`implements`/`used_by`/`undocumented` results into its `docs.json` object so the site receives them pre-resolved.

---

## 4. Output — the `docs.json` contract (site consumes; site renders)

The tool emits ONE artifact: `docs.json`, a resolved doc model. It is the cross-repo interface to `logos-site`, so it is **versioned and golden-tested**. Illustrative shape (keys frozen at impl time under `schema_version`):

```json
{
  "schema_version": 1,
  "package": "logos.lang.option",
  "items": [
    {
      "id": "logos.lang.option.Option",
      "kind": "enum",
      "path": "logos.lang.option.Option",
      "name": "Option",
      "parent": "logos.lang.option",
      "visibility": "pub",
      "signature": "enum Option<T>",
      "doc": "An optional value: `Some(T)` or `None`.\n\n## Examples\n...",   // RAW markdown
      "src": { "file": "stdlib/lang/option/option.logos", "line": 25 },
      "children": ["logos.lang.option.Option::map", "logos.lang.option.Option::Some"],
      "implements": ["logos.lang.cmp.PartialEq", "logos.lang.clone.Clone"],
      "used_by": ["logos.lang.result.Result::ok"]
    }
    // … modules carry kind:"module" and their //! inner doc; traits carry "implementors": [...]
  ]
}
```

Design rules for the contract:
- **Flat `items[]` + id links** (`parent`/`children`/cross-ref arrays reference `id`s). A flat, link-by-id model lets the site build any tree/index it wants without the tool prescribing page structure.
- **`doc` is raw markdown.** The tool never parses or renders it. The site runs it through its existing `markdown-it` (+ highlight.js for `logos` code, KaTeX for math). This keeps ONE markdown engine in the whole project and it is the site's.
- **Cross-refs pre-resolved by Deem** (`implements`/`implementors`/`used_by`) so the site does zero relational work.
- **`schema_version`** gates the site processor; a breaking shape change bumps it and the site pins a range.
- Serialized via `logos.mem.encoding.json` (`stdlib/mem/encoding/json/json.logos`) from the `lforge doc` step.

### 4.1 Markdown — resolved, not a gap

Earlier drafts treated "no markdown in the repo" as a fork. With rendering delegated to the site it is a **non-issue**: markdown bodies pass through as raw strings and the site's `markdown-it` renders them. No markdown engine is needed in Logos at all. (A native Logos markdown parser via `peg_gen` remains a *possible* future for non-site consumers — e.g. terminal `lforge doc --text` — but is explicitly out of scope here.)

---

## 5. Pipeline & artifact layout (`lforge doc`)

New subcommand, one branch in `main.logos` (~L189, beside `test`) + `cmd_doc` in a new `pkg/doc.logos` (auto-globbed by `CMakeLists:16`). Flow:

1. Build all lib targets (need a successful `sema_lower`); reuse `cmd_test`'s lib-topo-build (`cmd.logos:563-590`).
2. For each target, enumerate sources via `collect_logos_files` (`build_paths.logos:180`) and spawn `logosc --emit-docs --only-file <src> -o <doc-fact>.docwr <manifest>` per file (mirrors `build_lib`'s per-file fan-out, `builder.logos:145-174`).
3. Merge per-file containers into one EDB Writ (arrays concatenate; ids are content-stable so cross-file joins work).
4. Load EDB into `QEnv`, run the §3 rules, collect `QRows`; fold results into per-item objects.
5. Serialize to **`docs.json`** (via `logos.mem.encoding.json`) at **`.lforge/<profile>/doc/docs.json`** (`doc_dir_for(profile)`, new in `build_paths.logos` beside `out_dir_for`). Optional `--out <path>` to write straight into the site's ingest location.

Output: a single `docs.json`. Everything downstream (HTML, sidebar, search, styling) is `logos-site`.

### 5.1 Site-side (companion slice, in `logos-site` repo)
`build.mjs` gains a processor: read `docs.json`, group items into module pages, render each `doc` via `markdown-it`, emit `content`-equivalent API pages (or write generated `.md`/HTML into `dist/` under `/docs/api/`), and add the API tree to the sidebar. Templates and style are the site's. Tracked in that repo; not gated by this ADR beyond the schema contract.

---

## 6. Incremental rebuild (DBSP tie-in — deferred capability, designed-for now)

Because the EDB is per-file Writ fact tables and the cross-refs are Deem relations, an edit to one source file is a **±delta batch** over `DocItem`/`DocImpl`/`DocSigMention` for that file only. Feed it to the ADR 0013 incremental engine → it re-derives only the affected `implementors`/`used_by`/`reachable_pub` rows → `docs.json` regenerates from the maintained relations without a full re-walk. `logosc --version` stamp already forces full rebuild on toolchain change (`cmd.logos:104`). The first slice runs the **batch** Deem regime (queue-2 interpreter); the incremental regime is a drop-in third execution mode (ADR 0013 §1.6) once mature — no schema or rule change required. **This is the headline "помощнее": doc rebuilds are O(delta), which rustdoc cannot do.**

---

## 7. Open decisions

### 7.1 `docs.json` schema freeze (needs a joint call with the site)
The exact key names / nesting are a cross-repo contract. **v1 is specified in [docs/tooling/docs-json.md](../tooling/docs-json.md)** (format + invocation, since the tools are called from `logos-site`). It is a *draft* — producer-stable but not frozen. Proposal: co-design/freeze the v1 schema with `logos-site` off that spec, bump `schema_version` on breaks. This is the one thing that genuinely needs agreement across the two repos before the site processor is written.

### 7.2 stdlib `///` adoption (independent track)
stdlib has 0 `///` today (documents with plain `//` banners). The tool works on any package; stdlib docs are a separate migration (convert `//` file-header/item-preamble banners → `///`). Sequence it as its own effort after the tool exists so we dogfood on real input. Flag: some existing `//` banners carry rationale (`// CP-cm-17 …`) that is *not* user-facing doc — conversion is editorial, not mechanical.

### 7.3 Doc-tests (later slice)
`lforge test` already compiles standalone `tests/*.logos` (`cmd.logos:555`). Doc-tests = extract fenced ` ```logos ` blocks from `DocItem.doc`, synthesize test programs, route through the existing test path. Natural once the extractor exists; out of scope for slice 1.

### 7.4 Whole-package vs per-file emission
Chosen: **per-file** (`--only-file`) → aligns with §6 incremental and reuses `build_lib` fan-out. Cross-refs are recovered by Deem over the merged EDB, so per-file emission loses nothing.

---

## 8. FIRST SLICE — concrete spec

Goal: prove the full extract→resolve→serialize vertical on one fixture, ctest-gated, no half-measures on correctness. Ends at `docs.json`; rendering is the site's companion slice.

**Fixture**: a small package under `tests/logos/doc/` with `///` on a `pub struct`, a `pub fn`, a `pub trait`, and an `impl Trait for Struct` — including a doc-less pub item (to exercise `undocumented`) and a cross-type signature (to exercise `used_by`/`reachable_pub`).

**Compiler**: `logosc --emit-docs` (parse at `main.cpp:~3242` beside `--emit-abi`; dispatch a new mode running `sema_lower` then `emit_docs_container`). Emits `DocItem` + `DocImpl` + `DocSigMention` into a Writ container (`wbs_write` to `-o`, or SDN via `stringify` for golden readability). Signatures via `type_str`/`abi_type`; doc text via `.doc()`. Lives in the compiler binary (type_str is compiler-internal), sibling to `emit_abi_spec`.

**Oracle A (facts)**: golden `.docwr`/SDN of the fixture's fact set — differential, byte-checked, regenerated by a `logos-docs` custom target (mirrors the `logos-abi` target, `CMakeLists:327`).

**Deem**: three rules — `implementors`, `undocumented`, `used_by` — run over the emitted EDB via `Query::compile`/`bind_source`/`run`.

**Serialize**: fold query results into per-item objects and emit `docs.json` via `logos.mem.encoding.json`.

**Oracle B (resolved model)**: golden `docs.json` — assert `implements`/`implementors` reflect the fixture impl, `undocumented` names exactly the doc-less pub item, `used_by` links the cross-type signature, and doc bodies are the raw markdown verbatim.

**`lforge doc`**: wire the end-to-end command producing `docs.json`; a shell driver (`tests/lforge/doc_cmd.sh`, mirroring `test_cmd.sh`) builds the fixture package and asserts a well-formed `docs.json` with the expected items + cross-refs.

**Non-goals for slice 1**: incremental regime (batch Deem only), any rendering/HTML/markdown parsing (site's job), doc-tests, stdlib `///` conversion, search index, the site-side `build.mjs` processor (companion slice in `logos-site`).

---

## 9. Sequencing

1. ✅ **DONE 2026-07-05** — `--emit-docs` + fixture oracle (compiler, self-contained, ctest `logos_15_emit_docs`). See §10.
2. ✅ **DONE 2026-07-05** — `tools/docgen/docgen.logos`: `.docwr` → `docs.json` (schema_version 1), cross-refs resolved. ctest `logos_15_doc_json`. See §11. *(Deem-proper deferred — see §11.)*
3. ✅ **DONE 2026-07-05** — `lforge doc` (`tools/lforge/pkg/doc.logos`, `cmd_doc`): builds lib targets → `--emit-docs` per target → resolves to `.lforge/<profile>/doc/<name>.json`. ctest `logos_16_lforge_doc`. See §12.
4. **Joint**: freeze `docs.json` v1 schema with `logos-site`; site adds the `build.mjs` processor + templates (that repo).
5. *(later ADRs/slices)* incremental regime · doc-tests · stdlib `///` adoption · search · optional native markdown for non-site consumers.

---

## 10. Implementation notes — slice-1 (`--emit-docs`) landed 2026-07-05

`--emit-docs` is a modifier on `--emit-module` (sibling of `--emit-mlir`/`--emit-llvm`): it additionally writes `<output>.docwr`, a Writ-SDN doc-facts container, from the post-sema `prog` the ABI-layout block already walks. Files: `src/compiler/emit_module.{hpp,cpp}` (`emit_docs_facts`), `src/compiler/main.cpp` (`--emit-docs` flag → `EmitModuleOptions::emit_docs`). Test: `tests/lforge/emit_docs.sh` + ctest `logos_15_emit_docs`. Validated on the full `stdlib/lang` layer (exit 0, ~2900 facts, zero malformed paths).

Non-obvious findings (worth keeping — they cost iterations):
- **Two populations, two phases.** `prog.structs/enums/traits` decl mirrors (with fields, variants, doc-text) exist **pre-mono** and hold the source **templates** (`Option`, `Result` render as `enum Option`, not monomorphised instances) — correct for docs. But **functions** (`prog.functions`) at the pre-mono point carry the free fns and methods with **mangled** `name()`. Emit runs pre-mono, right after the ABI-layout block, before `mono_pass` moves `prog`.
- **`name()` is the mangled link symbol; the source name is `method_base()`.** A free fn `add` has `name()="…docdemo$add__f__i32__i32"`, `method_base()="add"`. Always display `method_base()`.
- **Method vs free-fn discriminant.** `impl_target_pattern()` is NOT populated pre-mono. The reliable signal is the **`self` first parameter** (owner = its stripped type — works for generic owners like `Result::unwrap`), with a **mangled-name fallback** (`<pkg>.<Owner>__<mbase>__f__…` vs free `…$<name>__f__…`) for `self`-less associated fns like `Vec::new`. Using only the mangle misclassified all generic-type methods (their head carries a `$`); using only `self` missed associated fns. Both together = correct.
- **Whole-module, not per-file.** Docs run one `--emit-docs` per package (no `--only-file`): cross-refs want the full surface, and per-file struct filtering isn't available (StructView has no `source_file()`). Per-file/incremental is deferred to §6.

Known slice-1 gaps (fast-follows, not blockers): struct/enum/trait/field/variant items have empty `src_file` (only FunctionView exposes `source_file()`); function `src_file` is an absolute path (normalise to repo-relative in the `lforge doc`/json step); trait per-method items not yet emitted (the `implementors` edge is — via `impls`); generic type-parameter lists are not yet rendered in signatures (`enum Option`, not `enum Option<T>`).

**Amendment 2026-07-06 — `kind: "macro"`.** The original emitter skipped `is_macro_hook()` fns entirely, which made the highest-leverage user surface — `deem!{}`/`trama!{}`/`schema_catalog!{}` — invisible in docs.json ("непонятно, какая функция отвечает за trama! и deem!"). Fixed: macro-hook fns now emit as `kind:"macro"` items (`signature: "macro <name>!"`), and they bypass the wql-internal package filter (the macro is the only consumer surface of those packages; everything else there stays excluded). Additive per the spec's §4 rule — no `schema_version` bump.

---

## 11. Implementation notes — slice-2 (`docgen`: `.docwr` → `docs.json`) landed 2026-07-05

`tools/docgen/docgen.logos` (~134 lines, plain Logos) reads a `.docwr` (Writ-SDN) via `parse_writ`, walks `items`/`impls` by key, folds the impl edges into per-item `implementors` (traits) / `implements` (types), collects the `undocumented` pub surface, and serialises **`docs.json` (schema_version 1)** via `logos.mem.encoding.json` (`JsonWriter`), writing with `logos.std.io.fs`. Test: `tests/lforge/doc_json.sh` + ctest `logos_15_doc_json` (jq-validates the JSON + cross-refs when jq is present). Validated at scale on the full `stdlib/lang` `.docwr`: **2480 items, valid 705 KB JSON, real implementor lists** (`Eq`:18, `Ord`:14, `Add`:12), `undocumented`:2431 (quantifies the §7.2 stdlib `///` gap).

The realised `docs.json` v1 shape (the cross-repo contract draft for §7.1 — NOT yet frozen):
```json
{ "schema_version": 1, "package": "docdemo",
  "items": [ { "kind","path","name","parent","visibility","signature","doc","src_file",
               "implementors":[…]  // traits only
               "implements":[…] } ],  // struct/enum only
  "undocumented": [ "<path>", … ] }
```

**Deem-proper is deliberately deferred (honest scoping, not a shortcut).** `implementors` is a group-by of the `impls` edge array and `undocumented` is a filter — neither needs Datalog, and forcing Deem to group a list would be ceremony (the engine is also still raw — user, 2026-07-05). Deem earns its keep on (a) the **transitive** relations (`reachable_pub`) and (b) **signature-mention joins** (`used_by`), both of which need the `DocSigMention` facts that slice-1 deferred. So the correct next Deem step is: emit `DocSigMention` from `--emit-docs` (slice-1b), THEN express `used_by`/`reachable_pub`/(and, uniformly, `implementors`) as Deem rules over a schema'd EDB via `Query::compile`/`QEnv::bind_source`. That also resolves the EDB-shape question: the current `.docwr` is string-keyed SDN (convenient for the plain-Logos walk); binding to Deem wants schema'd rows, so slice-1b should have `--emit-docs` emit schema TOMs (or docgen reconstruct them). Flagged for the joint schema-freeze (§7.1).

---

## 12. Implementation notes — slice-3 (`lforge doc`) landed 2026-07-05

`tools/lforge/pkg/doc.logos` adds `cmd_doc(&Manifest, profile)` + a `doc` branch in `main.logos` (dispatch + usage). Per lib target: `build_lib` (generates the `.module` manifest + resolves deps) → `logosc --emit-module <manifest> --emit-docs -o <docprefix>` (whole-module, no `--only-file`) → `read_to_string(<docprefix>.docwr)` → `docwr_to_json` → `write_string(.lforge/<profile>/doc/<name>.json)`. Test: `tests/lforge/doc_cmd.sh` + ctest `logos_16_lforge_doc` (full lforge project fixture with `///`, jq-validated docs.json). All 20 lforge ctests green.

`docgen` is a CMake binary target (`build/bin/docgen`, `tools/docgen/CMakeLists.txt`, added to the top-level `add_subdirectory` list) so `logos-site` can invoke it for non-lforge modules (the stdlib). Cross-repo invocation + format contract: [docs/tooling/docs-json.md](../tooling/docs-json.md).

Known slice-3 debt: `docwr_to_json` (+ `field_str`/`write_implementors`/`write_implements`) is duplicated between `tools/docgen/docgen.logos` and `tools/lforge/pkg/doc.logos` (~60 lines) — lforge and docgen are separate package trees; factor into a shared package (or make `lforge doc` shell out to the `docgen` binary) as a fast-follow. `--emit-docs` re-runs a full module compile (mono+codegen) purely for the doc facts; acceptable now, optimise later (emit docs as a byproduct of the per-file build once StructView exposes `source_file()` for per-file filtering, §6). One `docs.json` **per lib target** (`<name>.json`); a multi-target workspace gets N files — a combined index / merge is a fast-follow for the site.
