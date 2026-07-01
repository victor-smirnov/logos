# ADR 0012 — moving wql!/trama! PARSING onto peg_gen_logos, with schema-emitting actions

**Status:** DESIGN NOTE (2026-06-30). Phase-1 investigation output for the
"peg-ify the WQL/Trama frontends" workstream. Companion to
[`0012-writ-query-language.md`](0012-writ-query-language.md) and
[`0012-impl-seam.md`](0012-impl-seam.md). No code changes in this phase.

The goal: replace the hand-written recursive-descent parsers under `stdlib/std/wql/`
(`el.logos` `ElParser`, `wql.logos` `QLexer`, `trama.logos` `TrParser`) with
**peg_gen_logos-generated** parsers, and teach peg_gen to emit **schema-based node
construction** (dogfood ADR 0011: `h.make::<S>()` + typed field writes) instead of raw
TOM `put`/`get`.

---

## 1. peg_gen_logos action model (as of today)

A `.peg` alt with an `=> { CODE: N_foo, NAME: $1, ... }` action lowers (in
`tools/peg_gen_logos/pkg/codegen.logos`) to, per matched alternative:

```logos
let node: &mut WMap<Wu6, WAny> = mk_node(doc, N_foo, cap);   // codegen.logos:2318
node.set(F_NAME, WAny::ref_to(cap1));                        // emit_action_field :2347
node.set(F_SRC_LINE, WAny::from(fline as i56));
return node as *const WMap<Wu6, WAny> as *const u8;
```

where `mk_node` (codegen.logos:879) is:

```logos
fn mk_node(doc: &Writ, code: i32, cap: i64) -> &mut WMap<Wu6, WAny> {
    let node: &mut WMap<Wu6, WAny> = doc.tinymap(cap);
    node.set(F_CODE, WAny::from(code as i56));
    node.set_schema_type_code(code as u64);        // <-- already schema-tagged
    return node;
}
```

**Key fact — the generic-tree it already builds IS a schema node.** A node created by
`mk_node` is `doc.tinymap(cap)` + `set_schema_type_code(code)` + `set(F_key, ...)` with
u8 keys. That is *byte-identical* to what ADR-0011 `h.make::<S>()` produces:
`Writ::make_schema_h(cap, S::CODE)` = `doc.tinymap(cap)` + `set_schema_type_code(S::CODE)`
(wmap.logos:403), and a typed write `n.field = v` desugars to
`(&mut *n.m).set(KEY, WAny::from(v))`. So **codes and keys are already arbitrary** (they
come from `%nodes` and `%fields`), and the generator can target *any* schema's code/key
set today, not a fixed logos-AST node set.

The action expression kinds it supports (ast.logos:74): `CAPTURE $n`, `ARRAY_CAPTURE $...`,
`FOLD_CAPTURE $0`, `BOOL_LIT`, `INT_LIT`, `STR_LIT`. Captures store a raw node/WString
pointer via `WAny::ref_to(...)`; scalar literals via `WAny::from(v as i56)` /
`WAny::from(1u8/0u8)`.

**What is missing for true ADR-0011 schema-emission:**
- **Named schema-typed construction.** Today it emits `doc.tinymap(cap)` +
  `node.set(F_key, ...)`, not `h.make::<S>()` + `n.key = v`. At the byte level these are
  equal; the mandate wants the *emitted source* to read as typed schema writes (so the IR
  the generator emits dogfoods ADR 0011 and stays checkable). This is **additive**: a new
  codegen mode / per-grammar flag that, instead of `mk_node`+`.set`, emits
  `let n: S = doc.make::<S>(); n.key = <cap>;`. Requires the grammar to name the *schema
  type* per alt (see §4), and a scalar-vs-boxed decision for int/f64 fields.
- **Boxed wide scalars.** `WAny::from(x as i56)` only fits inline pods. For `i64/u64/f64`
  schema fields the ADR-0011 write path boxes via the fat view's carried allocator
  (`z`). The generator's `make::<S>()` output already carries `z` (WSchemaH), so a typed
  `n.count = big_i64` write routes through the boxed path automatically — a benefit of
  switching to `make::<S>()`. In the current WQL IR all scalar fields are `i32`/`u8`/`WAny`
  (ir.logos), which are inline — so **no boxing is needed for the WQL/Trama grammars**.
- **String-ref fields.** A schema `name: str` write desugars to `wstring_in_alloc(z, s)` +
  `WAny::ref_to`. The generator already interns strings (`wstr_p`, codegen.logos:872) and
  can store them as ref WAny — so this maps directly.
- **WRef<S> edge fields.** A capture `$n` returning a child node pointer → a
  `WRef<S>` field: `n.lhs = WRef::<SExpr>::from_any(WAny::ref_to(cap_n))`. Same shape as
  the current `node.set(F_lhs, WAny::ref_to(capn))` (the WAny handle IS the at-rest value,
  ir.logos:46) — additive one-liner in the schema-emission mode.

**Verdict:** schema-emission is a **small additive** change to `emit_action` /
`emit_action_field` (a new mode selected per grammar), NOT major surgery. The node model,
codes, and keys are already generic. Nothing forces a fixed logos-AST node set.

## 2. Dependencies — peg_gen depends on stdlib-CORE only, NOT std/wql

peg_gen_logos `use`s (main.logos + pkg/*.logos): `logos.lang.str`,
`logos.lang.writ.{container,anyval,array,wmap,wstring}`, `logos.lang.panic`,
`logos.mem.{string,collections.vec}`, `logos.std.env`, `logos.std.io.fs`, plus its own
`peg_gen_logos.{version,ast,grammar_parser,codegen}`. It does **not** import
`logos.std.wql.*`. Source-level: no cycle.

**Build-ordering caveat (the real cycle risk).** The generated wql/trama parsers would
live under `stdlib/std/wql/`, which is compiled into `liblogos-std.a` (the `logos-std`
module has `root stdlib/std/`, auto-including every `.logos` beneath it — stdlib/std/logos.module:4).
The `peg_gen_logos` *binary* links `liblogos-std.a` (CMakeLists.txt:39). So if a normal
`cmake --build` ran peg_gen_logos to (re)generate `el_parser.logos` INTO std/wql, the
build of `liblogos-std.a` would depend on a binary that links `liblogos-std.a` — a cycle.

**Sever it exactly as peg_gen_logos already ships its own output:** check the generated
parser in as a committed source artifact under std/wql, and gate regeneration behind a
separate **opt-in target** (`wql_peg_regen`, DEPENDS logosc + peg_gen_logos), never run by
the default build. This mirrors how `logos_parser.logos` is regenerated offline and gated
by the `peg_gen_logos_oracle` target, not by `add_test`.

## 3. Output wiring + the oracle

- **peg_gen_logos output today:** written to a file by `main.logos` — `parse_grammar_string`
  → `codegen_logos` → `write_string(<out-dir>/<output>.logos)`. `<output>` = the grammar's
  `%meta output`. The C++ sibling's output (`logos_parser.cpp`) is generated into `build/`
  and linked into `logosc`; the Logos generator's `logos_parser.logos` is regenerated on
  demand (not checked in as the compiler's parser — the C++ one still is).
- **Oracle (`peg_gen_logos_oracle`, CMakeLists.txt:72):** an opt-in custom target (NOT a
  ctest) that runs `oracle/run.sh`: builds a C++ stringify harness (links the
  peg_gen_CPP-generated `logos_parser.cpp` + writ/verification/core) and a Logos harness
  (runs peg_gen_logos on `logos.peg` → `logos_parser.logos`, links it), parses every file
  in `tests/logos` with both, serialises each AST via `writ::stringify`, and diffs
  byte-for-byte. Currently **2056/2056 identical**. This is the HARD SAFETY INVARIANT: any
  change to peg_gen_logos must keep this green.

## 4. "peg_gen as a library" — running it on an arbitrary .peg

`main.logos` already accepts an arbitrary grammar path + `--out-dir`. Running it on
`el.peg` / `wql.peg` / `trama.peg` needs only:
1. **Grammars.** Author `stdlib/std/wql/grammars/{el,wql,trama}.peg` — the EL precedence
   grammar (a subset of the logos.peg expression chain), the outer wql clause grammar
   (`fn N from src:[RowTy] var (where PRED)? (join …|group …)? select PROJ (: ResultTy)?`),
   and the Trama template grammar. `%meta output` = `el_parser` / `wql_parser` /
   `trama_parser`.
2. **Schema-emission mode + schema binding.** Add a grammar directive (e.g. `%meta
   schema_emit on`, or per-alt `=> S { field: $n }` where `S` names the schema type) so
   `%nodes` map to ADR-0011 schema *types* (codes taken from the schema, not a raw
   `%nodes` int). The action then emits `h.make::<S>()` + `n.field = $n`. `emit_action`
   gains this mode; `emit_public_entries` builds into the caller's arena (`&Writ`) rather
   than allocating a fresh `writ_new` doc, so the wql!/trama! handler passes its `h`.
3. **Output path + regen target.** `--out-dir stdlib/std/wql`; wire a `wql_peg_regen`
   custom target that runs the three generations offline. Commit the outputs.

No new generator *entry point* is required beyond the schema-emission mode and letting the
generated `parse_<export>` take an external `&Writ` arena (today it does `writ_new`
internally, codegen.logos:2471).

## 5. NAME INTERNING — the load-bearing decision

This is the crux the goal flags. Today the WQL codegen (`stdlib/std/wql/codegen.logos`)
reads the IR **structurally via schema `match`** (`SExpr::Field(f) => f.key`,
`SExpr::Lit(l) => l.val`) — good, that survives. But it recovers the *textual* names/types
from the **`ElParser` dense tables**, NOT the nodes:
`p.field_name(f.key)`, `p.param_name(pp.idx)`, `p.field_ty(key)`, `p.param_ty(idx)`,
`p.var_name(idx)`, `p.param_is_source(idx)` (codegen.logos:131/166/172/180/233;
trama_render.logos:215/256). A generated parser has no `ElParser` and no dense tables, so
`p.field_name(key)` has nothing to resolve against.

Two options:

- **(A) reproduce the ElParser tables in the generated parser.** The generator would emit
  a companion table struct + interning logic. Heavy, un-idiomatic for a PEG action model
  (interning is a *semantic* pass, not a grammar action), and it re-creates the exact
  external-side-table coupling we want to remove.

- **(B) store names/types IN the nodes (RECOMMENDED).** Change the IR schemas so a field
  step carries its name and value-type directly, e.g. `SField { base: WRef<SExpr>, name:
  str, ty: i32 }` instead of `{ base, key: u8 }`; `SParam { name: str, ty: i32, is_src:
  bool }` instead of `{ idx: i32 }`; `SVar { name: str, ty: i32 }`; `SCall { fn_name: str,
  args }`. The generator's schema-emission then does `n.name = $ident` (a string-ref
  write it already supports) — the name lives in the node, self-describing. Codegen's
  `p.field_name(f.key)` becomes `f.name` (a schema read, no side table). The dense-id +
  external-table machinery in `el.logos` (intern_field/param/var, field_starts/lens, the
  `field_name`/`param_name`/`var_name`/`field_ty`/`param_ty` accessors) is DELETED.

**Recommend (B).** It is the clean resolution: the IR becomes self-contained (which is
also more faithful to ADR-0011's "the schema is the typed view" and to the seam's promise
that "backends trust the IR"), and it is exactly the kind of write the schema-emission
mode produces natively (string-ref field write). The type tags (`field_ty`/`param_ty`)
that the parser sets from `with x:str` / `$p:i64` declarations move onto the node's `ty`
field the same way. This is a one-time IR-shape change in `ir.logos` + a mechanical rewrite
of `codegen.logos` reads (and `trama_render.logos`).

**Note on ordering:** (B) can and should land *before* peg-ification, as a pure refactor
of the existing hand-written path (change ir.logos + el.logos to store names in nodes +
rewrite codegen reads; gate on the wql/trama ctest subset). Then the generated parser is a
drop-in that produces the same self-describing IR — de-risking the swap.

## 6. Recommended approach + sequencing

**Schema-emission is achievable additively without touching the logos.peg oracle path.**
The generator's node model is already generic (arbitrary codes/keys, `set_schema_type_code`);
schema-emission is a new *mode* that only activates for grammars that opt in
(`%meta schema_emit`), leaving the default `mk_node`/`.set` path — and thus the
logos.peg → `logos_parser.logos` output — byte-identical. The oracle stays green because
logos.peg does not use the new mode.

Recommended increments (each gated on `wql|trama|token_macro|schema|metaprog` ctest subset
+ the `peg_gen_logos_oracle`, built via `cmake --build`, per feedback_validate_with_cmake_build):

1. **IR self-describing refactor (option B), hand-written path only.** Move names/types
   onto SField/SParam/SVar/SCall; rewrite codegen.logos + trama_render.logos reads; delete
   the ElParser dense tables. Byte-for-byte behavior unchanged (same emitted Logos source);
   gate on the wql/trama e2e tests. *No peg_gen change yet — oracle untouched.*
2. **Schema-emission mode in peg_gen_logos** (additive `emit_action` branch + `&Writ`
   external-arena entry). Gate: `peg_gen_logos_oracle` still 2056/2056 (default mode
   unaffected) + a tiny throwaway schema.peg that round-trips.
3. **el.peg** → `el_parser.logos`, committed; `wql_peg_regen` target; swap the EL
   expression parse in wql.logos/trama.logos to the generated parser. Delete el.logos RD.
4. **wql.peg** (outer clause) + **trama.peg** (template) → generated parsers; delete
   QLexer + TrParser. The wql!/trama! HANDLERS + codegen are unchanged (they consume the
   same self-describing IR).

**Constraints to respect:** keep the logos.peg oracle green (new mode is opt-in);
`cmake --build` must have NO cycle and NOT run the generator (commit outputs + offline
regen target); verify new tests genuinely assert (the WQL corpus has false-passing
history); the wql!/trama! codegen (codegen.logos, trama_render.logos) stays — only the
PARSE step is swapped.
