# peg_gen_logos — schema-emission smoke test

Verifies the generator's **schema-emission mode** (ADR 0012 peg-frontend): a `.peg`
grammar that carries a `%schema` block makes peg_gen_logos build first-class Writ
schema views — `h.make::<S>()` + typed schema field writes + ref edges — dogfooding
ADR 0011, instead of raw `WMap<Wu6,WAny>` `put`/`get` tinymaps.

## Files

- `demo.peg` — a tiny arithmetic grammar in `%schema` mode. Builds the `demo_ir`
  IR (`ELit`/`EBin`, family `EExpr`) with ref edges (`ERef<EExpr>`), a scalar
  `op: i32`, a dynamic `val: WAny`, fold-mode left-assoc binary parsing, and an
  external-arena entry (`parse_expr(src, doc) -> WAny`).
- `demo_ir.logos` — a self-contained schema module the grammar targets.
- `harness.logos` — parses `"2*3+4"`, walks `EExpr`, asserts the operator tree
  `EBin(+, EBin(*, ELit, ELit), ELit)` (proves precedence + ref-edge round-trip +
  schema-enum `match`).
- `run.sh` — regenerates the parser, compiles + links + runs the harness, asserts
  `OK:`. Also greps the generated source for `doc.make::<` / `ERef::<` to prove
  schema mode actually fired (not the raw-TOM fallback).

## Run

```
cmake --build build --target peg_gen_logos_schema_test
# or
tools/peg_gen_logos/schema_test/run.sh [build_dir]
```

## The `%schema` directive

```
%schema {
    use:      "some.ir.module"   // module to import for the schema types
    arena:    external            // parse_<export>(src, doc: &Writ) -> WAny
    ref_wrap: "ERef"              // edge-wrapper type for `ref T` fields (default WRef)
    ELit  { val: "WAny" }
    EBin  { op: "i32", lhs: "ref EExpr", rhs: "ref EExpr" }
}
```

Each `S { field: "type", ... }` names a grammar node (`=> { CODE: "S", ... }`)
that is built as schema `S` (code = `S::CODE`). Field-type strings drive the
emitted write:

| field type    | emitted write                                         |
|---------------|-------------------------------------------------------|
| `"ref T"`     | `n.f = <ref_wrap>::<T>::from_any(hand_any(cap))` (edge)|
| `"str"`       | `n.f = <interned str>` (schema string field)          |
| `"WAny"`      | `n.f = hand_any(cap)` / verbatim literal (dynamic)    |
| `"i32"` etc.  | `n.f = <val> as T` (inline scalar)                    |

Presence of a `%schema` block turns on schema-emission mode for the whole
grammar. Grammars WITHOUT `%schema` (logos/writ/hrpc) keep the raw-TOM mode
byte-identical — that is what keeps `peg_gen_logos_oracle` green.

## Note on isolated compilation

`run.sh` merges the harness + generated parser + `demo_ir` into ONE compilation
unit before compiling. An isolated `--emit-module` of a package that only READS a
`WRef`/`ERef<S>` field (never constructs that `S`) trips a pre-existing, documented
compiler mono-enqueue bug (`from_wany`/`from_any` not enqueued cross-CU — see
`project_writ_query_language`). That bug is unrelated to the generator; single-CU
compilation is how `el.logos` etc. build inside `liblogos-std`, and it exercises
the generated schema-emission code identically.
