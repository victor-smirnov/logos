# ADR 0011 — Writ schemas: VERIFIED implementation plan (TOM-only)

**Status:** verified against `main` (a8c6006f) on 2026-06-29. De-speculated from six
subsystem recipes; every file/function/line below was confirmed against real code
unless explicitly marked RISK/UNKNOWN.

Companion to [`0011-writ-schemas.md`](0011-writ-schemas.md). Read that for the *what*;
this is the *how*, as gated increments.

> **STATUS (2026-06-29, branch `feat/writ-schemas`):** Increments **1, 3, 4, 5, 6, 7 DONE
> and ctest-gated** (schema enum + match works; boxed writes work). Inc 6: the schema
> view is a 16-byte `{m: TOM-ptr, z: *mut Allocator}` fat view — `z` carries the ARENA
> ALLOCATOR (not a `*Writ`). `make::<S>()` reinterprets a layout-identical stdlib
> `WSchemaH` (from `Writ::make_schema_h`) as `S` via `retype_expr`. Writes box wide
> values via `z` (`make_int`/`box_u64`/`box_f64`/`box_f32`); inline `WAny::from` is used
> when an overload exists (no allocator needed). Views bound from an erased WAny carry
> `z=null` → read + small/inline writes OK; wide writes need a make/handle-origin view. Three latent bugs found+fixed (had
> been masked by false-passing tests): (a) `CODE_EXPR` was key 52, out of the AST
> TinyObjectMap range 0..51 → `code(...)` was never stored (moved to slot 6); (b)
> WAny accessors (`as_i64`/`as_bool`/…) emitted as `method_call` on a `&WAny` enum
> receiver returned null in mlir-gen (`gen_recv_struct` rejects enums) → field-read
> values silently dropped → comparisons elided. Fixed: resolve the accessor symbol +
> emit a DIRECT free call (like the `WAny::from` write path). (c) Absent-key reads
> return a null-ref WAny; `as_i64`/`as_u64`/`as_f64` dereffed it → SEGV. Fixed: the
> accessors are now null-safe (null → 0), honoring the ADR's absent-key-default. Schemas parse, validate (key range/uniqueness), register as a
> view type, construct (`wr.make::<S>()`), bind (`x.view::<S>()` trusted),
> read (`p.f`), and write (`p.f = v`, inline-scalar values only). Verified
> deviations from the speculative recipes are captured in §0 (D1–D4) — notably the
> bind method is `.view::<S>()` not `.as::<S>()` (`as` is reserved), and the WritMap
> trait (Inc 2) is deferred (direct `WMap<Wu6,WAny>` calls; reserve the seam when a
> 2nd backing lands), and `view_checked::<S>() -> Option<S>` (checked bind: compares the
> pointee's `schema_type_code` to `S::CODE` once, returns `Some(S)`/`None`). **DONE** —
> built via block_expr(SLet __p; SLet __r = if_expr(...)) yielding `var_ref(__r)`; the
> ROOT BUG was `resolve()`/WAny accessors emitted as `method_call` on the `&WAny` enum
> receiver returning null (the recurring trap) — fixed with a resolved free call.
> **String (`str`) fields DONE** — write interns via the view allocator
> (`wstring_in_alloc(z,s)` → `WAny::ref_to` → `set`); read decodes null-safely via a new
> stdlib `WAny::as_wstr` (absent key → empty str). **Dynamic `WAny` fields DONE** — a
> field typed `WAny` stores/reads the value VERBATIM (identity — no conversion, no box;
> the heterogeneous/erased field). **The TOM-scope ADR is now COMPLETE.**
> Tests: `pass/schema_decl,schema_read,schema_write,schema_box_write,schema_enum_match,
> schema_view_checked,schema_str,schema_wany`, `fail/schema_dup_key,schema_key_range`.

> **FUTURE / PROPOSED (not scheduled — design TODO):**
> 1. **Narrow `code`/`category` to `u56`.** Currently `schema_type_code` is `u64`
>    (16-bit category in bits[48:63] + 48-bit variant). Re-cut to a 56-bit value so it
>    embeds directly into a `WAny` inline value (the i56 slot) — a code becomes carriable
>    *as data* with no boxing, saving both space and time. Slight code-space narrowing
>    (lose 8 bits). Reserve the **freed top 8 bits as a niche for enums** (schema-enum
>    discriminant / Option-of-view niche, à la the existing zoned niche-enum work). Touch
>    points: `include/logos/writ/schema_codes.hpp` (`CATEGORY_SHIFT`/`*_MASK`/ctors),
>    `category_of`/`variant_of`, the AST/LIR/type producers that stamp codes, and the
>    `view_checked` compare.
> 2. **Make the category mask a configurable parameter** rather than the hardcoded
>    `CATEGORY_MASK = 0xFFFF << 48`. Let the category/variant split point be tunable
>    (per-store or per-schema-family) so different code families can trade category width
>    for variant width. Folds together with (1) — the u56 re-cut is the natural moment to
>    parameterize the split.

---

## 0. Verified architecture (the load-bearing decisions)

These four decisions replace the recipes' speculation and de-risk the whole feature.

### D1 — A schema is a `Kind::Struct`, NOT a new `LogosType::Kind`. **VERIFIED.**

The recipes proposed `Kind::Schema = 15`. This is **wrong and dangerous**:

- `LogosType::Kind` (`include/logos/compiler/sema.hpp:48-134`) has **no explicit integer
  assignments** — values are positional. The ordinal is serialized into the Writ mirror
  via `writ::schema::type(int32_t(kind))` (`src/compiler/sema.cpp:263`;
  `include/logos/writ/schema_codes.hpp:30`) and read back by `kind()` (`sema.hpp:261`).
  In-source comments (sema.hpp:74/108/115) mandate **appending after the last value
  (`FnItem`, sema.hpp:116)** to keep blob/ABI identity stable. Inserting at 15 renumbers
  `Ptr/Ref/MutRef/Struct/…` and breaks every `.writ0` blob.
- A genuinely new Kind would touch a large fraction of: **88** `switch(...kind())`, **712**
  `case Kind::`, **2312** `.kind() ==` sites in `src/compiler/`.

**Decision:** a `schema S {…}` registers a normal struct named `S` in `structs_` with a
single synthetic field `m: *const WMap<Wu6, WAny>`, plus a new `bool is_schema` (and the
schema's key/type table) recorded alongside. The type resolves, gets `impl` blocks, is
passed by value as an 8-byte pointer-shaped aggregate, and round-trips — all through
existing machinery, **zero** `.kind()`-site changes. This is exactly the grain used by
`zone_mut`/`zoned2`/`rel_ptr`/`non_null`/`repr_transparent` (per-struct bool flags) and
is structurally identical to the hand-written runtime view `WView2`
(`stdlib/lang/writ/static_view.logos:35`, `pub struct WView2 { pub base: *const u8 }`).

### D2 — mlir-gen needs ZERO new code. **VERIFIED.**

Schema field access desugars *in SEMA* into ordinary LIR method-call / field-read nodes
(`recv.m.get(key).as_bool()`). mlir-gen's `gen_expr_kind(EMethodCallView)`
(`src/compiler/mlir_gen_expr.cpp:2544`) reads `resolved_symbol` off the node
(`:2548/:2661`), looks it up with `find_func_op` (`:2666`), emits `func::CallOp` (`:2800`)
— **no schema knowledge**. Precedents where SEMA already synthesizes resolved calls:
operator overloading (`sema_expr.cpp:1947-2005`, builds `builder().call(symbol,…)`),
tuple-eq with a *synthetic receiver chain* + explicit `resolved_symbol` + `addr_of_temp`
(`sema_expr.cpp:4509-4542`), index overload → `EMethodCall` (`:10520`), `str_eq`/`str_cmp`
(`:2229/2258`). Mono picks up synthesized calls automatically (`mono_scan.cpp:202-218`).

**Two ways to emit; prefer (a):** (a) build a synthetic *AST* node and re-enter
`lower_method_call`/`lower_field_read` — gets type inference, autoderef, mangling, mono,
borrow-check for free; (b) build the `EMethodCall` LIR directly with a pre-computed
`resolved_symbol` (tuple-eq pattern) — you must mangle yourself and auto-ref the receiver
(`addr_of_temp(recv,false,make_ref(false,field_t))`) and substitute the field type. Use
(a) unless a measured reason forces (b).

### D3 — `as` is a reserved keyword; `node.as::<S>()` does NOT parse. **VERIFIED — RISK.**

`KW_AS = "as"` (`tools/peg_gen_cpp/grammars/logos.peg:368`), used for casts
(`cast_expr <- unary_expr (KW_AS type_ref …)`, logos.peg:2643). The lexer emits keyword
literals before the IDENT regex; `as::` passes the word-boundary check and tokenizes as
`KW_AS`. Method-call grammar alts are all `DOT IDENT …` (logos.peg:2663-2689); there is no
`DOT KW_AS` alt. (Proof it matters: `new`/`null` needed explicit `DOT KW_NEW`/`DOT KW_NULL`
carve-outs at logos.peg:2674-2689.)

**Decision for this plan:** name the bind method **`.view::<S>()`** (read view) and
**`.view_checked::<S>()`** (returns `Option`), and the trusted descent **`.child::<S>()`**.
`make` stays `Writ::make::<S>()` (parses cleanly as `STATIC_CALL`, see D-grammar). If we
later want the literal `.as::<S>()` surface, add a `DOT KW_AS COLONCOLON LT_TYPE
type_arg_list GT_TYPE LPAREN call_arg_list? RPAREN => {CODE: METHOD_CALL, NAME:"as", …}`
alt and regenerate the parser (low mechanical risk, gated by the oracle).

### D4 — TOM `&mut` is THIN, not fat. **VERIFIED — corrects the recipes.**

`WMap<Wu6,WAny>` is **not** `#[zone_mut]` (`stdlib/lang/writ/wmap.logos:400` comment:
"fixed capacity → set never allocates → thin &mut"). Only string-keyed `WMap<WString,V>`
is fat (`wmap.logos:50`). So `&mut S` for a TOM schema is a plain pointer. **But** writing
a *boxed* value (`make_int` overflow, `box_f64`, `wstring_in_alloc`) needs the allocator:
obtain it from the Writ handle the producer already holds, NOT from a fat ref. For the
TOM-only milestone, schema field writes of Pod-fitting scalars (`bool`, small ints) need
no zone; boxed-value writes are **deferred to Increment 6** and require an explicit zone
argument or `Writ` handle in scope. `ref_repr_of` makes `&mut Struct` fat **only** when
the pointee struct's `zone_mut()` is true AND it's in `all_struct_defs_`
(`mlir_gen_types.cpp:626-636`) — so a schema view stays thin unless we explicitly set
`zone_mut`. Do **not** set it for TOM.

---

## 1. Build / test commands

```
cd /home/victor/devel/logos/build
cmake --build . -j12                       # regenerates logos_parser from logos.peg automatically
bash ../tests/logos/ctest-summary.sh       # full suite (one-shot summary)
# focused single test:
ctest --test-dir . -R <name> --output-on-failure
```

The C++ parser `logos_parser.cpp` is **generated at build time** from
`tools/peg_gen_cpp/grammars/logos.peg` by `peg_gen_cpp` (`src/compiler/CMakeLists.txt:32`).
Grammar edits propagate on the next `cmake --build`. `ast.hpp` node/key codes are
hand-maintained and **must match** the codes declared in `logos.peg`. The byte-identical
AST oracle (`peg_gen_logos_oracle`, opt-in, not in ctest) gates grammar correctness.

Verified anchors (all confirmed on `main`):

| symbol | file:line |
|---|---|
| `LogosType::Kind` enum | sema.hpp:48 (last value `FnItem` :116) |
| `SemaStructInfo` | sema_impl.hpp:2413 (flags: `zone_mut` 2458, `is_union` 2487, `repr_transparent` 2497) |
| `structs_` / `datatypes_` / `enums_` maps | sema_impl.hpp:2842 / 2843 / 2848 |
| `make_struct_type` / `make_enum_type` | sema_impl.hpp:275 / 311 |
| `find_struct_by_name` / `find_enum_by_name` / `lookup_qualified_` | sema_impl.hpp:3153 / 3175 / 3096 |
| `try_resolve_as_known_type` / `is_known_type_name` | sema_collect.cpp:4132 / 4181 |
| pass-0 name scan / type pre-reg | sema_collect.cpp:305 / 414-446 |
| phase-1 dispatch (STRUCT/ENUM) | sema_collect.cpp:1536 / 1670 |
| `collect_struct` / `collect_enum` | sema_collect.cpp (STRUCT @1536, ENUM @1670 dispatch) |
| `lower_struct_def` / `lower_enum_def` | sema_decl.cpp:1154 / 1387 |
| lower dispatch (Stage E) | sema.cpp:7643 (struct) / 7789 (enum) |
| `lower_field_read` | sema_expr.cpp:9189 (struct-field core 9503-9573; **DataRef intercept 9483-9501**) |
| `lower_place_assign` / `try_dataref_field_write` | sema_stmt.cpp:7237 / 7194 (call site 7327-7335) |
| `lower_method_call` (turbofish collect) | sema_expr.cpp:7158 (TYPE_PARAMS 7297-7308) |
| `lower_static_call` (`Type::make::<S>()`) | sema_expr.cpp:13119 (TYPE_PARAMS 13339) |
| `lower_match` / `build_pattern_variant[_data]` / exhaustiveness | sema_stmt.cpp:8217 / 3049,3114 / 7489 |
| `ctfe_eval_const` | sema.cpp:4322 (decl sema_impl.hpp:750) |
| `AttrTarget` enum | sema_impl.hpp:1407 (`Struct,Datatype,Enum,Trait,Fn,Const` — no Schema yet) |
| `LirBuilder` API | lir_builder.hpp: `field_read` 54, `call` 61, `method_call` 114, `var_ref` 34, `lit_int` 29, `addr_of_temp` 88 |
| `lir::LExprPtr` == `lir_view::ExprRef` | lir.hpp:41 (alias; `std::move` is syntactic) |

Runtime surface (all **VERIFIED** present, `use logos.lang.writ.*`):

| call | file:line | signature |
|---|---|---|
| `WMap<Wu6,WAny>::get` | wmap.logos:349 | `(self:&Self,key:u8)->WAny`; absent → null WAny |
| `WMap<Wu6,WAny>::set` | wmap.logos:359 | `(self:&mut Self,key:u8,val:WAny)`; cap-full/oob = no-op |
| `schema_type_code` / `set_schema_type_code` | wmap.logos:340 / 341 | `(&Self)->u64` / `(&mut Self,u64)` |
| `Writ::tinymap` | wmap.logos:383 | `<'a>(self:&'a Writ,cap:i64)->&'a mut WMap<Wu6,WAny>` (thin) |
| `WAny::from(bool/i8/u8/i16/i24/u16/u24/i56)` | anyval.logos:171-181 | inline Pod ctors |
| `WAny::from(&WMap<Wu6,WAny>)` | wmap.logos:392 | `WAny::ref_to(...)` |
| `WAny::as_bool/as_i64/as_u64/as_f64` | anyval.logos:98/239/243/248 | read accessors |
| `WAny::resolve` / `ref_to` / `is_null` / `raw` | anyval.logos:100/85/90/63 | |
| `make_int` / `box_f64` / `box_u64` / `box_i64` | container.logos:143/125/131/120 | `unsafe (*mut Allocator, v)->WAny` |
| `wstring_in_alloc` | wstring.logos:49 | `unsafe (*mut Allocator, str)->*mut WString` |
| `zone_of` / `zone_mut_ref::<T>` (intrinsics) | sema_expr.cpp:3166 / 4853 | recover/build fat zone ref |
| `schema_codes.hpp` masks | schema_codes.hpp:16-18 | `CATEGORY_SHIFT=48`, `CATEGORY_MASK`, `VARIANT_MASK` |

**GAP (VERIFIED):** `schema_codes.hpp` defines masks but **no** `category_of`/`variant_of`
functions, and there is **no Logos-side `schema::variant_of`**. The match desugaring must
inline `code & VARIANT_MASK` (or add a tiny Logos `const fn`). `WTinyValMap` is a type
alias for `WMap<Wu6,WAny>` (wmap.logos:315).

---

## 2. Gated increments

Each increment compiles green and has a test. Order is dependency-respecting.

### Increment 1 — Parse `schema S {…}` + register the type. (smallest end-to-end slice)

**Goal:** `schema Path { kind: i64 = 0, flatten: bool = 6 }` parses and `Path` resolves as
a type (usable in `let p: &Path`, `fn f(p: &Path)`), with a synthetic `m` field. No field
sugar yet.

Files / edits:

1. **Grammar** `tools/peg_gen_cpp/grammars/logos.peg`:
   - `%tokens`: add `KW_SCHEMA = "schema"` near `KW_ENUM` (logos.peg:353). Keyword tokens
     are matched before IDENT; longest-match ordering is automatic.
   - `%nodes`: add `SCHEMA_DEF = 162` and `SCHEMA_FIELD_DEF = 134` (both **VERIFIED free**;
     do not reuse 254/255 — taken by STATIC_DEF/OFFSET_OF). Mirror exact values in ast.hpp.
   - `%fields`: add `STORE_KIND`, `CODE_EXPR` reusing **free key slots 52, 53** (VERIFIED
     free: 27,40,43,52-63). Do **not** reuse BASE/LABEL/SUPERS — those have live meanings on
     other nodes and the recipe's "schema never uses them" assumption is fragile.
   - Rules (mirror `struct_def`/`union_def` at logos.peg:1145/1169):
     ```
     store_clause <- COLON KW_STORE LPAREN IDENT RPAREN  => { NAME: $4 }
     code_clause  <- COLON KW_CODE  LPAREN expr  RPAREN  => { VALUE: $4 }
     schema_field <- KW_PUB? IDENT COLON type_ref (ASSIGN expr)? COMMA?
                     => { CODE: SCHEMA_FIELD_DEF, NAME:…, TYPE:…, VALUE:(key expr)? }
     pub_schema_def <- pub_vis KW_SCHEMA IDENT store_clause? code_clause? LBRACE schema_field* RBRACE
                     => { CODE: SCHEMA_DEF, IS_PUB:1, VIS:$1, NAME:$3, STORE_KIND:$4, CODE_EXPR:$5, FIELDS:$7 }
     schema_def     <- KW_SCHEMA IDENT store_clause? code_clause? LBRACE schema_field* RBRACE
                     => { CODE: SCHEMA_DEF, NAME:$2, STORE_KIND:$3, CODE_EXPR:$4, FIELDS:$6 }
     ```
     Also add `KW_STORE = "store"`, `KW_CODE = "code"` tokens (only inside the clauses;
     they are contextual — verify they don't shadow uses of `store`/`code` as idents, else
     keep them as plain `IDENT` matched in the rule and assert the spelling in sema).
   - `item` rule (logos.peg:529): insert `pub_schema_def / schema_def` **before**
     `pub_struct_def / struct_def`.
   - Reuse `FIELDS` (key 22) for the field array to match struct shape (so `lower_field`
     helpers can be reused).

2. **AST codes** `include/logos/compiler/ast.hpp`: add (matching grammar exactly)
   ```cpp
   inline constexpr Code SCHEMA_DEF       {"SCHEMA_DEF",       162};
   inline constexpr Code SCHEMA_FIELD_DEF {"SCHEMA_FIELD_DEF", 134};
   inline constexpr Key  STORE_KIND       {"STORE_KIND",        52};
   inline constexpr Key  CODE_EXPR        {"CODE_EXPR",         53};
   ```

3. **SemaStructInfo flag** `src/compiler/sema_impl.hpp` (struct @2413): add
   `bool is_schema = false;` and `std::vector<uint8_t> field_keys;` (parallel to `fields`),
   plus `uint64_t schema_type_code = 0;` and `std::string store_kind = "tom";`. (Reusing
   `SemaStructInfo` — schema *is* a struct — avoids a parallel cache + new finders.)

4. **Pass-0** `sema_collect.cpp:305`: add `|| ic == la::SCHEMA_DEF` to the name pre-scan
   condition. In the type pre-registration block (after the ENUM block ~446), add a
   `else if (ic == la::SCHEMA_DEF)` arm that inserts an empty `SemaStructInfo{}` into
   `structs_` under `sema_key(cur_package_, name)` (mirror the STRUCT pre-reg).

5. **Phase-1 collect** `sema_collect.cpp:1536`: add `else if (c == la::SCHEMA_DEF) { …
   check_annotations(AttrTarget::Struct, sname, false, pending_annots); collect_schema(item); }`.

6. **`collect_schema`** (new, in sema_collect.cpp near `collect_struct`): mirror
   `collect_struct` but
   - synthesize one field `{name:"m", type: make_ptr(false, make_generic_struct("WMap",
     {make_datatype_type("Wu6"), make_datatype_type("WAny")}))}` and push to `info.fields`
     so the struct has a real, registered `m` field;
   - parse `SCHEMA_FIELD_DEF` entries into `info.field_keys` (key from `la::VALUE` via
     `ctfe_eval_const`, default = positional index; validate 0..51 for `store_kind=="tom"`,
     uniqueness of names AND keys);
   - parse `code_clause` (`CODE_EXPR`) via `ctfe_eval_const` into `info.schema_type_code`;
   - parse `store_kind` (assert `"tom"` for now; reject others with a clear error);
   - set `info.is_schema = true`, `info.is_pub`, `info.package`, `info.module_id`, `info.doc`.
   The schema's **declared** fields (kind/flatten/…) are stored ONLY in `field_keys`-paired
   metadata, NOT as struct fields — the only real struct field is `m`.

7. **Lowering** `sema.cpp:7643` area: add `else if (c == la::SCHEMA_DEF) { prog.structs.
   push_back(lower_struct_def(item)); }` — but `lower_struct_def` reads the AST `FIELDS`
   directly; instead call it against a node whose only field is `m`, OR (cleaner) build the
   struct draft from `structs_[key]` which already has just `m`. Simplest: make
   `lower_struct_def` source fields from `structs_` (it already looks the struct up by name
   per sema_decl.cpp:1154) — verify it uses `si.fields` not the AST array; if it re-reads
   the AST `FIELDS`, route schema lowering through a thin wrapper that feeds only `m`.

Type resolution comes for free: `try_resolve_as_known_type` already returns
`make_struct_type` for anything in `structs_` (sema_collect.cpp:4167).

**Test (L0):** `tests/logos/.../schema_decl.logos`:
```logos
use logos.lang.writ.wmap;
schema Path { kind: i64 = 0, flatten: bool = 6 }
fn takes(p: &Path) -> i64 { return 0i64; }
fn main() { }
```
`cmake --build . -j12 && ctest -R schema_decl`. Green = parses + resolves.

### Increment 2 — `schema S : code(...) {…}` validation + `WritMap` trait + auto handle.

**Goal:** the `code(...)` clause and per-schema `CODE` const are usable; define the store
seam.

1. **`WritMap` trait + impl** in a new stdlib file `stdlib/lang/writ/writ_map.logos` (or
   append to wmap.logos):
   ```logos
   pub trait WritMap {
       fn get(self: &Self, key: u8) -> WAny;
       fn set(self: &mut Self, key: u8, val: WAny);
       fn schema_type_code(self: &Self) -> u64;
       fn set_schema_type_code(self: &mut Self, code: u64);
   }
   impl WritMap for WMap<Wu6, WAny> { /* forward to the existing inherent methods */ }
   ```
   (ADR §6: desugaring talks to `WritMap`, never TOM directly. `WKey` collapses to `u8`
   for TOM today — keep the trait key `u8` for the milestone, generalize later.)
   Build with `cmake --build` (validates stdlib edits per the cmake-build rule).

2. The schema's `CODE` constant: expose `info.schema_type_code` as an associated const so
   `Path::CODE` resolves (this is what `view`/`make` desugaring reads). For Increment 2 a
   compiler-internal lookup suffices; surfacing `Path::CODE` as a real assoc const can be a
   follow-up if path-const resolution is non-trivial.

**Test (L1):** a schema with `: code(0x0001_0000_0000_0007)` collects without error; trait
+ impl compile. `ctest -R writ_map` + reuse Increment-1 test.

### Increment 3 — Field READ sugar (`p.flatten` → `(&*p.m).get(6u8).as_bool()`).

**Goal:** reading a schema field works end-to-end (the first user-visible feature).

Edit `src/compiler/sema_expr.cpp`, `lower_field_read` — insert a schema branch **right
before** the DataRef intercept at **line 9483** (same shape as DataRef, which is THE
precedent for view-over-pointer field access):

```cpp
// SCHEMA field read: p.field  ⇒  (deref p to &m).get(key).as_T()
if (recv_base_t && TypeRef(recv_base_t).kind() == LogosType::Kind::Struct) {
    auto [spkg, ssi] = find_struct_by_name(struct_name_from_type(recv_base_t));
    if (ssi && ssi->is_schema) {
        // find declared field (name → key, type) in ssi metadata
        // 1. recv.m  (real struct field, type *const WMap<Wu6,WAny>)
        auto m = builder().field_read(std::move(recv), "m", /*ptr type*/);
        // 2. (&*m).get(key)  — emit a method_call routed via lower_method_call (path (a))
        //    or directly with resolved_symbol (path (b), like tuple-eq 4536):
        //    auto recv_ref = builder().addr_of_temp(deref(m), false, make_ref(false, wmap_t));
        //    auto any = builder().method_call(recv_ref, "get", "", {}, {lit_int(key,u8)}, -1, wany_t);
        // 3. any.as_T()  per field type → as_bool / as_i64 / resolve+cast
        return /* converted ExprRef */;
    }
}
```

Prefer **path (a)**: synthesize the AST shape `recv.m.get(key).as_T()` and re-enter
`lower_field_read`/`lower_method_call`, so receiver auto-ref, mangling, and mono are
automatic (verified: mlir-gen lowers it with no schema knowledge — D2). The conversion
method is chosen by the field's declared type: `bool→as_bool`, integer→`as_i64`(+narrow),
`&WString`/ref→`resolve()` + cast (mirror `WView2::tiny_map_get` + `get_int`,
static_view.logos:78/87). Absent key → null WAny → as_T returns 0/false/null (verified
get returns null WAny on miss).

**Test (L2):** construct a TOM by hand (`wr.tinymap`, `set_schema_type_code`, `set`), wrap
as `&Path` via an `unsafe` cast, read `p.kind`/`p.flatten`, assert values. Reuse the
existing writ test harness. `ctest -R schema_read`.

### Increment 4 — Field WRITE sugar (`p.flatten = y` → `p.m.set(6u8, WAny::from(y))`).

Edit `src/compiler/sema_stmt.cpp`, `lower_place_assign`. At the DataRef call site
(**7327-7335**) add a sibling `try_schema_field_write(rn, field, val_node)` — a near-
mechanical clone of `try_dataref_field_write` (7194):

```cpp
std::optional<lir_view::StmtRef>
SemaChecker::try_schema_field_write(const std::string& recv, const std::string& field,
                                    writ::TinyMapView val_node) {
    TypeRef rt = lookup(recv);            // resolve through &mut
    auto [pkg, ssi] = find_schema_struct(rt);    // is_schema struct (or &mut/ptr to one)
    if (!ssi || !ssi->is_schema) return std::nullopt;
    // find field → (key, type); require &mut + writable (mirror DataRef mutability gate 7205-7208)
    lir::LExprPtr val = lower_expr(val_node);
    // val → WAny:  Pod-fitting scalar ⇒ WAny::from(val);  (boxed types deferred, see D4/Inc6)
    // emit: recv.m.set(key, wany)  — method_call routed via lower path (a)
    // wrap in ExprStmt via make_stmt_emit
}
```

For the milestone, **only Pod-fitting field types** (`bool`, `i8..i24/u8..u24`, small ints
via `WAny::from`) are writable; boxed types error with "schema write of boxed field requires
zone (Increment 6)". Per D4, TOM `&mut` is thin, so no `zone_of` needed for Pod writes.

**Test (L2):** write then read back `p.kind`/`p.flatten`; assert round-trip and that an
absent-then-set key becomes present. `ctest -R schema_write`.

### Increment 5 — Construction (`Writ::make::<Path>()`) + bind (`x.view::<Path>()`).

**Goal:** producers stamp the code; consumers bind from a `WAny`/`WRef` with one check.

- `Writ::make::<S>()` parses as `STATIC_CALL` with `TYPE_PARAMS` (verified, logos.peg:3224;
  `lower_static_call` consumes type args at sema_expr.cpp:13339). Special-case callee
  `make` with one schema type-arg: desugar to
  `{ let p = self.tinymap(CAP); p.set_schema_type_code(S::CODE); Path{ m: p as *const _ } }`.
  (`CAP` = number of declared fields, or a fixed small default.)
- Bind `x.view::<S>()` / `x.view_checked::<S>()` — name `view`, **not** `as` (D3). A
  `view_checked` returns `Option<S>`: `if (&*p).schema_type_code() != S::CODE { None } else
  { Some(S{m:p}) }`. `view` is the trusted (no-check) form for already-bound trees.
  `child::<S>()` = trusted descent. These are method-call lowerings keyed on the method
  name + schema type-arg in `lower_method_call`.

**Test (L2):** `let p = wr.make::<Path>(); p.kind = 7; assert(p.kind == 7);` plus a
`WAny`→`view_checked::<Path>()` round-trip with a wrong-code negative case. `ctest -R schema_make`.

### Increment 6 — Boxed-value writes (`make_int` overflow, `box_f64`, string fields).

Lift the Increment-4 Pod-only restriction. Needs an allocator: thread it from the `Writ`
handle in scope, or require `&mut S` to be fat for string-backed schemas later. For TOM:
`p.name = s ⇒ p.m.set(7u8, WAny::ref_to(wstring_in_alloc(zone, s) as *const u8))` where
`zone` is the producer's allocator. Keep it explicit (a `zone:` param or a `with_zone`
form) until the fat-ref story for schemas is decided. **Test:** write/read an i64 that
exceeds i56 (forces `box_i64`) and an f64; assert round-trip.

### Increment 7 — `schema enum E { V(S), … }` + `match`.

**Goal:** closed union discriminated by the pointee's `schema_type_code`.

- **Grammar/AST:** add `SCHEMA_ENUM_DEF` (free node code, e.g. 241) + `category_clause`
  (reuse a free key for `CATEGORY_EXPR`). `schema_variant <- IDENT LPAREN type_ref RPAREN`
  reuses `VARIANT_DEF`. Register `E` as a struct-shaped type (single `m` field) OR a
  dedicated `is_schema_enum` flag on `SemaEnumInfo`/`SemaStructInfo`.
- **collect_schema_enum:** variants carry a *schema type* (the concrete child schema), not
  a payload; `category` packs the top 16 bits (validate all variants share it).
- **Match:** hook the existing pattern path, not a new top-level branch (MEDIUM risk —
  spread across 3 sites). In `build_pattern_variant`/`build_pattern_variant_data`
  (sema_stmt.cpp:3049/3114), when `scrut_type` is a schema-enum, lower the arm guard to
  `(code & VARIANT_MASK) == V::variant` reading `(&*scrut.m).schema_type_code()` once, and
  bind the arm variable to `V{ m: scrut.m }` (trusted, no re-check). Add a parallel
  schema-enum branch to `check_match_exhaustiveness` (sema_stmt.cpp:7500). Inline the
  `& VARIANT_MASK` (no `variant_of` exists — GAP).

**Test (L2):** build two TOMs with distinct codes, `match` an erased `WAny` over the schema
enum, assert each arm fires and binds correctly; assert a `_`/non-exhaustive case. `ctest
-R schema_enum`.

### Increment 8 — Full-suite gate + docs.

Run the **full** ctest (`bash ../tests/logos/ctest-summary.sh`) — compiler+stdlib changes
require it (per the full-ctest rule). Run the `peg_gen_logos_oracle` target to confirm the
grammar additions keep byte-identical ASTs. Update DIVERGENCES.md if `schema` is a Logos
addition (it is — note it as a blessed Writ-platform extension).

---

## 3. Deep-subsystem VERIFIED / RISK sections

### SEMA — type representation & registration

**VERIFIED:** `Kind` is positional/serialized (no `=15`); append-only after `FnItem`.
Schemas register in `structs_` (sema_impl.hpp:2842) as `Kind::Struct` via existing
`make_struct_type` (275) + `try_resolve_as_known_type` (sema_collect.cpp:4167). Per-struct
bool flags (`zone_mut` 2458, `repr_transparent` 2497, etc.) are the proven 4-touch pattern
to add `is_schema`. A single `*const`-field struct is an 8-byte by-value pointer-shaped
aggregate; `#[repr(transparent)]` (enforced sema_collect.cpp:1594) gives pointer-exact
layout if needed for niche `Option<View>`.
**RISK/UNKNOWN:** (1) Struct vs ZonedStruct — schemas register in `structs_`/`make_struct_type`
here; if a Writ/zoned context needs `datatypes_`/`ZonedStruct`, switch maps + finder. Decide
by whether the view participates in `#[zoned]` layout (it should NOT — it's a host-side
handle). (2) `lower_struct_def` (sema_decl.cpp:1154) must source fields from `structs_`
(only `m`), not re-read the AST `FIELDS` array — verify and wrap if it reads the AST.

### SEMA — field access / assign / turbofish / match

**VERIFIED:** `lower_field_read` @9189, struct core @9503-9573, **DataRef intercept @9483**
(the exact view-over-pointer precedent — emits `.ptr()` method_call then `field_read`).
`lower_place_assign` @7237; `try_dataref_field_write` @7194 called @7327-7335 (the write
precedent + insertion point). Builder: `field_read` (lir_builder.hpp:54), `method_call`
(114), `call` (61), `lit_int` (29), `var_ref` (34), `addr_of_temp` (88); `lir::LExprPtr`
== `lir_view::ExprRef` (lir.hpp:41). Method turbofish collected @7297-7308; static turbofish
@13339. Match bindings via `push_scope()`+`bind_pattern(pat,scrut_type)` (sema_stmt.cpp:
8654-8655); enum dispatch in `build_pattern_variant[_data]` (3049/3114) + exhaustiveness
(7489/7500).
**RISK/UNKNOWN:** (1) **`as` is reserved (KW_AS, logos.peg:368)** — `x.as::<S>()` will not
parse; use `.view::<S>()`/`.view_checked::<S>()`, or add a `DOT KW_AS …` grammar carve-out
(new/null precedent at logos.peg:2674-2689). This is the single hardest surface decision.
(2) Match is the most spread-out change (3 sites) — MEDIUM risk; hook the pattern path,
don't reimplement arm/guard machinery.

### MLIR-GEN — desugar mechanism

**VERIFIED:** ZERO new mlir-gen code. `gen_expr_kind(EMethodCallView)` (mlir_gen_expr.cpp:
2544) reads `resolved_symbol` (2548/2661), `find_func_op` (2666), emits `func::CallOp`
(2800) — schema-agnostic. SEMA fully resolves callees (sema_expr.cpp:9178-9186 sets
`mc.resolved_symbol`); mono auto-instantiates (mono_scan.cpp:202). Precedents: operator
overload (1947-2005), tuple-eq synthetic-receiver-chain (4509-4542), index (10520),
str_eq (2229).
**RISK/UNKNOWN:** when emitting LIR directly (path b), you must (a) substitute the schema
view's type args into the `m` field type before `field_read` (else mlir-gen's `gep_field`
sees symbolic `T`), (b) auto-ref the receiver with `addr_of_temp(...,make_ref(false,wmap_t))`
since `WMap::get` takes `&self`. Path (a) (re-enter `lower_method_call` with a synthetic AST)
avoids both — strongly preferred.

### RUNTIME — stdlib surface

**VERIFIED:** every call the desugaring needs exists with the signatures in §1 (get/set/
schema_type_code/set_schema_type_code @wmap.logos:340-378; tinymap @383; WAny from/as_*/
resolve/ref_to @anyval.logos; make_int/box_f64/box_u64 @container.logos; wstring_in_alloc
@wstring.logos:49; zone_of/zone_mut_ref intrinsics). TOM `&mut` is THIN (wmap.logos:400).
TOM `set` is a no-op on cap-full/oob (no realloc) — producer must size `tinymap(cap)`.
**RISK/UNKNOWN:** (1) **No `variant_of`/`category_of` functions** in schema_codes.hpp (only
masks) and no Logos `schema::variant_of` — inline `code & VARIANT_MASK` in match, or add a
const fn. (2) `WritMap` trait does not exist yet — Increment 2 creates it + the single TOM
impl (ADR §6 invariant: TOM hardcoded nowhere but that one impl). (3) `Path::CODE` as a
real associated const may need path-const-resolution work; compiler-internal lookup is the
fallback for the milestone.

---

## 4. Top risks (carry into implementation)

1. **`as` keyword collision (D3).** Blocking for the literal `node.as::<S>()` surface.
   Mitigated by naming the bind method `view`/`view_checked`/`child`; a grammar carve-out
   is the alternative if `.as::<S>()` is required. Gated by the AST oracle.
2. **Match is the most spread-out change** (build_pattern_variant + variant_data +
   exhaustiveness, all keyed on schema-enum). Deferred to the last increment; reuse the
   pattern path rather than a bespoke top-level branch.
3. **`lower_struct_def` field source + the schema-as-struct boundary.** Must lower only the
   synthetic `m` field (not the declared sugar fields). Verify whether it reads `structs_`
   or the AST `FIELDS`; wrap if the latter. Getting this wrong yields a struct with the
   wrong layout or duplicate fields.
