# Writ

> Scope: the Writ structured-data model — its heterogeneous value slot (`WAny`), the map/array/string containers, the `schema` / `schema enum` typed-view language over map-like Writ objects (ADR 0011), and the `@{…}` Writ data-literal notation. Writ is a metaprogramming/stdlib-level surface that ships and versions WITH the language but sits above the core compiler grammar/sema, so it has its own spec. Source layers: `stdlib/lang/writ/*.logos` (value model + containers: anyval, wmap, array, wstring, wstatic, static_view, container, document, typed_value, typed_arr); the `schema`/`schema enum` DECL grammar in `tools/peg_gen_cpp/grammars/logos.peg` (referenced from core [Grammar](grammar.md)/[Items](items.md), not restated here); schema sema/codegen in `src/compiler/sema_collect.cpp`, `src/compiler/sema_expr.cpp`, `src/compiler/sema_stmt.cpp`; the `@{…}` embedding grammar in `tools/peg_gen_cpp/grammars/writ.peg`; the schema-code scheme in `include/logos/writ/schema_codes.hpp`; ADR 0011. Each rule id is its permanent linkable address; ids never change across revisions.

## Value model

### `writ.value.wany-word` — WAny is one 8-byte tagged word

`WAny` is the Writ heterogeneous slot: a single 8-byte word that is either an inline primitive or a reference to a type-tagged zone object. It is a niche-packed `#[zoned] #[borrow_carrying] enum WAny { Ref(*const u8), Pod(u64) }` — the `AnyVal` analog, one word, `impl Copy`. `word == 0` is a null reference (`Ref(0)`); `word & 1 == 1` is a `Pod` (inline); `word & 1 == 0 && word != 0` is a `Ref`. Zone objects are ≥2-aligned, so a `Ref`'s low bit is always 0 and never collides with the `Pod` tag.

*Divergence:* EXTENSION — no Rust equivalent. Self-describing tagged word akin to a NaN-boxed dynamic value; Writ is Logos's serde-adjacent data substrate, not a Rust feature.

*Evidence:* `stdlib/lang/writ/anyval.logos#L16-L40`

### `writ.value.pod-encoding` — Pod inline encoding: i56 value + 7-bit code

A `Pod` word is `(value << 8) | ((code & 0x7F) << 1) | 1`: bit 0 = 1 (the Pod tag), bits[7:1] = a 7-bit Writ type code (1..127), bits[63:8] = a 56-bit signed inline value. Inline integers are therefore `i56`, not `i64`; a value that does not fit 56 signed bits (checked by `fits_i56`) must box into a `Ref`. `WAny::pod(v, code)` builds the word; `pod_code()` reads bits[7:1]; `as_i56()` is `raw() >> 8` (sign-extending).

*Evidence:* `stdlib/lang/writ/anyval.logos#L77-L79`, `stdlib/lang/writ/anyval.logos#L96-L97`, `stdlib/lang/writ/anyval.logos#L142-L146`

### `writ.value.pod-codes` — Inline Pod type codes

Inline `Pod` codes: `WA_I56=1` (generic inline integer, no exact width), `WA_BOOL=2`, and the exact-width integer codes `WT_I8=20`, `WT_U8=21`, `WT_I16=22`, `WT_I24=23`, `WT_U16=24`, `WT_U24=25` (each keeps its precise type across a round-trip). All exact-width inline integers fit the 56-bit Pod value, so none boxes. `WAny::from(v)` is overloaded per primitive and selects the code; `bool` uses `WA_BOOL`, a plain `i56` uses `WA_I56`.

*Evidence:* `stdlib/lang/writ/anyval.logos#L108-L119`, `stdlib/lang/writ/anyval.logos#L170-L181`

### `writ.value.ref-codes` — Ref (arena-object) type codes

A `Ref` `WAny` points at a tagged arena object; its in-band tag (the legacy WritTag varint written immediately before the object) identifies the pointee: `W_STRING=130`, `W_ARRAY=100`, `W_MAP=101` (string-keyed object map), `W_TINYMAP=98` (bitmap-indexed tiny map), `W_DECIMAL=102`, `W_TYPEDVALUE=4115`, plus boxed wide scalars `W_I64=26`, `W_U64=27`, `W_F32=30`, `W_F64=31`. `w_type_code(obj)` decodes the tag: `obj[-1]` in 1..222 is the code directly; a header byte >222 means `(header-223+1)` little-endian code bytes follow at `obj[-2]…`; 0 = unset.

*Evidence:* `stdlib/lang/writ/anyval.logos#L126-L140`, `stdlib/lang/writ/anyval.logos#L148-L165`

### `writ.value.type-code` — Unified type code and kind predicates

`WAny::type_code()` is the single dispatch point: 0 for null, the inline `pod_code()` for a Pod, or `w_type_code(resolve())` for a Ref. Kind predicates read it: `is_null`, `is_pod`, `is_ref`, `is_int` (inline i56/exact-width OR boxed W_I64/W_U64), `is_float` (boxed W_F32/W_F64), `is_string`, `is_array`, `is_map`, `is_tinymap`. `resolve()` returns the value-form absolute pointer (`raw() as *const u8`); it is safe to obtain (like Rust `as_ptr`), and dereferencing it is where `unsafe` lives.

*Evidence:* `stdlib/lang/writ/anyval.logos#L205-L213`, `stdlib/lang/writ/anyval.logos#L218-L238`, `stdlib/lang/writ/anyval.logos#L99-L100`

### `writ.value.absent-is-null` — Absent / null WAny decodes to the type's zero

A null or absent `WAny` decodes to the reading type's zero, never faulting: `as_i64`/`as_u64` → 0, `as_f32`/`as_f64` → 0.0, `as_bool` → false, a Ref accessor → a null-ref. This is the schema absent-key contract (a missing map key returns a null `WAny`, then the field type's zero).

*Divergence:* EXTENSION — sparse-store default; no Rust analog. Cross-ref [`writ.schema.absent-key`](#writschemaabsent-key--absent-schema-field-reads-as-the-types-zero).

*Evidence:* `stdlib/lang/writ/anyval.logos#L239-L250`

### `writ.value.at-rest-vs-value` — At-rest (self-relative) vs value (absolute) WAny forms

A `WAny` has two forms. The VALUE form (the plain `WAny`) holds a `Ref` as an ABSOLUTE pointer — movable, by-value, in registers. The AT-REST form is the same word stored in an arena slot reached through a `*zoned WAny` (e.g. `WArray`/`WMap` buffers), where a `Ref` is a SELF-RELATIVE delta `target − &slot`. The compiler owns the bridge: `*slot` materialises at-rest→value, `*slot = v` lowers value→at-rest; `Pod`/null are position-independent (identity). No hand-rolled relative math; the retired `WAnyRel` marker type is gone.

*Divergence:* EXTENSION — self-relative at-rest references support a never-move arena (Cap'n-Proto-like position independence); no Rust equivalent.

*Evidence:* `stdlib/lang/writ/anyval.logos#L8-L21`, `stdlib/lang/writ/anyval.logos#L290-L296`

### `writ.value.wanymut-cursor` — WAnyMut mutable element cursor

`WAnyMut` is the mutable dual of `WAny`: a fat `&mut` onto an element slot = `{ slot: *zoned mut WAny, zone: *mut u8 }`. `get()` materialises the slot to a value-form `WAny`; `set(v)` lowers a new value into the slot (replace in place, like Rust `&mut self[i]`). All predicates delegate to `get()`. `Array::get_mut` / `WMap<WString,WAny>::get_mut` produce it, cascading the parent's zone into the child for grow/intern.

*Evidence:* `stdlib/lang/writ/anyval.logos#L253-L288`

## Schema declaration

### `writ.schema.view-over-map` — A schema is a typed view over a map-like Writ object

A `schema` is to a map-like Writ object what a `struct` is to a flat byte layout: the same dotted-field syntax, but the backing store is a sparse, self-describing, schema-tagged map (a `WMap<Wu6,WAny>` TOM by default). Unlike a struct, fields are presence-keyed (a key may be absent), the key is a stable code, and the layout is forward/backward compatible — a new key leaves an old reader valid. The schema NAME itself is the typed view; there is no separate `FooView` type.

*Divergence:* EXTENSION — a protobuf/Cap'n-Proto/FlatBuffers-class typed message primitive expressed with ordinary field syntax; no Rust equivalent.

*Evidence:* `ADR 0011 §1`; `src/compiler/sema_collect.cpp#L4013-L4023`

### `writ.schema.decl-grammar` — Schema declaration grammar

`schema S <type_param_list>? <code_clause>? { field* }` where `field := pub? IDENT ':' type ('=' const_expr)? ','?`. Sema registers `S` as a Struct flagged `is_schema`. The `code(...)` and `category(...)` clauses use a CONTEXTUAL keyword (a bare IDENT validated `== "code"`/`"category"` in sema, so `code`/`category` stay usable identifiers). The core Logos spec does not carry a separate schema-decl rule — this is that rule.

*Evidence:* `tools/peg_gen_cpp/grammars/logos.peg#L1207-L1222`, `src/compiler/sema_collect.cpp#L4039-L4048`

### `writ.schema.synthetic-view-fields` — A schema's real layout is the fat view `{m, z}`

A schema view's ONLY real struct fields are synthetic: `m: *const WMap<Wu6,WAny>` (the backing TOM pointer) and `z: *mut u8` (the arena allocator, for boxing wide values on write). Together they are a 16-byte fat view identical to stdlib `WSchemaH`. Declared fields do NOT occupy struct offsets — they live only in the parallel `schema_fields` / `schema_keys` tables and are surfaced by desugared get/set. `z` is null for a read-only view bound from an erased `WAny`.

*Evidence:* `src/compiler/sema_collect.cpp#L4055-L4066`; `stdlib/lang/writ/wmap.logos#L410-L416`

### `writ.schema.field-key-code` — Field keys: explicit `= N` or positional index

Each field's key is the `= const_expr` (CTFE-evaluated) when present, else the running positional index (starting at 0, advancing to `key + 1` after each field). Keys are TOM `u8` codes and must lie in `0..51`; a key `< 0` or `> 51` is a hard error `"key N out of TOM range 0..51"`. A key equal to an earlier field's is a hard error `"duplicate key N"`.

*Divergence:* EXTENSION — explicit stable field numbers (protobuf-style) with a positional-default fallback; no Rust equivalent.

*Evidence:* `src/compiler/sema_collect.cpp#L4068-L4102`; `tests/logos/fail/schema_key_range.logos`, `tests/logos/fail/schema_dup_key.expected`

### `writ.schema.type-code` — schema_type_code from the `code(expr)` clause

An optional `code(const_expr)` clause sets the schema's `schema_type_code` (CTFE-evaluated to a `u64`); absent ⇒ 0. This code is stamped into the backing TOM header (`WMap<Wu6,WAny>.schema_code`) at construction and is the schema's global identity — read from the pointee, never stored separately. `schema_type_code()` / `set_schema_type_code()` on the TOM read/write it.

*Evidence:* `src/compiler/sema_collect.cpp#L4041-L4053`; `stdlib/lang/writ/wmap.logos#L339-L341`

### `writ.schema.category-variant-mask` — schema_type_code packs category(16) | variant(48)

`schema_type_code` is `category(16 bits) | variant(48 bits)`: `CATEGORY_SHIFT = 48`, `CATEGORY_MASK = 0xFFFF << 48`, `VARIANT_MASK = (1 << 48) − 1`. `category_of(code) = code & CATEGORY_MASK`; `variant_of(code) = code & VARIANT_MASK`. Predefined categories: `CAT_UNSET=0`, `CAT_AST=1`, `CAT_TYPE=2`, `CAT_LIR_EXPR=3`, `CAT_LIR_STMT=4`, `CAT_LIR_PAT=5`, `CAT_SYMBOLS=6`, `CAT_DIAG=7` (each `<< 48`). The scheme is globally unique and kind-independent, so a `code` identifies its logical schema under any backing.

*Evidence:* `include/logos/writ/schema_codes.hpp#L17-L47`

### `writ.schema.field-read-sugar` — Field read `p.f` ⇒ `WritField::from_wany(m.get(KEY))`

A schema field read `p.f` desugars to `T::from_wany((&*self.m).get(KEY))`: read the synthetic `m`, reinterpret to `&WMap<Wu6,WAny>`, `get(KEY)` yielding a `WAny`, then convert to the field type `T` via the `WritField` trait. An absent key returns a null `WAny` (→ the type's zero, per [`writ.schema.absent-key`](#writschemaabsent-key--absent-schema-field-reads-as-the-types-zero)). A name that is not a declared schema field falls through to the ordinary struct path (which handles the real synthetic `m`).

*Evidence:* `src/compiler/sema_expr.cpp#L9737-L9770`; `tests/logos/pass/schema_read.logos`

### `writ.schema.field-write-sugar` — Field write `p.f = v` ⇒ `m.set(KEY, WritField::to_wany(v, z))`

A schema field write `p.f = v` desugars to `self.m.set(KEY, T::to_wany(v, z))`: convert `v` to a `WAny` via `WritField::to_wany(self, z)`, then `set(KEY, …)`. `z` is the view-carried arena allocator, passed for boxing wide values (i32/u32/i64/u64/f32/f64/str) and interning strings; inline conversions ignore it. A write mismatched against the field type is a hard error `"schema write 'p.f': expected …, got …"`.

*Divergence:* EXTENSION — writing a boxed field needs the view's arena (`z`); this is why a mutating view carries the allocator. No Rust analog.

*Evidence:* `src/compiler/sema_stmt.cpp#L7256-L7305`; `tests/logos/pass/schema_write.logos`

### `writ.schema.writfield-trait` — WritField is the WAny↔T field conversion seam

Field conversions go through `trait WritField { fn from_wany(v: WAny) -> Self; fn to_wany(self: Self, z: *mut Allocator) -> WAny; }`. Stdlib impls cover `bool`, `str`, `WAny` (identity), all integer widths (i8..i64/u8..u64/i24/i56/u24/u56/isize/usize), and floats (f32/f64). Inline-fitting values ignore `z`; wide/boxing values (i32/u32/u56/u64/isize/usize/f32/f64) allocate through it; `str` interns via `wstring_in_alloc(z, …)`. A user type becomes schema-storable by implementing `WritField`.

*Divergence:* EXTENSION — a serde-`Serialize`/`Deserialize`-shaped seam specialised to the `WAny` slot; extensible per type.

*Evidence:* `stdlib/lang/writ/wmap.logos#L418-L465`

### `writ.schema.str-field` — str schema field: intern on write, decode on read

A `str` schema field writes by interning the string into the view's arena (`WAny::ref_to(wstring_in_alloc(z, s))`) and reads by decoding the interned `WString` back to a `str` (`v.as_wstr()`, null-safe → empty). A `str` field resolves to `WritField for str` (its `writfield_type_name` maps `Slice<u8>` → `"str"`).

*Evidence:* `stdlib/lang/writ/wmap.logos#L439-L444`; `src/compiler/sema_expr.cpp` (`writfield_type_name`, `case K::Slice: return "str"`); `tests/logos/pass/schema_str.logos`

### `writ.schema.wany-field` — WAny schema field is dynamic (identity, no conversion)

A field typed `WAny` stores and reads any `WAny` verbatim — the `WritField for WAny` impl is the identity, no conversion, no boxing. This is the heterogeneous/dynamic field: `c.v = WAny::from(42i56)` then `c.v = WAny::from(true)` re-tags the same slot, and an absent `WAny` field reads back null.

*Evidence:* `stdlib/lang/writ/wmap.logos#L445-L448`; `tests/logos/pass/schema_wany.logos`

### `writ.schema.absent-key` — Absent schema field reads as the type's zero

Reading a schema field whose key is absent from the backing TOM yields a null `WAny`, which each `WritField::from_wany` maps to the field type's zero: `i64` → 0, `bool` → false, a Ref type → null, `str` → empty, `WAny` → null. No fault, no `Option` — the sparse store's default.

*Divergence:* EXTENSION — forward/backward compatibility: an old reader of a newer message sees absent new fields as zeros. Cross-ref [`writ.value.absent-is-null`](#writvalueabsent-is-null--absent--null-wany-decodes-to-the-types-zero).

*Evidence:* `stdlib/lang/writ/anyval.logos#L241`; `tests/logos/pass/schema_read.logos`

### `writ.schema.generic` — Generic schemas resolve field conversion at monomorphization

`schema Wrap<T: WritField> { val: T = 0 }` binds `T` as a TypeVar; a field typed `T` stays symbolic in the generic body and is substituted with the receiver's concrete type-args at a use-site (`Wrap<i64>` → `T` becomes `i64`). The read/write sugar emits the BARE `T__from_wany`/`T__to_wany`, which mono retargets to the concrete `WritField` impl at instantiation. A type param without a `WritField` impl is a hard error `"does not implement trait 'WritField'"`.

*Divergence:* EXTENSION — generic message fields over a dynamic slot, resolved by monomorphization; no Rust equivalent.

*Evidence:* `src/compiler/sema_expr.cpp#L9744-L9757`, `src/compiler/sema_stmt.cpp#L7267-L7277`; `tests/logos/pass/schema_generic_str.logos`, `tests/logos/fail/schema_generic_not_writfield.expected`

### `writ.schema.generic-instance-code` — Per-instance schema_type_code for a generic schema

A generic schema/enum's concrete instance derives its `schema_type_code` from the base `code`'s CATEGORY plus a VARIANT computed by hashing the canonical concrete name: `variant = type_hash_56bit(type_hash_23("pkg::ConcreteName")) & VARIANT_MASK`, then `(base_code & CATEGORY_MASK) | variant`. A non-generic schema uses `base_code` unchanged. `make`, `view_checked`, and schema-enum `match` all share this `schema_instance_code` helper, so a produced node's stamped code matches what a consumer checks.

*Evidence:* `src/compiler/sema_expr.cpp#L9337-L9342` (`schema_instance_code`)

### `writ.schema.impl-and-traits` — A schema name is an ordinary type for impl / trait purposes

The schema name is an ordinary type: `impl S { … }` and `impl Trait for S` need no special casing; methods take `self: &S` / `&mut S` and use both the field sugar and raw `self.m.get`/`set`. A schema may be a trait bound (`fn walk<S: WritNode>(n: &S)`). No `dyn` schema exists; open runtime polymorphism is `WAny` + an explicit checked bind, closed sets are a `schema enum`.

*Evidence:* `ADR 0011 §5`; `tests/logos/pass/schema_impl.logos`

## Schema enum and dispatch

### `writ.enum.decl` — schema enum is a closed union over schemas discriminated by the pointee

`schema enum E <type_param_list>? <category(expr)>? { V(S), … }` is a closed union whose variants are OTHER concrete schemas; a value is a view `{m, z}` onto the TOM of one variant. It is NOT a flat Logos `enum`: no discriminant is stored in the value — the variant is read from the POINTEE's own `schema_type_code`. Sema flags it `is_schema` + `is_schema_enum`; the optional `category(expr)` clause sets the enum's category. Each variant maps a name → its concrete schema view type.

*Divergence:* EXTENSION — a self-identifying tagged union where the tag lives in the pointee (single source of truth), not the handle; no Rust analog.

*Evidence:* `src/compiler/sema_collect.cpp#L4109-L4159`; `tests/logos/pass/schema_enum_match.logos`

### `writ.enum.match-dispatch` — match on a schema enum is an if-chain on schema_type_code

`match e { E::V(b) => …, _ => … }` over a schema enum desugars to: `let __sm = e.m; let __code = (&*__sm).schema_type_code();` then an if-chain comparing `__code` to each variant's per-instance code (`schema_instance_code`), binding `b` to the concrete variant view `V { m: __sm, z }` in the matched arm. The discriminant is never stored; it is read from the matched node itself. Or-patterns in a schema-enum arm are not yet supported (`"or-patterns not supported yet"`); an arm pattern must be `E::Variant(b)` or `_`.

*Evidence:* `src/compiler/sema_stmt.cpp#L8335-L8340`, `src/compiler/sema_stmt.cpp#L8387-L8443`, `src/compiler/sema_stmt.cpp#L8490-L8500`; `tests/logos/pass/schema_enum_match.logos`

## Typed-view binding

### `writ.view.make` — `wr.make::<S>()` allocates and stamps a schema view

`wr.make::<S>()` produces a fresh schema view over a newly allocated, code-stamped TOM. It desugars to `wr.make_schema_h(cap, S::CODE)` — a `WSchemaH { m, z }` — reinterpreted (retyped, identical layout) as `S`. `cap` is the schema's declared field count (min 1). The returned view carries the arena allocator `z`, so its fields are immediately writable (boxing/interning work). For a generic instance, `S::CODE` is the per-instance code.

*Divergence:* EXTENSION — a typed constructor over a self-describing map (protobuf-message-builder-shaped); no Rust equivalent.

*Evidence:* `src/compiler/sema_expr.cpp#L9247-L9265`; `stdlib/lang/writ/wmap.logos#L399-L407`; `tests/logos/pass/schema_read.logos`

### `writ.view.trusted-bind` — `.view::<S>()` is an unchecked (trusted) bind

`.view::<S>()` (alias `.child`) binds a receiver to the schema view `S` WITHOUT a `schema_type_code` check — the producer is trusted. A `WAny` receiver is `resolve()`d to `*const WMap<Wu6,WAny>`; a `&WMap`/`*WMap` receiver is reinterpreted directly; the result is `S { m, z }`. Use it where the type is statically known (a concrete child schema inside an already-bound tree), where the code check is provably redundant.

*Divergence:* EXTENSION — the trust-boundary escape hatch; parallels an unchecked downcast. Cross-ref [`writ.view.checked-option`](#writviewchecked-option--viewcheckeds-returns-options-verifying-the-code).

*Evidence:* `src/compiler/sema_expr.cpp#L9324-L9339`; `tests/logos/pass/schema_read.logos`, `tests/logos/pass/schema_enum_match.logos`

### `writ.view.checked-option` — `.view_checked::<S>()` returns `Option<S>`, verifying the code

`.view_checked::<S>()` is the checked bind from an erased or external `WAny`: resolve to the TOM pointer, read `(&*p).schema_type_code()`, and yield `Option::Some(S { m: p })` iff it equals `S::CODE`, else `Option::None`. The check happens ONCE, at the trust boundary. This is the safe downcast from untrusted input; pattern-match the `Option` to consume.

*Divergence:* EXTENSION — a checked downcast returning `Option<S>` (like a fallible `TryInto`), specialised to the schema-code identity; no Rust equivalent.

*Evidence:* `src/compiler/sema_expr.cpp#L9267-L9322`; `tests/logos/pass/schema_view_checked.logos`

### `writ.view.check-policy` — Check policy is decided by the static type, not a flag

Whether `schema_type_code` is verified follows from the access's static type: an external blob / erased `WAny` (`node.view_checked::<S>()`) is checked once; a concrete child-schema edge inside an already-bound tree (`.view::<S>()`) is trusted (check provably redundant, elided); a `match` over a `schema enum` checks once (the match itself); a `WAny`-typed field defers the check to the `.view_checked::<S>()` at use. Erased/external input ⇒ check unavoidable; a bound concrete child ⇒ check elided. The ADR's planned `.as::<S>()` / `.as_trusted::<S>()` bind forms are NOT shipped — `make` / `view` / `view_checked` are the shipped surface (the code wins over the ADR).

*Evidence:* `ADR 0011 §4`; `src/compiler/sema_expr.cpp#L9247-L9339` (only `make`/`view`/`view_checked` handled)

## Typed edges

### `writ.edge.wref` — `WRef<S>` is a typed erased graph edge

`WRef<S>` (stdlib `pub struct WRef<S> { h: WAny }`) is the one graph-edge primitive: a single `WAny` ref handle whose phantom type param `S` names the target schema family. `WRef::<S>::from_any(h)` wraps an erased handle; `.any()` returns it. `S` is compile-time-only documentation of the edge target — the stored value IS the `WAny`.

*Divergence:* EXTENSION — a typed pointer into a Writ graph (Cap'n-Proto struct-pointer-shaped); the type is phantom, no Rust equivalent.

*Evidence:* `stdlib/std/wql/ir.logos#L29-L43`

### `writ.edge.wref-writfield` — `WRef<S>` is a schema field via identity WritField

`impl<S> WritField for WRef<S>` is the identity over the underlying `WAny`: `from_wany(v) = WRef { h: v }`, `to_wany(self) = self.h`. A `lhs: WRef<S>` schema field therefore stores/loads exactly like any other field with zero conversion — the edge value is the target's raw `WAny` ref. `writfield_type_name` routes a struct field type through its BARE template name (`WRef`), which mono retargets to the concrete `WRef$G1$S__from_wany`/`__to_wany` at the call.

*Evidence:* `stdlib/std/wql/ir.logos#L45-L49`; `src/compiler/sema_expr.cpp#L9358-L9373` (`writfield_type_name` struct case)

### `writ.edge.resolve-view` — Consume a `WRef<S>` via `.any().view::<S>()`

A consumer resolves an edge to a concrete view with `r.any().view::<S>()` — take the erased `WAny`, then trusted-bind it to `S` (the child type is statically the edge target, so no code check). The producer wires an edge by writing the target node's raw `WAny` ref straight into the parent's map slot (`b.lhs = WRef::<S>::from_any(child.as_ref())`), often with no `WRef` value materialised at all. A node's `as_ref()` yields `WAny::ref_to((self.m as i64) as *const u8)` — the arena TOM outlives the local view, so the ref is not dangling (provenance laundered through `i64`).

*Evidence:* `stdlib/std/wql/ir.logos#L80-L89`, `stdlib/std/wql/ir.logos#L149-L166`

### `writ.edge.readonly-mono-seam` — A read-only `WRef<S>` field forces its generic methods to be cloned

A compilation unit that only READS a `WRef<S>` field emits a call to `WRef<S>::from_wany` (returns `Self`) yet materialises no `WRef<S>` value; without help, mono would never clone `WRef$G1$S`'s methods, and mlir-gen would fail with `"does not reference a valid function"`. The generic-WritField field-read seam fixes this: at the concrete-rewrite site, `record_needed_struct` schedules `instantiate_struct_templates()` to clone all of the receiver struct's methods — the receiver type is recovered from the impl-target pattern substituted with the call's args (works for any Self-shape, e.g. `WRef<S>`), with the method's own return type as a fallback for the `from_wany` case.

*Evidence:* `src/compiler/mono_clone.cpp#L3458-L3494`; `tests/logos/pub_lib/wref_field_pkg.logos`, `tests/logos/pass/wql_wref_field_pkg.logos`

## Data literal

### `writ.literal.at-brace` — The `@{…}` Writ data literal

Writ structured-data literals use the leading `@` sigil in expression position: `@null`, `@true`/`@false`, `@INT`/`@-INT`, `@FLOAT`, `@"str"`, `@{ k: v, … }` (map), `@[ v, … ]` (array). Only the OUTERMOST literal needs the `@`; nested values are plain. See the core [`expr.writ.outer-at-prefix`](expressions.md), [`expr.writ.sdn-literal`](expressions.md), and [`grammar.writ.*`](grammar.md) rules for the full syntax; this Writ spec adds only the encoding/embedding semantics below.

*Divergence:* EXTENSION — self-describing data-notation literals (SDN); no Rust equivalent.

*Evidence:* `tools/peg_gen_cpp/grammars/writ.peg#L79-L102`; core `expr.writ.outer-at-prefix`, `expr.writ.sdn-literal`

### `writ.literal.value-encodings` — Literal value kinds and their WAny encodings

A `@`-literal value lowers to: null; bool (Pod code 5-word); an integer inline as i24 when in `[-2^23, 2^23−1]`, else boxed i64; float (boxed f64); string; array (a homogeneous scalar array I8..F64 becomes a typed array, otherwise an object array); map (integer keys I32/U32/I64/U64 become a typed map, otherwise a string-keyed object map); an embedded type (a tiny map carrying kind/uid/name); or a capture/PARAM placeholder bound at runtime. See the core [`expr.writ-lit.value-kinds`](expressions.md) and [`expr.writ-lit.int-small-inline-else-boxed`](expressions.md); the `WAny` value words match [`writ.value.pod-encoding`](#writvaluepod-encoding--pod-inline-encoding-i56-value--7-bit-code).

*Evidence:* core `expr.writ-lit.value-kinds`, `metaprog.wany.int-i56-value-form`, `metaprog.wany.bool-value-form`

### `writ.literal.typed-collections` — Typed array / typed map literal forms

`@<Elem>[…]` is a typed dense array and `@<K>{…}` / `@<K,V>{…}` a typed map; the type params are syntax hints. A typed array resolves to `WArray<Elem>` (Elem one of I8/U8/I16/U16/I32/U32/I64/U64/F32/F64); a typed map to `WMap<K,V>` (K one of I32/U32/I64/U64, V defaults to `WAny`). The `writ.peg` runtime parser keeps the type params as hints and returns regular ARRAY/MAP nodes. See the core [`type.writ-arr.elem-set`](types.md) / [`type.writ-map.key-val-set`](types.md).

*Evidence:* `tools/peg_gen_cpp/grammars/writ.peg#L108-L129`; core `type.writ-arr.elem-set`, `type.writ-map.key-val-set`

### `writ.literal.captures` — `$`-captures and `$N` positional parameters

A `${expr}` or `$ident` capture inside a `@`-literal binds a runtime value into the tree; `$ident` captures of the same name share one value slot (deduplicated), `${expr}` captures never dedup (side effects). In the `writ.peg` embedding grammar a `$N` node is a positional PARAM placeholder (type_hash=127, tag 0xFF) for document templates. A capture-bearing `@`-literal's result type is `Rc<Writ>` (needs `use logos.lang.writ.tmpl;`), not `WritStatic`. See the core [`expr.writ-capture.*`](expressions.md) and [`expr.writ-lit.result-type`](expressions.md).

*Evidence:* `tools/peg_gen_cpp/grammars/writ.peg#L130-L133`; core `expr.writ-capture.ident-dedup`, `expr.writ-lit.result-type`

### `writ.literal.rodata-blob` — A capture-free `@`-literal is a WritStatic rodata blob

A `@`-literal with no captures has type `WritStatic` — a compile-time blob in rodata laid out `[u64 size][bytes]`, with `WritStatic.ptr` pointing past the size prefix at the first blob byte. The blob is Writ format: 8-byte SELF-RELATIVE `WAny` slots, string-keyed object maps (`WMap<WString,WAny>`), `WArray<WAny>` arrays, and `WString` strings — the same layout the parser builds. `writ_static_size(ptr)` reads the size 8 bytes before `ptr`; `WritStatic.size()` wraps it. See the core [`type.writ.lit-and-array-map`](types.md) and [`type.identity.wstatic-config`](types.md) (WritStatic literal type identity = its byte-hash).

*Evidence:* `stdlib/lang/writ/wstatic.logos#L1-L26`; core `type.writ.lit-and-array-map`, `type.identity.wstatic-config`

### `writ.literal.wview2-reader` — Read a rodata blob through WView2

A `WritStatic` blob is read with `WView2` (`static_view`): `wview2_from_ptr(hs.ptr)` builds the reader over the DocumentHeader at `base+0`, whose self-relative root `WAny` word materialises via `root()`. From the root, `map_get(node, key)` (string-keyed), `tiny_map_get(node, key)` (u8-keyed TOM), `array_len`/`array_get`, and the scalar/string reads follow `Ref`s via `resolve()` (position-independent). A node handle is a value-form `WAny` whose `resolve()` is its absolute address. The legacy 4-byte base-relative reader (`WritView`) decodes the OLD format and would read garbage against the current blob layout.

*Evidence:* `stdlib/lang/writ/static_view.logos#L1-L122`

## Containers

### `writ.container.writ-root` — Writ is the owned never-move root container

`Writ` is the owned, mutable root container (Vec/Box-like) for a never-move zoned document: it owns an `Allocator` (a segment arena) by value and places typed objects into it. A placed object never moves, so a pointer to it — and any self-relative zoned ref into it — stays valid for the container's lifetime; growth appends a fresh segment rather than reallocating. Both fields are `UnsafeCell` (interior-mutable, non-Freeze) so a shared `&Writ` may mutate the arena and root word. `writ_new(seg_size)` builds one (RAII-freed on drop); `set_root`/`root` hold the top-level `WAny` as a raw 8-byte word (0 = unset).

*Divergence:* EXTENSION — an arena/document root (bump-region owner); adjacent to a typed_arena, no direct Rust std analog.

*Evidence:* `stdlib/lang/writ/container.logos#L1-L48`

### `writ.container.tom` — `WMap<Wu6,WAny>`: the bitmap-indexed TinyObjectMap

`WMap<Wu6,WAny>` (alias `WTinyValMap`, the legacy TinyObjectMap, code `W_TINYMAP=98`) is a compact FIXED-capacity map of up to 52 small keys (`0..51`) → `WAny`. Its header packs `bitmap[0:51] | cap[52:57] | size[58:63]`, plus a separate `schema_code: u64` and a self-relative `data: *zoned mut WAny` value buffer kept in key order. Lookup is O(1): a key's value-array position is `popcount(bitmap & keys-below)`. `get(key)` returns null `WAny` for an absent/out-of-range key; `set(key, val)` is a thin `&mut` (fixed cap → never allocates), a no-op for `key ≥ 52` or a full map with a new key. `Wu6` is a pure type-level label for the 6-bit key. This is the DEFAULT schema backing.

*Evidence:* `stdlib/lang/writ/wmap.logos#L294-L379`

### `writ.container.object-map` — `WMap<WString,WAny>`: the string-keyed object map

`WMap<WString,WAny>` (alias `WValMap`, code `W_MAP=101`) is the JSON-object string-keyed hash map over the arena: open-addressing + linear probing + FNV-1a, interned `WString` keys, `WAny` values. It GROWS like a Rust `HashMap` — at load factor >0.75 it appends a fresh 2× entry buffer and rehashes every live entry, re-anchoring each self-relative slot through its absolute value (the old buffer stays dead until the container drops). `#[zone_mut]` makes a `&mut` a fat ref carrying the arena; a read `&` stays thin. Overloaded `set(key, v)` accepts i64/f64/bool/`&WString`/`&WArray`/`&WMap`/`str` (interning as needed).

*Evidence:* `stdlib/lang/writ/wmap.logos#L1-L66`, `stdlib/lang/writ/wmap.logos#L104-L120`, `stdlib/lang/writ/wmap.logos#L256-L273`

### `writ.container.dense-int-map` — `WMap<K,WAny>`: the dense integer-keyed map

`WMap<K,WAny>` where `K: WIntKeyTag` (i32/u32/i64/u64; wire codes MapI32AnyVal=3101 .. MapU64AnyVal=3104) is a dense FIXED-capacity int-keyed map: parallel `K[]` keys + `*zoned WAny[]` values with O(n) linear lookup (the map is small), a thin `&mut` (fixed cap → no allocation). The type-var-first spec overlaps the string/bitmap first-arg specs; struct-layout selection picks the more-specific spec, and the dense methods sit behind the `K: WIntKeyTag` bound (which `WString`/`Wu6` do not satisfy) so they never collide.

*Evidence:* `stdlib/lang/writ/wmap.logos#L471-L513`

### `writ.container.warray` — `WArray<WAny>` / `WArray<T>`: the heterogeneous vs typed array

`WArray` has two partial specialisations by element kind. `WArray<WAny>` (code `W_ARRAY=100`) is the heterogeneous JSON array: AT-REST `WAny` elements in a `*zoned mut WAny` buffer (each `*(buf.add(i))` materialises the slot's Ref-delta → absolute). `WArray<T>` (T: `WArrTag`, codes 2101-2110) is a TYPED homogeneous packed array of a primitive (plain position-independent `*mut T` elements). Both share a `#[zone_mut] #[pinned] #[zoned]` shape: a fat `&mut` carrying the allocator, a pinned header, a self-relative `data` delta. Growth appends a fresh 2× buffer and re-points `data`; the copy re-anchors each self-relative slot through its absolute intermediate (not a memcpy).

*Evidence:* `stdlib/lang/writ/array.logos#L31-L60`

### `writ.container.document-walk` — Document = Writ root + root WAny; walkers dispatch on type_code

A "document" is a `Writ` container plus its root `WAny` (`set_root`/`root`); walkers recurse from any `WAny`, dispatching on `type_code()` exactly like equal/stringify. `node_count(v)` counts every scalar/string/array/map node (a null root = 0); `depth(v)` is the max nesting (scalar/empty-container = 1). Container children are recursed via `is_array`/`is_map` + `resolve()`. This is the canonical traversal shape.

*Evidence:* `stdlib/lang/writ/document.logos#L1-L64`

### `writ.container.typed-value` — WTypedValue: an SDN datatype instantiation

`WTypedValue` (code `W_TYPEDVALUE=4115`) is a Writ SDN datatype instantiation with canonical textual form `@Type(params?) = <initializer>`: three at-rest `WAny` words — the type-name (Ref to an interned `WString`), the params (a `WArray<WAny>` or null), and the initializer value. Words are held as raw `i64` and accessed through `*zoned mut WAny` slots (materialise on read, lower on write). `typed_arr` builds `&[T] as <T>[]` casts into a dense typed array (`WArray<T>` tagged 2101-2110) rooted in a fresh `Rc<Writ>`.

*Evidence:* `stdlib/lang/writ/typed_value.logos#L1-L20`; `stdlib/lang/writ/typed_arr.logos#L1-L18`

## Pitfalls

### `writ.pitfall.wany-method-on-enum-ref` — A WAny accessor called as a method on a `&WAny` enum returns null in mlir-gen

Calling a `WAny` accessor as a `method_call` on a `&WAny` (enum) receiver returns null in mlir-gen: `gen_recv_struct` rejects enums, so the receiver is not materialised. The schema desugarings avoid this by routing through a resolved FREE call — the `resolve_wany` helper takes the enum by `addr_of_temp`, finds the `WAny__resolve` free-function candidate, and emits a plain `call` producing `*const u8` before any TOM access. Consumer-relevant wherever a schema/view path reads a `WAny` field or binds a `WAny` handle: prefer the free-call/resolve form over a bare `.method()` on a `&WAny`.

*Evidence:* `src/compiler/sema_expr.cpp#L9228-L9245` (`resolve_wany`); MEMORY (recurring trap noted in `project_writ_schemas`)

### `writ.pitfall.provenance-launder` — Launder a view→node WAny ref through `i64`

A node's `as_ref()` returns `WAny::ref_to((self.m as i64) as *const u8)`: the cast through `i64` launders the pointer provenance. The `WAny` ref targets the TOM in the Writ arena (which outlives the local view), so the returned ref is NOT a dangling reference to the temporary view — without the launder the borrow checker conservatively rejects the escaping raw ptr. Use this idiom (mirrored across every IR node's `as_ref` and `arg_any`) when a view method must yield an edge handle that outlives the view.

*Evidence:* `stdlib/std/wql/ir.logos#L149-L166`, `stdlib/std/wql/ir.logos#L91-L104`
