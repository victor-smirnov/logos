# Bug catalog: Items

**Group**: 2 — Items: structs / enums / datatypes / traits
**Grammar rules covered**: `item`, `struct_def`/`pub_struct_def`, `enum_def`/`pub_enum_def`, `variant_list`, `variant_def`, `variant_payload_list`, `datatype_def`/`pub_datatype_def`, `trait_def`/`pub_trait_def`, `genos_def`/`pub_genos_def`, `super_list`, `trait_method`, `trait_kw`, `impl_block`, `impl_item`, `struct_inst`/`pub_struct_inst`, `datatype_inst`/`pub_datatype_inst`, `trait_inst`/`pub_trait_inst`, `field_def`, `method_def`, `meta_block`
**Reference doc**: [docs/language/reference/items.md](../language/reference/items.md)
**Implementation entry points**:
- [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) — `lower_struct_def`, `lower_enum_def`, `lower_datatype_def`, `lower_trait_def`, `lower_impl_block`, `eval_static_hermes_lit`
- [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) — first-pass registration, dup-detection
- [src/compiler/mlir_gen_types.cpp](../../src/compiler/mlir_gen_types.cpp) — `register_struct`, `register_tagged_enum`

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/items/`

## Bugs

### B-it-01: Recursive by-value struct → SEGFAULT

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-Sprint1 (sema cycle-check via check_recursive_value_types; tests/logos/fail/recursive_struct_by_value)
**Repro**: `B03/` —
```logos
struct Node { x: i32, child: Node, }
fn main() -> i32 { return 0; }
```
**Observed**: `[1] 648491 segmentation fault (core dumped) logosc ...`. Exit 139.
**Expected**: Sema diagnostic: "infinite-size type 'Node' (cannot contain itself by value); use a pointer or `Box<Self>`".
**Suspected root**: Field-type resolution recurses into struct definition without a depth/cycle guard. Likely [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) `lower_struct_def` field loop or [src/compiler/mlir_gen_types.cpp](../../src/compiler/mlir_gen_types.cpp) `register_struct` recursion (which I read earlier — line 178 calls `register_struct(*def_it->second)` with no visited-set).
**Tags**: `oversight:simple`, `tech-debt:missing-cycle-guard`

### B-it-02: Recursive by-value enum compiles silently (no instantiation forces codegen yet)

**Severity**: P0.5 (latent — will crash on use)
**Status**: fixed-in-Sprint1 (same check_recursive_value_types pass; tests/logos/fail/recursive_enum_by_value)
**Repro**: `B15/` —
```logos
enum List { Nil, Cons(i32, List), }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly (exit 0) because `main` doesn't materialise a `List`. Any actual use would cause infinite-recursion in tagged-enum payload-size computation.
**Expected**: Same as B-it-01 — sema rejects the recursive payload type with a clear error.
**Suspected root**: Same family as B-it-01; payload-type resolution in `register_tagged_enum` lacks cycle guard.
**Tags**: `oversight:simple`, `tech-debt:missing-cycle-guard`

### B-it-03: Duplicate field name in struct silently accepted

**Severity**: P0 (silent miscompile potential)
**Status**: fixed-in-M0.1 (check_unique_names; tests/logos/fail/dup_struct_field)
**Repro**: `B07/` —
```logos
struct Foo { pub x: i32, pub x: i64, }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly. Binary runs to exit 0. Whatever `Foo` field layout is used is implementation-defined.
**Expected**: Sema rejects with "duplicate field 'x' in struct 'Foo'".
**Suspected root**: `lower_struct_def` field-iteration doesn't dedup-check field names. Field map is likely a `Vec` not a `Map`, so collisions aren't surfaced.
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-it-04: Duplicate enum variant silently accepted

**Severity**: P0 (silent miscompile potential)
**Status**: fixed-in-M0.1 (check_unique_names; tests/logos/fail/dup_enum_variant)
**Repro**: `B08/` —
```logos
enum E { A, A, }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly.
**Expected**: "duplicate variant 'A' in enum 'E'".
**Suspected root**: Same family as B-it-03 — variant list doesn't dedup-check.
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-it-05: Duplicate trait definition silently accepted

**Severity**: P0 (silent shadowing)
**Status**: fixed-in-M0.1 (collect_trait dup check; tests/logos/fail/dup_trait_def)
**Repro**: `B17/` —
```logos
trait Greet { fn hello(self: *const Self) -> i32 { return 1; } }
trait Greet { fn bye(self: *const Self) -> i32 { return 2; } }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly. Whichever was registered last presumably wins.
**Expected**: "duplicate trait 'Greet'" — same as the duplicate-struct/enum messages already emitted for those.
**Suspected root**: Trait registration uses different code path than struct/enum registration; the dup-check was missed when traits were added.
**Tags**: `oversight:simple`, `design:incomplete`

### B-it-06: Empty enum body compiles (no diagnostic)

**Severity**: P2 design
**Status**: not-a-bug (intentional; stdlib uses empty enums as marker / never types — see meta_variant_intrinsics)
**Repro**: `B05/` —
```logos
enum Empty { }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly.
**Expected**: Either accept and treat as uninhabited (`!`-like, like Rust's empty enum) — but then document — OR reject with "enum must have at least one variant".
**Suspected root**: `variant_list` is an optional repetition (zero-or-more), and there's no post-hoc check enforcing non-empty.
**Tags**: `design:incomplete`

### B-it-07: `struct Empty;` parses but is unusable

**Severity**: P2 design + P1 diagnostic
**Status**: fixed (sema struct_inst path emits "did you mean 'struct Empty { ... }'" when type expression resolves to error_t)
**Repro**: `B02/` —
```logos
struct Empty;
fn main() -> i32 { let _e = Empty {}; return 0; }
```
**Observed**: `error [fn main]: unknown type 'Empty'` — the bare `struct Empty;` parsed as an `INSTANTIATE_DECL` (forward-instantiation hint, à la C++ explicit instantiation), but no actual `struct Empty { ... }` definition exists, so the type doesn't enter the type pool.
**Expected**: Either reject the syntax outright with "explicit instantiation requires a generic struct definition", or accept `struct Empty;` as a unit struct (Rust-style) and register it as zero-field.
**Suspected root**: `struct_inst` grammar is too permissive; sema doesn't distinguish "non-generic name with no body" from "instantiation of generic with type-args".
**Tags**: `design:incomplete`, `tech-debt:overloaded-syntax`

### B-it-08: Forward `pub struct Foo<T>;` then `pub struct Foo<T> { ... }` confuses sema

**Severity**: P2 design
**Status**: fixed — chose the "reject explicitly" branch of the OR. Sema's STRUCT-without-NAME path now pre-checks the type expression: if it's `GENERIC_INST` whose args contain a `TYPE_REF` whose NAME is neither a known type nor a type-param in scope, emit a specific "explicit instantiation requires concrete type arguments; write the body directly" diagnostic instead of the misleading "unknown type 'T'". Forward-declaration as a feature stays out of scope. Lock-in: fail test `struct_inst_unbound_typevar`.
**Repro**: `B14/` —
```logos
pub struct Foo<T>;
pub struct Foo<T> { pub x: T }
fn main() -> i32 { let f: Foo<i32> = Foo::<i32> { x: 5 }; return f.x; }
```
**Observed**: `error [fn main]: unknown type 'T'` — the `pub struct Foo<T>;` instantiation hint parses with T as a type-var that's unbound at item scope; or the dup-detection rejects redefinition.
**Expected**: Either treat `struct Foo<T>;` as a forward declaration (legal pair with the body that follows), or reject the syntax explicitly.
**Suspected root**: `struct_inst` accepts ANY `type_ref` after `struct`, but `Foo<T>` at item-scope has no type-param scope to bind T against. Same family as B-it-07.
**Tags**: `design:incomplete`, `tech-debt:overloaded-syntax`

### B-it-09: `impl Trait for i32` parses + sema-OKs but method dispatch fails

**Severity**: P2 design
**Status**: fixed — primitive method dispatch in `lower_method_call` already tried `&T` and `&mut T` receiver auto-ref; extended to also try `*const T` and `*mut T` so `impl Trait for i32` declared with `self: *const Self` is reachable from dot-call. Symmetric auto-addr-of-temp at the call site lifts the value receiver to `*const T`/`*mut T` when the method expects it. Lock-in: pass test `impl_trait_for_primitive` covers both `&self` and `self: *const Self` styles.
**Repro**: `B13/` —
```logos
trait Doubled { fn doubled(self: *const Self) -> i32; }
impl Doubled for i32 { fn doubled(self: *const i32) -> i32 { unsafe { return (*self) * 2; } } }
fn main() -> i32 { let x: i32 = 21; return x.doubled(); }
```
**Observed**: `error [fn main]: method call: receiver is not a struct (got i32)`.
**Expected**: Either disallow `impl Trait for <primitive>` at the impl-block parse level (with clear diagnostic), OR support method dispatch on primitive receivers via the trait impl. The current state — accepting the impl but failing dispatch — is the worst of both worlds.
**Suspected root**: `gen_method_call` path in [src/compiler/mlir_gen_expr.cpp](../../src/compiler/mlir_gen_expr.cpp) uses `gen_recv_struct` which restricts receivers to Struct/ZonedStruct kinds. Primitive receivers aren't routed through the trait-impl symbol table.
**Tags**: `design:incomplete`, `tech-debt:hardcoded-special-case`

### B-it-10: Meta block with capture (`${...}`) silently accepted (should be rejected)

**Severity**: P1 diagnostic
**Status**: fixed (eval_static_hermes_lit emits diagnostic for HERMES_CAP_*; tests/logos/fail/meta_block_with_capture)
**Repro**: `B23/` —
```logos
struct Foo { x: i32, meta @{ "schema": ${some_var}, } }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly.
**Expected**: `eval_static_hermes_lit` should reject capture nodes per the documented contract ("no captures `${...}`, just static values" — see [attributes.md](../language/reference/attributes.md#meta-blocks-meta)). At least error: "meta block requires static values; ${...} captures not allowed".
**Suspected root**: [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) `eval_static_hermes_lit` has the capture-rejection branch (lines 108-111: `code == HERMES_CAP_IDENT.code || HERMES_CAP_EXPR.code → return nullptr`), BUT returning nullptr bubbles up as silent failure rather than a sema error. The caller `extract_meta_val` returns `nullptr` and just leaves `meta_val` empty.
**Tags**: `oversight:simple`, `tech-debt:silent-nullptr-on-error`

### B-it-11: Extern fn without varargs accepted despite grammar requiring `, ...`

**Severity**: P2 design (grammar/parser mismatch)
**Status**: not-a-bug (verified 2026-05-04) — extern_fn_def has 3 alts at logos.peg:841-846 covering varargs, with-return-type, and without-return-type forms. Catalog read only the first alt.
**Repro**: `B24/` —
```logos
extern fn strlen(s: *const u8) -> i64;
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly.
**Expected**: Per grammar ([logos.peg:835](../../tools/peg_gen/grammars/logos.peg#L835)): `extern_fn_def <- KW_EXTERN KW_FN IDENT LPAREN param_list COMMA DOTDOTDOT RPAREN ARROW type_ref SEMI` — varargs marker is required. Either the grammar is wrong (should have an alternate without `...`) or the parser/sema accepts a form the grammar doesn't.
**Suspected root**: There's likely an alternate `extern_fn_def` production not visible in the line I read, OR the parser is lenient. Worth grep'ing for all `extern_fn_def` alternates.
**Tags**: `design:incomplete`, `tech-debt:grammar-doc-drift`

## Tag summary

| Tag | Open | Fixed | N/A | Total | Bugs |
|---|---|---|---|---|---|
| `design:incomplete` | 0 | 4 | 2 | 6 | B-it-05, B-it-06, B-it-07, B-it-08, B-it-09, B-it-11 |
| `oversight:simple` | 0 | 6 | 0 | 6 | B-it-01, B-it-02, B-it-03, B-it-04, B-it-05, B-it-10 |
| `tech-debt:missing-cycle-guard` | 0 | 2 | 0 | 2 | B-it-01, B-it-02 |
| `tech-debt:missing-uniqueness-check` | 0 | 2 | 0 | 2 | B-it-03, B-it-04 |
| `tech-debt:overloaded-syntax` | 0 | 2 | 0 | 2 | B-it-07, B-it-08 |
| `tech-debt:grammar-doc-drift` | 0 | 0 | 1 | 1 | B-it-11 |
| `tech-debt:hardcoded-special-case` | 0 | 1 | 0 | 1 | B-it-09 |
| `tech-debt:silent-nullptr-on-error` | 0 | 1 | 0 | 1 | B-it-10 |

**Cluster preview**:
- **Missing-cycle-guard** (B-it-01/02) — recursive type-defs blow up at type-resolution. One architectural fix: visited-set in `register_struct`/`register_tagged_enum` recursion + sema check.
- **Missing-uniqueness-check** (B-it-03/04/05) — fields, variants, traits all lack dup-name validation; struct/enum DEFs themselves correctly reject dups but their bodies don't. Single architectural fix: one helper, three call sites.
- **Overloaded-syntax** (B-it-07/08) — `struct Foo;` and `struct Foo<T>;` are ambiguous between forward-decl and explicit-instantiation. Either grammar tightens (introduce explicit `instantiate Foo<i32>;` keyword — already exists per grammar line 429!), or the no-body forms become hard errors at item position.

## Regression-confirmed (NOT bugs)

- **I01**: empty struct body `Empty {}` works.
- **I04**: recursive struct via `*mut Self` works.
- **I06**: single-variant enum works.
- **I12**: trait default-method works.
- **I16**: recursive enum via `*mut` works.
- **I18**: duplicate `struct Foo` def correctly rejected (proves the dup-check exists for struct, not for variants/fields/traits).
- **I19**: extern fn parses (alongside the variadic-form gap noted in B-it-11).
- **I20**: empty type-param list `<>` correctly rejected.
- **I21**: `Self` in inherent impl works.
- **I22**: duplicate `enum E` def correctly rejected.
- **I25**: missing super-trait correctly diagnosed.

## Notes for Phase 3

- **Cycle-guard cluster** is likely shared with type-system (Group 6) — the same recursion happens through `*Foo`, `Box<Foo>`, `Vec<Foo>`. Worth confirming via Group 6 tests.
- **Uniqueness-check cluster** suggests an architectural pattern: every "list of named items" needs a dup-detector. Pattern: `for item in list: if names.insert(item.name).second { ... } else { error("duplicate " + item.kind + " '" + item.name + "'") }`. Should grep for places that build name-keyed maps from item lists.
- **Empty-allowed forms** (B-it-06 empty enum) should be a positive design choice (uninhabited-type), not a bug per se. Decision needed: do we adopt `enum Never { }` as `!` analog, or reject?
