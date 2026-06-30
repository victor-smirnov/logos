# ADR 0011 — Writ schemas: typed views over map-like Writ objects

**Status:** PROPOSED (2026-06-29) — design fixed; implementation TOM-only first.
**Date:** 2026-06-29

## Context

Map-like Writ objects (`TinyObjectMap` / the Logos `WMap<Wu6,WAny>`, the dense
int-keyed `WMap<K,WAny>`, the string-keyed `WMap<WString,WAny>`) are accessed
today as untyped key→`WAny` stores. The compiler's own L-IR already imposes a
*schema* on the tiny map by hand — [`lir_schema.hpp`](../../include/logos/compiler/lir_schema.hpp)
defines, as C++ constants:

- per-node-class **field keys** (`Key = NamedCode<uint8_t>`, codes 0..51),
- per-node-class **variant codes** (`enum class Code`) packed into the map's
  `schema_type_code` via the category(16)|variant(48) scheme of
  [`schema_codes.hpp`](../../include/logos/writ/schema_codes.hpp).

This works but: (a) it is a *convention* enforced by `static_assert`, not a
language construct; (b) access is raw `m.get(KEY)`/`m.put(KEY,v)` — untyped, no
WAny↔T conversion, no check that a given map actually belongs to the schema;
(c) it is unavailable as a feature of the **Logos** language over
`WMap<Wu6,WAny>`.

`schema_type_code` is a first-class TOM field present in BOTH the C++ form and
the Logos `WMap<Wu6,WAny>` (one shared byte layout; see
[`tiny_object_map.hpp`](../../include/logos/writ/tiny_object_map.hpp) and
[`wmap.logos`](../../stdlib/lang/writ/wmap.logos)). The other map kinds do **not**
carry it yet.

Goal: lift `lir_schema` from "a C++ convention over TOM" to a **first-class
Logos construct** — a typed view over a map-like object — that generalises to
other map kinds **without a compat-breaking redesign** when they are added.

## Decision

### 1. `schema` is a language item: a "struct over a map"

A `schema` is to a map-like Writ object what a `struct` is to a flat byte
layout — the same dotted field syntax, but the backing store is a sparse,
self-describing, schema-tagged map.

|              | `struct`                | `schema`                                          |
|--------------|-------------------------|---------------------------------------------------|
| storage      | dense, fixed offset     | map: presence-keyed, key = stable code            |
| fields       | all present             | sparse (a key may be absent)                      |
| compatibility| layout-fragile          | forward/backward (new key ⇒ old reader still ok)  |
| tag          | none                    | `schema_type_code` (category\|variant)            |

The schema **name itself is the typed view** — no separate `FooView` type.
`&S` is a thin read view; `&mut S` is a fat view carrying the zone (allocator),
exactly like `#[zone_mut]` in [`wmap.logos`](../../stdlib/lang/writ/wmap.logos)
— a nested `set` must intern/grow in the same arena.

### 2. Grammar

```
schema_decl  := vis? "schema" IDENT store_clause? code_clause? "{" field* "}"
field        := vis? IDENT ":" type ("=" const_expr)? ","   // name : type = key
store_clause := ":" "store" "(" store_kind ")"              // default = tom
code_clause  := ":" "code" "(" const_expr ")"               // → schema_type_code (opt)

schema_enum  := vis? "schema" "enum" IDENT store_clause? cat_clause? "{" variant* "}"
variant      := IDENT "(" type ")" ","                      // Variant(ConcreteSchema)
cat_clause   := ":" "category" "(" const_expr ")"           // top 16 bits of code (opt)
```

- The **key** (`= const_expr`) is optional. Its type is set by the backing
  kind: `u8` (0..51) for TOM, integer for dense, and for string-backed schemas
  it **defaults to the field name as a string** when omitted.
- `store(...)` is optional, default `tom`. Reserving it now means adding
  `int`/`str`/… later is **additive**, not a syntax change.

### 3. `schema enum` — a closed union over schemas, discriminated by the pointee

A `schema enum` is **not** a normal Logos `enum` (which stores a discriminant +
payload in a flat layout). Its variants are *other schemas*; a value is just a
`WAny`/`WRef` to the TOM of one variant. The discriminant is **NOT stored
separately** — it is read from the **pointee's `schema_type_code`** (the
category\|variant scheme is already globally unique, so the node identifies
itself; single source of truth). `match` resolves the Ref, reads
`variant_of(schema_type_code)`, and dispatches.

### 4. Check policy follows from the type — "check where needed, skip where provably redundant"

Whether `schema_type_code` is verified is decided by the static type of the
access, not by a flag:

| access | statically known | check? |
|--------|------------------|--------|
| `node.as::<S>()` (root / external blob, trust boundary) | nothing | **yes, once** |
| `p.left : WRef<Path>` (concrete child schema) | exact child type | **no** — trust the producer |
| `p.val : WAny` (erased) | nothing | **yes**, at `.as::<S>()?` on use |
| `match … : schema enum` | variant from code | **yes, once** = the `match` itself |
| `node.as_trusted::<S>()` (hot path, asserted) | asserted by caller | **no** |

Erased `WAny` or external input ⇒ check unavoidable; a concrete child schema
inside an already-bound tree ⇒ check provably redundant, elided. Explicit
`p.left_checked()?` / `as_trusted::<S>()` are the escape hatches at the edges.

### 5. impl / traits

The schema name is an ordinary type, so `impl S { … }` and `impl Trait for S`
work with no special casing; methods take `self: &S` / `&mut S` and use both the
field sugar and raw `self.m.get/set`. A schema may be a trait bound
(`fn walk<S: WritNode>(n: &S)`). The compiler auto-implements a marker trait
`WritNode` (= `WritSchema`) per schema, supplying `const CODE`, the bind ops,
and the backing-store handle. Open polymorphism over arbitrary schemas at
runtime is `WAny` + a runtime check; closed sets are `schema enum`. No `dyn`
schema for now.

### 6. Forward-compat: separate "schema" from "backing store"

The single decision that prevents a future breaking redesign: desugaring talks
to a **store abstraction**, never to TOM directly.

```logos
trait WritMap {
    fn get(self: &Self, key: WKey) -> WAny;
    fn set(self: &mut Self, key: WKey, val: WAny);
    fn schema_type_code(self: &Self) -> u64;
    fn set_schema_type_code(self: &mut Self, code: u64);
}
```

- `WKey` = generalized key (`u8` for TOM, `i64` for dense, Ref-to-`WString`
  for string maps).
- Today the only `impl WritMap` is `WMap<Wu6,WAny>` (TOM). Adding a map kind =
  a new `impl`, **not** a change to the schema feature.

**Node identity = `(kind, code)`**, not just `code`:

- **kind** — which backing (TOM / dense / string / …). Recovered from the
  **allocation type tag** (`W_TINYMAP`, `W_MAP`, `MapI32AnyVal=3101`, … in
  [`wmap.logos`](../../stdlib/lang/writ/wmap.logos)), kind-independently, without
  knowing the header layout.
- **code** — `schema_type_code` inside the header (needs the kind to locate the
  field). Globally unique and kind-independent (category\|variant), so
  `code → kind` is statically known from the schema's declaration.

TOM-only today ⇒ `kind ≡ W_TINYMAP` is constant and folds out of dispatch (read
only `code`). The desugaring still expresses identity as the pair, so when other
kinds arrive the only addition is "read type-tag → kind" in the prologue **where
kind is not statically pinned** — additive.

### 7. Invariants locked now (so later additions don't redesign the feature)

1. Desugaring goes through `WritMap`; **TOM is hardcoded nowhere** except the
   single `impl WritMap for WMap<Wu6,WAny>` and the default backing.
2. `schema_type_code` is the **kind-independent global identity**; one `code` =
   one logical schema under any backing. Read from the pointee (single source).
3. **Locator protocol**: `schema_type_code` is found via "allocation type-tag →
   per-kind offset". We do **not** bloat int/string map headers with the field
   now; each kind adds the slot when it gains schemas. What is stable is the
   *locating rule* (tag→offset), not the field's presence everywhere. Adding the
   slot to a dense map later is a local ABI bump of that map (covered by the
   versioning/ABI tracking), not a redesign of schemas.
4. `schema enum` is **homogeneous** (all variants one kind) for now → dispatch
   reads only `code`. Mixed-kind unions come later by adding the tag→kind read
   to the `match` prologue; the identity-as-pair already permits it.

## Desugaring (TOM backing)

Backing = `WMap<Wu6,WAny>` ([`wmap.logos`](../../stdlib/lang/writ/wmap.logos));
WAny↔T conversions already exist.

```logos
// bind (trust boundary; checked once)
node.as::<Path>()
  ⇒ { let p = node.resolve() as *const WMap<Wu6,WAny>;
      if (&*p).schema_type_code() != Path::CODE { Err } else { Ok(Path{ m: p }) } }

// read  p.flatten (flatten: bool = 6) — conversion chosen by field type
p.flatten ⇒ self.m.get(6u8).as_bool()
p.kind    ⇒ self.m.get(0u8).as_i64()                       // i64
p.name    ⇒ &*(self.m.get(7u8).resolve() as *const WString) // WStr
// absent key → null WAny → the type's zero (false / 0 / null-ref)

// write  p.flatten = y (needs &mut Path; zone for boxed types)
p.flatten = y ⇒ self.m.set(6u8, WAny::from(y))
p.kind    = k ⇒ self.m.set(0u8, make_int(zone_of(self), k))
p.name    = s ⇒ self.m.set(7u8, WAny::ref_to(wstring_in_alloc(zone_of(self), s) as *const u8))

// descend  p.left (left: WRef<Path>) — child type known ⇒ NO check
p.left          ⇒ Path{ m: self.m.get(1u8).resolve() as *const WMap<Wu6,WAny> }
p.left_checked() ⇒ as .as::<Path>(), with the code check (untrusted input)

// match over a schema enum (one check = the match)
match e { Expr::Bin(b) => …, Expr::Lit(l) => …, }
  ⇒ { let p = e.resolve() as *const WMap<Wu6,WAny>;
      let v = schema::variant_of((&*p).schema_type_code());
      if      v == BinExpr::variant { let b = BinExpr{ m: p }; … }
      else if v == LitExpr::variant { let l = LitExpr{ m: p }; … }
      else { /* non-exhaustive / _ */ } }

// producer stamps the code
wr.make::<Path>() ⇒ let p = wr.tinymap(CAP); p.set_schema_type_code(Path::CODE); Path{ m: p, zone }
```

## Implementation plan (TOM-only first)

1. **Grammar**: `schema` + `schema enum` items
   ([grammar](../../tools/peg_gen_logos/pkg/grammar_parser.logos) + C++ parser).
2. **Sema**: new decl kind; validate key range 0..51 / uniqueness / `code`
   packing; register the schema name as a view type; auto-`impl WritNode`;
   resolve `recv.FIELD` and field-assignment against the schema
   ([`sema_expr.cpp`](../../src/compiler/sema_expr.cpp)).
3. **mlir-gen**: desugar field read/write, descent, bind, and `match` into
   `WritMap` (today: `WMap<Wu6,WAny>`) calls + WAny↔T conversions — the runtime
   already exists; codegen only routes.
4. **Store trait**: define `WritMap` + the one `impl` for `WMap<Wu6,WAny>`.
   Storage/runtime are otherwise untouched.

## Non-goals (this ADR)

- Non-TOM backings (dense int / string / arbitrary keys) — design reserves for
  them (§6/§7) but does not implement.
- Mixed-kind `schema enum`.
- `dyn` schema / runtime open polymorphism beyond `WAny` + explicit `.as::<S>()`.

## Consequences

- Compiler L-IR `lir_schema` becomes a candidate to migrate onto the language
  feature later (it is the motivating real-world schema set).
- A typed, forward-compatible message-type primitive over Writ TOM (protobuf /
  Cap'n-Proto-like shapes) with ordinary dotted field syntax.
- The `(kind, code)` identity + `WritMap` seam is the contract that keeps adding
  map kinds additive.
