# Items and Modules

Specification of item-level constructs (functions, statics, constants, structs, unions, enums, Writ datatypes, type aliases, impls, extern blocks, use declarations) and the module / separate-compilation model (packages, manifests, loading, imports, prelude, visibility, symbol mangling, export catalogs). Rules are auto-assembled from the `spec-extract` artifacts spanning the grammar, sema (collect / decl / impl / module-loader / manifest), mono, and codegen layers. Each rule id is a permanent linkable address and is never renamed.

## Item kinds

### `item.kinds.set` — Module item alternatives

A module item is one of: doc-comment, annotation, template decl, const/static def, type alias, enum def, datatype def/inst, trait def/inst, struct unit/def/inst, instantiate decl, item-position metacall, fn-macro item invocation, union def, impl block, extern block, extern fn, or fn def — each in plain and `pub` forms where visibility applies.

**Source:** `tools/peg_gen/grammars/logos.peg#L529`

## Visibility

### `item.visibility.pub-module` — Visibility marker pub / pub(module)

Item visibility is `pub` (fully exported) or `pub(IDENT)` where IDENT is a contextual keyword validated == "module" in sema, meaning module-linkage: visible to other packages of the SAME module but not exported to consumers.

**Examples:**

```logos
pub(module) fn helper() {}
```

**Divergence:** Logos uses `pub(module)` for module-linkage; Rust uses `pub(crate)`/path-restricted visibilities.

**Source:** `tools/peg_gen/grammars/logos.peg#L1273-L1284`

## Attributes

### `item.attr.datatype-promotion` — #[datatype]/#[annotation] promote a struct into the datatype pipeline

A struct-syntax item annotated `#[datatype]` or `#[annotation]` is treated as a datatype declaration; `#[zoned]` marks self-relative fields and does NOT promote a struct to a datatype.

**Divergence:** Logos addition: datatype/annotation/zoned attributes (no Rust equivalent).

**Source:** `src/compiler/sema_collect.cpp#L366-L431`

### `item.attr.struct-enum-flag-set` — Struct/enum attribute flag vocabulary

The recognised struct/enum modifier attributes are exactly: `datatype`, `annotation`, `zoned`, `zone_mut`, `rel_ptr`, `self_describing`, `pinned`, `borrow_carrying`, `no_auto_drop`, `non_null`. A struct bearing `#[datatype]` or `#[annotation]` is promoted to the datatype pipeline.

**Divergence:** Logos-specific memory/zone attribute set; no Rust analogue.

**Source:** `src/compiler/sema_impl.hpp#L1430-L1460`

### `item.attr.target-kind-validity` — Built-in attributes restricted to declared item kinds

Each compiler-recognised attribute is valid only on a fixed set of item kinds: `type_code`→{struct,datatype,enum,trait}; `zoned`→{struct,enum}; `datatype`→{struct}; `self_describing`/`rel_ptr`/`pinned`/`zone_mut`/`no_auto_drop`/`non_null`→{struct}; `borrow_carrying`→{struct,enum}; `annotation`→{struct,datatype}; `tag_dispatch`→{trait}; `metaprog_handler`/`no_mangle`/`fn_macro`/`token_macro`/`test`/`should_panic`/`ignore`→{fn}; `cfg`/`cfg_attr`→{all item kinds}; `repr`→{struct,enum}. Applying a built-in attribute to a disallowed kind is an error; an unrecognised name is treated as a user `#[annotation]` lookup.

**Examples:**

```logos
#[zoned] enum E {}  // ok
```
```logos
#[datatype] enum E {} // error (struct only)
```

**Source:** `src/compiler/sema_impl.hpp#L1462-L1507`

### `item.attr.unknown-warn` — unknown attribute is warned

A top-level user `#[name]` attribute that is neither a builtin attribute, a registered metaprog-handler trigger, nor the name of an `#[annotation]` datatype is a warning (likely typo, missing import, or removed handler).

**Source:** `src/compiler/sema_collect.cpp#L607-L665`

## Annotations

### `item.annotation.arg-forms` — Attribute argument forms

Within an attribute argument list, an argument is one of: `IDENT(args)` (nested call), `IDENT = lit` (key-value), a bare literal (positional), or a bare IDENT (legacy). A literal may be an enum ref `IDENT::IDENT`, raw/normal string, float, integer, `true`/`false`, or a bracketed array of literals. Lists allow a trailing comma.

**Examples:**

```logos
#[cfg(target = "x86")]
```
```logos
#[align(8)]
```
```logos
#[list([1, 2, 3])]
```

**Source:** `tools/peg_gen/grammars/logos.peg#L648-L672`

### `item.annotation.attribute-forms` — annotation/attribute syntax

An annotation is `#[NAME]`, `#[NAME = literal]`, or `#[NAME(args...)]`; arguments may be positional or `key = value`, and an argument value may be an array literal `[ ... ]`.

**Source:** `src/compiler/sema_render.cpp#L1400-L1460`

### `item.annotation.forms` — Outer attribute forms

An attribute is `#[ NAME (args) ]`, `#[ NAME = val ]`, or `#[ NAME ]`. The `= val` form admits an enum literal `IDENT::IDENT` or an integer.

**Examples:**

```logos
#[derive(Debug)]
```
```logos
#[repr = 8]
```
```logos
#[inline]
```

**Source:** `tools/peg_gen/grammars/logos.peg#L619-L641`

### `item.annotation.inner-attribute` — Inner attribute attaches to enclosing module

An inner attribute `#![ ... ]` (with the same `(args)` / `= val` / flag payload shapes as an outer attribute) attaches to the enclosing module rather than the following item. (Currently only `#![no_implicit_prelude]`.)

**Examples:**

```logos
#![no_implicit_prelude]
```

**Source:** `tools/peg_gen/grammars/logos.peg#L631-L636`

## Doc comments

### `item.doc.comment-attached-to-next-item` — Doc comments attach as documentation

Outer doc comments (`///`, `/** */`) accumulate and attach to the next item; inner doc comments (`//!`, `/*! */`) accumulate into the module-level inner documentation. The comment markers are stripped.

**Source:** `tools/peg_gen/grammars/logos.peg#L305-L308`

### `item.doc.comment-strip` — doc comment accumulation and prefix stripping

`///` line docs strip the leading `///` plus one optional space; `/** */` outer block docs and `/*! */`/`//!` inner docs accumulate; outer docs attach to the next non-doc item, inner docs accumulate into a per-module inner-doc buffer joined by newlines and never attach to a specific item.

**Source:** `src/compiler/sema_collect.cpp#L1404-L1436`

### `item.doc.inner-module` — Inner doc-comment is module summary

> **Conflict flag:** id extracted with differing titles: "Inner doc-comment is module summary"; "Inner doc-comments (//! and /*!) form module-level doc".

> **Conflict flag:** multiple source layers describe this id with differing statements; both are preserved verbatim and must be reconciled before treating either as canonical.

1. An inner doc-comment (`//!` line or `/*! */` block) accumulates into the enclosing module's doc summary and is never attached to a specific item.
2. `//!` lines (leading '//! ' stripped) and `/*! ... */` inner block comments accumulate into a module-level inner-doc buffer, committed as the module's inner documentation after all items.

**Source:** `tools/peg_gen/grammars/logos.peg#L541-L554`, `src/compiler/sema.cpp#L7412-L7428`, `src/compiler/sema.cpp#L8022-L8030`

### `item.doc.outer-block` — Outer block doc-comment

An outer block doc-comment `/** ... */` is an item/member-stream element with the same next-item binding role as line doc-comments; the `/**` envelope and per-line leading `*` are stripped and lines joined with newline.

**Source:** `tools/peg_gen/grammars/logos.peg#L544-L549`

### `item.doc.outer-line` — Outer line doc-comment binds to next item

An outer line doc-comment (`///`, captured as DOC_LINE) is an item-stream element; consecutive outer doc-comments accumulate and attach to the next real item.

**Source:** `tools/peg_gen/grammars/logos.peg#L536-L537`

### `item.doc.outer-line-block` — Outer doc-comments (/// and /**) attach to next item

`///` line doc-comments (with leading '/// ' stripped, joined by newline) and `/** ... */` outer block doc-comments accumulate into the pending doc buffer and become the DOC of the next item.

**Source:** `src/compiler/sema.cpp#L7399-L7411`

## Conditional compilation (`cfg`)

### `item.cfg.conditional-compilation` — cfg attributes gate item lowering

An item whose pending cfg attributes evaluate false (cfg_attrs_drop_item) is dropped before lowering; its pending annotations and doc are consumed and discarded.

**Examples:**

```logos
#[cfg(unix)] fn f() {}
```

**Source:** `src/compiler/sema.cpp#L7435-L7439`

### `item.cfg.drop-disabled` — cfg-disabled items are dropped

Before collecting an item, `cfg_attr` activations are applied and `cfg(...)` predicates evaluated against pending annotations; if any predicate is false the item is dropped entirely (neither collected nor lowered) together with its pending annotations.

**Source:** `src/compiler/sema_collect.cpp#L1437-L1446`

### `item.cfg.gate-before-registration` — cfg-false items do not register their name

A `#[cfg(...)]` predicate is evaluated before name registration; an item whose cfg is false registers no name. This permits the same-name-under-mutually-exclusive-cfg idiom (e.g. cfg(unix)/cfg(windows) structs) without a duplicate-name error.

**Source:** `src/compiler/sema_collect.cpp#L358-L365`

## Functions

### `item.fn.all-paths-return` — Non-void fn must return on every path

> **Conflict flag:** id extracted with differing titles: "Non-void fn must return on every path"; "All paths must return a value".

> **Conflict flag:** multiple source layers describe this id with differing statements; both are preserved verbatim and must be reconciled before treating either as canonical.

1. A fn with a non-void, non-error return type is rejected ("not all paths return a value") unless every control-flow path through its body returns (or diverges).
2. A function with a non-void, non-error return type must have every control-flow path produce a value (trailing tail expressions count as implicit returns); otherwise it is an error.

**Source:** `src/compiler/sema_collect.cpp#L4524-L4528`, `src/compiler/sema_decl.cpp#L1071-L1082`

### `item.fn.antiquot-name` — Function with antiquoted name

`[pub] [unsafe] fn #(expr) [<type-params>] ( [params] ) [-> T] block` carries an expr-TOM name (NAME_VAR), valid only inside a quote body; these alts omit the where-clause because NAME_VAR and WHERE share a slot.

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1286-L1293`, `tools/peg_gen/grammars/logos.peg#L1312-L1319`

### `item.fn.def` — Function definition

A function item is `[pub[(vis)]] [unsafe] fn NAME [<type-params>] ( [param_list] ) [-> T] [where-clause] block`. NAME may be IDENT or the contextual keywords `new`/`null`. The where-clause and return type are optional.

**Examples:**

```logos
pub unsafe fn f<T>(x: T) -> T where T: Copy { x }
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1286-L1335`

### `item.fn.empty-body-void` — Omitted return type defaults to void

A fn that declares no return type has return type `()` (void).

**Source:** `src/compiler/sema_collect.cpp#L4477-L4479`, `src/compiler/sema_collect.cpp#L4669-L4671`

### `item.fn.impl-trait-param-desugar` — `impl Trait` param desugars to a generic param

> **Conflict flag:** id extracted with differing titles: "`impl Trait` param desugars to a generic param"; "impl Trait parameter desugars to a fresh generic param".

> **Conflict flag:** multiple source layers describe this id with differing statements; both are preserved verbatim and must be reconciled before treating either as canonical.

1. An `impl Trait` parameter type is desugared into a synthetic generic type-parameter (with the corresponding trait bound) appended to the fn's type-param list.
2. A top-level `impl <bound>` in argument position desugars to a fresh synthetic generic type-parameter (the function becomes ordinary generic); `impl Trait` in RETURN position keeps the opaque-type handling instead.

**Source:** `src/compiler/sema_collect.cpp#L4656-L4666`, `src/compiler/sema_impl.hpp#L2703-L2710`

### `item.fn.impl-trait-return-infer` — impl Trait return inferred from body

A function declared `-> impl Trait` resolves its return type to the single concrete type inferred from the body's return expressions; if none can be inferred it is an error.

**Examples:**

```logos
fn f() -> impl Iterator { 0..3 }
```

**Source:** `src/compiler/sema_decl.cpp#L1061-L1070`

### `item.fn.name-underscore-reserved` — `_` reserved as a function name

A function declaration whose name is the single underscore `_` is ill-formed; `_` is reserved for ignored bindings (so `_(...)` cannot become a valid call expression).

**Examples:**

```logos
fn _() {}  // error: '_' is reserved for ignored bindings
```

**Source:** `src/compiler/sema_decl.cpp#L144-L146`

### `item.fn.nested` — Nested function statement

A `fn name(params) [-> T] { ... }` at statement position is a nested function: its body is lifted to a top-level free function and the local name binds a fn-ptr value. A nested fn captures nothing; reads of enclosing locals are rejected (use a closure instead).

**Source:** `tools/peg_gen/grammars/logos.peg#L1829-L1837`

### `item.fn.never-fallback-precompute` — Body-diverges flag for `!` fallback

A fn whose body always diverges is flagged so that type-argument inference can apply the Rust-2024 `!`-fallback rule.

**Source:** `src/compiler/sema_collect.cpp#L4673-L4679`

### `item.fn.no-mangle` — #[no_mangle] keeps bare symbol name

`#[no_mangle]` on a function causes the bare base name to be used as the symbol name (no name mangling).

**Source:** `src/compiler/sema_collect.cpp#L1753-L1764`

### `item.fn.param-drop-epilogue` — By-value params dropped at function epilogue

By-value droppable (move-type) parameters are dropped at the function epilogue, equivalently to a `let` binding, when the body falls off the end without an explicit terminating return/break/continue. A parameter that was moved on any branch is conservatively skipped from the static epilogue drop (to avoid double-free on the move path).

**Examples:**

```logos
fn consume(_x: Move) {}  // _x dropped at end
```

**Source:** `src/compiler/sema_decl.cpp#L1083-L1117`

### `item.fn.param-list-trailing-comma` — Parameter list trailing comma

A parameter list is `param (, param)* (,)?`, but a trailing comma is forbidden when immediately followed by `...` (the variadic marker), so `, ...` separators are unambiguous.

**Source:** `tools/peg_gen/grammars/logos.peg#L1337-L1342`

### `item.fn.param-pattern` — Pattern-binding parameters

A parameter may bind an irrefutable pattern: a tuple-destructure `(a, b, ...) : T`, a struct pattern `Name { f, .. } : T`, or a slice pattern `[h, t] : T`. Refutable patterns at the fn boundary are rejected in sema with the same diagnostic as for `let`.

**Examples:**

```logos
fn f(Point { x, y }: Point) {}
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1356-L1393`

### `item.fn.param-self-shorthand` — Self-receiver / ref-binding parameter shorthand

A parameter may be `&[mut] IDENT` (reference binding, type elided), `ref IDENT : T`, or `mut IDENT : T` (mutable local binding, mutability invisible to callers).

**Source:** `tools/peg_gen/grammars/logos.peg#L1344-L1355`

### `item.fn.param-variadic` — Variadic parameter

`IDENT : T ...` marks a variadic parameter (IS_VARIADIC); plain `IDENT : T` is the ordinary typed parameter.

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1379-L1382`

### `item.fn.runtime-abi-no-mangle` — main, no_mangle, metacall thunks keep bare symbol

`main`, `#[no_mangle]` functions, and `__metacall_thunk_*` functions suppress package/signature mangling and keep their bare names as link symbols.

**Source:** `src/compiler/sema_collect.cpp#L4858-L4868`

### `item.fn.self-ref-param-type` — self-receiver parameter type

A `&self` / `&mut self` receiver parameter takes the type `&Self` / `&mut Self`, where `Self` is the in-scope Self type; mutability follows the `mut` marker.

**Source:** `src/compiler/sema_collect.cpp#L4498-L4502`, `src/compiler/sema_collect.cpp#L4643-L4647`

### `item.fn.signature-form` — function item signature form

A function is `[pub] [unsafe] [extern] fn NAME [<type-params>] (params) [-> RET_TYPE] BLOCK`, or terminated with `;` when bodyless (declaration only).

**Source:** `src/compiler/sema_render.cpp#L1375-L1398`

### `item.fn.signature-overloading` — Functions overloadable by signature

Functions are keyed by a signature derived from base name, parameter types, and vararg-ness, allowing multiple same-named functions to coexist; only an exact symbol-name collision (same package, base, signature) is a "duplicate function" error.

**Divergence:** Rust does not permit free-function overloading by signature.

**Source:** `src/compiler/sema_collect.cpp#L4712-L4713`, `src/compiler/sema_collect.cpp#L4837-L4881`

### `item.fn.tail-expr-is-return` — Tail expression is implicit return

Inside a fn body, a block's tail expression acts as an implicit return value (typed against the declared return type) for both lowering and reachability analysis.

**Source:** `src/compiler/sema_collect.cpp#L4519-L4523`

### `item.fn.tail-match-as-return` — Tail match arms are return values

When a non-void function's body ends with a `match` expression, each EXPR arm of that tail match is treated as the function's return value.

**Source:** `src/compiler/sema_decl.cpp#L953-L972`

### `item.fn.test-attributes` — #[test] / #[ignore] / #[should_panic] flag functions

On a function, #[test] marks it a test, #[ignore] marks it ignored, and #[should_panic] marks expect-panic; #[should_panic(expected="msg")] records the expected panic substring (string literal, quotes stripped). These flags are reset per-function before reading.

**Source:** `src/compiler/sema.cpp#L7791-L7836`

### `item.fn.test-attrs` — test-harness function attributes

`#[test]`, `#[ignore]`, and `#[should_panic]` are recognised on a function; `#[should_panic(expected = "…")]` records the expected panic substring from its string-literal argument.

**Source:** `src/compiler/sema_collect.cpp#L1767-L1797`

### `item.fn.test-modifiers-require-test` — `#[should_panic]`/`#[ignore]` are `#[test]` modifiers

`#[test]` marks a free function as a test case; `#[should_panic]` and `#[ignore]` are modifiers valid only in combination with `#[test]`. All three apply to functions only.

**Uncertainty:** The 'only valid in combination with #[test]' constraint is enforced downstream, not in this unit (comment-stated).

**Source:** `src/compiler/sema_impl.hpp#L1488-L1493`

### `item.fn.unique-mangled-name` — Each mangled function symbol must have at most one body

Two distinct functions resolving to the same mangled link symbol is an error; in particular a private function in one package and a pub function of the same base name in an imported package must not collide, requiring rename to disambiguate.

**Related:** `module.symbol.function-symbol-name`

**Source:** `src/compiler/mlir_gen_fn.cpp#L219-L235`

### `item.fn.vararg-extern-only` — Variadic functions are extern-only C-ABI declarations

A function declared variadic (vararg) is emitted only as an external declaration with C variadic calling convention; non-vararg parameters are typed normally and a missing return type denotes void.

**Source:** `src/compiler/mlir_gen_fn.cpp#L169-L184`

## Function parameters

### `item.fn-param.datanode-by-value` — DataNode eidos cannot be passed by value

A parameter whose type is (or contains) a DataNode datatype (one holding relative-pointer fields) is rejected by value; it must be passed as `DataRef<T>` because the relative pointers require a zone base pointer.

**Divergence:** Logos addition (zoned/DataNode model); no Rust analog

**Source:** `src/compiler/sema_decl.cpp#L700-L713`

### `item.fn-param.mut-binding` — `mut` parameter binding

A typed parameter `mut x: T` makes `x` a mutable, caller-invisible local binding: the body may reassign or take `&mut` of it. Desugared to an immutable synthetic parameter plus prologue `let mut x = synth;` (a move of the param value into the user local).

**Examples:**

```logos
fn f(mut x: i32) { x += 1; }
```

**Source:** `src/compiler/sema_decl.cpp#L714-L741`, `src/compiler/sema_decl.cpp#L1046-L1060`

### `item.fn-param.owning-box-dyn` — By-value Box<dyn Trait> param owns the box

A by-value parameter of owning trait-object type (`Box<dyn Trait>`) makes the callee own the box: it is dropped at the callee epilogue (vtable drop_in_place + dealloc) and call sites coerce the argument to a heap fat handle.

**Examples:**

```logos
fn f(b: Box<dyn Trait>) {}
```

**Source:** `src/compiler/sema_decl.cpp#L742-L759`

### `item.fn-param.self-reserved` — `self` reserved for impl receivers

A parameter named `self` is an error outside an impl-block; `self` is only the magic receiver inside impl methods.

**Examples:**

```logos
fn f(self: i32) {}  // error outside impl
```

**Source:** `src/compiler/sema_decl.cpp#L687-L694`

### `item.fn-param.struct-pattern` — Struct-pattern function parameter

A parameter may be an irrefutable struct pattern `Name { a, b, ... }: Name`. Each named field (or its `f: binding` rename) becomes a body-visible binding of the field's type; `..` rest is ignored. The pattern is desugared to a synthetic parameter plus prologue `let bind = synth.field;`.

**Examples:**

```logos
fn f(Point { x, y }: Point) -> i32 { x + y }
```

**Source:** `src/compiler/sema_decl.cpp#L604-L648`, `src/compiler/sema_decl.cpp#L999-L1045`

### `item.fn-param.tuple-pattern` — Tuple-destructure function parameter

A parameter may be an irrefutable tuple pattern `(a, b, ...): (T1, T2, ...)`. Each non-`_` element name becomes a body-visible binding of the corresponding tuple element type, desugared to a synthetic parameter plus prologue `let a = synth.0;` etc.

**Examples:**

```logos
fn f((a, b): (i32, i32)) -> i32 { a + b }
```

**Source:** `src/compiler/sema_decl.cpp#L654-L684`, `src/compiler/sema_decl.cpp#L975-L994`

### `item.fn-param.unique-names` — Parameter names must be unique

All parameter names within one function signature must be distinct.

**Source:** `src/compiler/sema_decl.cpp#L765-L768`

### `item.param.no-infer-placeholder` — `_` rejected in fn signature type positions

The inferred-type placeholder `_` is rejected (E0121) when it appears in a fn signature's parameter or return type positions.

**Source:** `src/compiler/sema_collect.cpp#L4660-L4662`, `src/compiler/sema_collect.cpp#L4667-L4672`

### `item.param.self-receiver-and-modifiers` — function parameter and self-receiver forms

A parameter is `[mut] NAME [: TYPE] [...]`; a self-receiver is rendered as `&[mut] self` (a reference parameter without an explicit type). The `...` suffix marks a variadic parameter.

**Source:** `src/compiler/sema_render.cpp#L1101-L1125`

## Static functions

### `item.static-fn.def` — Static (associated) function definition

`[pub] static [unsafe] fn NAME [<params>] (params) [-> T] { ... }` defines an associated/free function with no `self` receiver; its own optional type-parameter list follows the name, matching instance/free fn generics. The name may be the `new` keyword.

**Examples:**

```logos
static fn make<T>(x: T) -> Self { ... }
```
```logos
pub static fn new() -> Self { ... }
```

**Divergence:** `static fn` spelling for associated (no-self) functions; Rust uses an `fn` without a `self` parameter inside an impl.

**Source:** `tools/peg_gen/grammars/logos.peg#L1067-L1093`

## Statics

### `item.static.address-place-machinery` — static items addressed as places

Every `static [mut]` item has link symbol `<pkg>$<NAME>` (extern-block-declared statics keep the bare name); reads lower as a dereference of the static's address and writes as a store through the same address.

**Source:** `src/compiler/sema_impl.hpp#L2886-L2892`, `src/compiler/sema_impl.hpp#L2905-L2910`

### `item.static.aggregate-init-by-copy` — aggregate static initialized by value-copy

If a static's type is an aggregate (struct, zoned struct, tuple, array, slice, closure, or a tagged enum) and its initializer evaluates to a pointer to the value, the static is initialized by copying the full value (size = size_of(T)) into the static's storage; scalar (non-aggregate) statics are initialized by a single store.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L741-L756`

### `item.static.def` — Module-level static definition

`[pub] static [mut] NAME : T = expr ;` defines a true global with stable storage and address (one global symbol; `&STATIC` identity holds), distinct from `const` inline substitution. The `mut` form (matched before the immutable form) marks mutable storage; without `mut`, reads are safe and writes are rejected.

**Examples:**

```logos
static COUNTER: u64 = 0;
```
```logos
static mut FLAG: bool = false;
```

**Source:** `tools/peg_gen/grammars/logos.peg#L705-L716`

### `item.static.extern-requires-unsafe` — Access to an extern-block static requires unsafe

A static declared in an extern block (declaration only, foreign storage) requires `unsafe` at every access.

**Source:** `src/compiler/sema_impl.hpp#L1931-L1933`

### `item.static.global-storage` — static gets global storage with symbol, mut/extern flags

A `static` item is lowered with IS_STATIC set, real global storage keyed by a module-qualified symbol (fallback pkg$name); `static mut` sets IS_MUT; a static lacking VALUE is extern (IS_EXTERN, no initializer emitted).

**Examples:**

```logos
static X: i32 = 5;
```
```logos
static mut Y: i32 = 0;
```

**Source:** `src/compiler/sema.cpp#L7854-L7876`

### `item.static.global-storage-and-mut-safety` — static items have global storage; mut access is unsafe

`static [mut] NAME: T = expr;` is a true global with a stable address and `&STATIC` identity. Reads and writes of a `static mut` require `unsafe`. A static with no initializer is an extern (external-linkage) declaration.

**Source:** `tools/peg_gen/grammars/logos.peg#L322`

### `item.static.immutability-not-by-const-global` — immutable static stays writable storage; immutability enforced at sema

Storage for an immutable (non-`mut`) `static` is NOT a read-only constant; it is writable storage assigned once at startup. Immutability of a non-`mut` static is enforced by rejecting writes during semantic analysis, not by making the storage constant.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L702-L708`

### `item.static.link-symbol` — static link symbol qualification

A `static` with a value is registered with a module-qualified link symbol `<module_id>.<pkg>$<name>` (bare `<pkg>$<name>` when module_id empty) so two modules declaring the same `pkg::NAME` do not collide; an extern static (no value) links against the bare name.

**Source:** `src/compiler/sema_collect.cpp#L1859-L1880`

### `item.static.mut-requires-unsafe` — static mut access requires unsafe

Reading or place-assigning a `static mut` item requires an enclosing `unsafe` block.

**Source:** `src/compiler/sema_impl.hpp#L2880-L2884`

### `item.static.runtime-initialized-storage` — static items get zero-init storage filled at program startup

A non-extern `static` has global storage that is zero-initialized at link time and assigned its declared initializer value at program startup (before `main`), via a synthesized startup initializer running every static's init expression in declaration order. A `static`'s initializer is thus an ordinary runtime-evaluated expression, not a compile-time constant.

**Divergence:** Rust requires `static` initializers to be const-evaluable; Logos evaluates them at runtime startup instead.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L702-L714`, `src/compiler/mlir_gen_dyn.cpp#L716-L758`

### `item.static.shadowing-by-binding` — Local/param binding shadows a module static

A module static name is treated as a static reference only when not shadowed by an in-scope local binding or a type/const-generic parameter of the same name.

**Source:** `src/compiler/sema_impl.hpp#L2894-L2903`

### `item.static.unsafe-access` — static mut and extern static require unsafe

A `static mut` is recorded as a mutable static (its reads/writes require `unsafe`); an extern static (declared with no value) is recorded as an extern static, every access of which requires `unsafe`.

**Source:** `src/compiler/sema_collect.cpp#L1881-L1887`

## Constants

### `item.const.def` — Module-level constant definition

A module constant is `[pub] (const|let) NAME [<params>] : T = expr ;`. The `const` keyword admits an optional type-parameter list, making the RHS a generic compile-time factory substituted at each use site; `let` stays non-generic. Both forms require an explicit type annotation and an initializer.

**Examples:**

```logos
pub const MAX: i32 = 100;
```
```logos
const PMap<K,V>: WritStatic = @{...};
```
```logos
let X: u8 = 1;
```

**Divergence:** `let` accepted as a const keyword at module level; generic `const NAME<...>` factory has no direct Rust analog.

**Source:** `tools/peg_gen/grammars/logos.peg#L688-L699`

### `item.const.generic-and-typed` — const item with optional generics and type

A const item is `[pub] const NAME [<type-params>] [: TYPE] = VALUE ;`; const items may be generic.

**Divergence:** Generic const items (const with type parameters) are a Logos extension.

**Source:** `src/compiler/sema_render.cpp#L1192-L1211`

### `item.const.inlined-value` — const carries an inlined value expression

A `const` item stores its initializer as a VALUE expression that downstream codegen inlines at each use site (contrasted with statics, which have one global per item).

**Examples:**

```logos
const K: i32 = 10;
```

**Source:** `src/compiler/sema.cpp#L7845-L7853`

## Structs

### `item.struct.attr-flags` — structural struct attribute flags

Recognised structural struct attributes set per-struct flags: no_auto_drop, self_describing, rel_ptr, pinned, zone_mut, zoned (zoned2), borrow_carrying, non_null.

**Divergence:** Logos addition (zone/memory-model attributes).

**Source:** `src/compiler/sema_collect.cpp#L1557-L1573`

### `item.struct.custom-dst-last-field-unsized` — Trailing unsized field makes the struct a custom DST

A struct whose LAST field has unsized type (`[T]`, `dyn Trait`, or nested DST) is itself unsized (is_dst); such a struct may appear only behind `&`/`&mut`/`*const`/`*mut`/`Box`, and is constructed via unsafe raw-parts assembly (never by value).

**Source:** `src/compiler/sema_impl.hpp#L2428-L2434`

### `item.struct.explicit-inst` — Explicit struct instantiation declaration

`[pub[(vis)]] struct TYPE_REF ;` where TYPE_REF carries type arguments (e.g. `struct Foo<i64>;`) is an explicit-instantiation declaration binding annotations to a generic struct instantiation. The dedicated `instantiate Foo<T>;` form is preferred.

**Divergence:** A6: see B-item-92 — bare `struct Foo;` is the unit struct, generic form kept for the unbound-typevar diagnostic

**Source:** `tools/peg_gen/grammars/logos.peg#L1133-L1138`

### `item.struct.explicit-instantiation-needs-concrete-args` — Explicit struct/datatype instantiation requires concrete type args

A bodyless `struct Foo<args>;` / `datatype Foo<args>;` (NAME absent, TYPE present) is an explicit instantiation: type args must be concrete (no unbound type vars), else it is an error directing to write the body; a bare `struct Foo;` referencing an undefined name is also an error.

**Source:** `src/compiler/sema.cpp#L7538-L7620`, `src/compiler/sema.cpp#L7668-L7712`

### `item.struct.field-name-unique` — Struct field names must be unique

Within a struct declaration, two fields may not share a name; a duplicate is a compile error.

**Source:** `src/compiler/sema_decl.cpp#L1291-L1294`

### `item.struct.fields-and-inherent-methods` — struct item form with optional inherent methods

A struct is `[pub] struct NAME [<type-params>] { fields... }`, or `[pub] struct NAME [<type-params>] ;` when field-less; each field is `[pub] NAME : TYPE [...]`. Inherent methods may be declared in the struct body, which is equivalent to a separate `impl NAME { ... }` block.

**Divergence:** Legacy `struct Foo { fields, fn ... }` form (methods inside the struct body) is accepted; not a Rust form.

**Source:** `src/compiler/sema_render.cpp#L1140-L1150`, `src/compiler/sema_render.cpp#L1251-L1308`

### `item.struct.generic-inline-method-self` — Inline methods of a generic struct bind Self to the generic self-type

For a generic struct `Struct<T...>`, methods declared in the struct body are lowered as if inside `impl<T...> Struct<T...>`: `Self` is bound to `Struct<T...>`, the struct's type params are recorded as the method's impl type-params, and the impl target pattern is `Struct<T...>` — so `-> Self` (and other Self uses) substitute correctly at monomorphization. Non-generic structs lower body methods with their own type params directly.

**Examples:**

```logos
struct Pair<A,B>{a:A,b:B; fn make(a:A,b:B)->Self{Self{a,b}}}  // Pair::<i32,i32>::make(..) yields Pair<i32,i32>
```

**Source:** `src/compiler/sema_decl.cpp#L1307-L1372`

### `item.struct.generic-method-drops-struct-params` — Generic struct body methods keep only method-level type params

When lowering a body method of a generic struct, type parameters that coincide with the struct's own type parameters are removed from the method's TYPE_PARAMS (mono re-injects them via IMPL_TYPE_PARAMS); only method-introduced type parameters remain method-level.

**Source:** `src/compiler/sema_decl.cpp#L1342-L1364`

### `item.struct.inline-methods-self-binding` — Inline struct-body methods get Self + struct type-params in scope

Methods declared inline in a struct body are collected with `Self` bound to the struct's (possibly generic) self-type and the struct's type parameters installed as impl type-params, identically to `impl`-block methods. For a generic struct, `Self = Name<TVs...>` and generic methods are routed so static calls (`Pair::<i32,i32>::make()`) substitute the struct's params; for a non-generic struct `Self = Name`.

**Source:** `src/compiler/sema_collect.cpp#L4084-L4128`

### `item.struct.lifetime-param-unique` — Struct lifetime parameters must be uniquely named

Within a struct declaration, two lifetime parameters may not share a name; a duplicate is a compile error.

**Source:** `src/compiler/sema_decl.cpp#L1271-L1273`

### `item.struct.named-def` — Named-field struct definition

`[pub[(vis)]] struct IDENT [<type-params>] [where-clause] { field_def_or_doc* method_def_or_doc* }` defines a struct with named fields, optional generics, an optional where-clause, and optional inline method definitions.

**Examples:**

```logos
pub struct S<T> where T: Clone { x: T, fn get(&self) -> &T { &self.x } }
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1149-L1150`, `tools/peg_gen/grammars/logos.peg#L1160-L1161`

### `item.struct.no-auto-drop` — #[no_auto_drop] suppresses compiler-emitted drop

A struct marked `#[no_auto_drop]` receives NO compiler-emitted automatic Drop (neither user-drop invocation nor field drop glue) — the ManuallyDrop<T> lang-item shape.

**Source:** `src/compiler/sema_impl.hpp#L2427`

### `item.struct.repr-other-rejected` — non-transparent struct repr modes rejected

On a struct, `#[repr]` with no argument is an error, and any repr mode other than `transparent` (e.g. `C`, `packed`, `align(...)`) is parse-then-reject (not silently accepted).

**Source:** `src/compiler/sema_collect.cpp#L1583-L1610`

### `item.struct.repr-transparent` — #[repr(transparent)] requires single field

`#[repr(transparent)]` on a struct sets repr_transparent (the wrapper inherits its single field's layout) and requires the struct to have exactly one field, else it is rejected.

**Examples:**

```logos
#[repr(transparent)] struct W(i32)
```

**Source:** `src/compiler/sema_collect.cpp#L1581-L1604`

### `item.struct.self-describing-thin-ptr` — #[self_describing] custom-DST uses a thin raw pointer

A custom-DST struct marked `#[self_describing]` has in-band recoverable tail length/metadata, so raw `*const T`/`*mut T` to it is a THIN pointer (metadata recovered at deref) rather than a fat DstRef.

**Source:** `src/compiler/sema_impl.hpp#L2435-L2439`

### `item.struct.transparent-collapses-layout` — repr(transparent) collapses to the single field's layout

A struct annotated `#[repr(transparent)]` has the layout (size/alignment/ABI) of its single field.

**Uncertainty:** Single-field constraint is enforced elsewhere; this unit only propagates the flag.

**Source:** `src/compiler/sema_decl.cpp#L1205-L1207`

### `item.struct.tuple-def` — Tuple struct definition

`[pub[(vis)]] struct IDENT [<type-params>] ( tuple_field (, tuple_field)* ) ;` defines a tuple struct whose fields are types only; field names are synthesized as "0","1",… so `foo.0` and pattern `Foo(a,b)` work uniformly with named-field structs. Each tuple_field may carry its own `pub`.

**Examples:**

```logos
pub struct Pair(pub i32, i32);
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1151-L1152`, `tools/peg_gen/grammars/logos.peg#L1174-L1180`

### `item.struct.tuple-struct-fields` — Tuple-struct field shape and synthetic names

A struct whose first field definition carries no NAME is a tuple struct. Its positional fields are assigned synthetic decimal names "0", "1", … in declaration order, so member access (`foo.0`) and patterns (`Foo(a, b)`) reuse named-field machinery.

**Source:** `src/compiler/sema_collect.cpp#L4007-L4015`, `src/compiler/sema_collect.cpp#L4035-L4048`

### `item.struct.tuple-struct-positional` — Tuple struct: positional fields, call-form ctor and pattern

`struct Foo(T1, T2);` declares a tuple struct with positional fields; its constructor is the call form `Foo(a, b)` and its pattern is `Foo(x, y)`.

**Source:** `src/compiler/sema_impl.hpp#L2426`

### `item.struct.type-param-unique` — Struct type parameters must be uniquely named

Within a struct declaration, two type parameters may not share a name; a duplicate is a compile error.

**Source:** `src/compiler/sema_decl.cpp#L1265-L1269`

### `item.struct.unit-decl` — Unit struct declaration

`[pub] struct IDENT ;` declares a zero-field (unit) struct. A bare IDENT immediately followed by `;` is a unit struct; `struct Foo<...>;` (IDENT then `<`) is instead parsed as an explicit instantiation. This rule MUST be matched before struct_inst.

**Examples:**

```logos
pub struct Foo;
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1120-L1131`

### `item.struct.where-clause-named-only` — Where-clause only on IDENT-name struct alternatives

A struct/enum definition where-clause is accepted only on the IDENT-NAME alternatives, not on the antiquot (NAME_VAR / `#`-prefixed) alternatives, because WHERE and NAME_VAR share an AST slot.

**Uncertainty:** Slot-sharing is an implementation constraint surfaced as a grammar restriction.

**Source:** `tools/peg_gen/grammars/logos.peg#L1140-L1150`

### `item.struct.zoned-field-promotes-to-datatype` — Struct with a zoned-struct field is not plain data

A struct is plain-data (is_data_plain) unless any of its fields has zoned-struct kind, in which case it is a (non-plain) zoned datatype.

**Source:** `src/compiler/sema_impl.hpp#L2424`

### `item.struct.zoned-promotes-to-datatype` — #[zoned] struct lowered as a datatype (zoned struct)

A struct carrying #[zoned] (promotes_to_datatype) is lowered with IS_ZONED set, treated as a zoned struct/datatype rather than a plain struct.

**Source:** `src/compiler/sema.cpp#L7625-L7660`

### `item.tuple-struct.synthetic-field-names` — Tuple-struct fields named by ordinal

Tuple-struct fields are named by their zero-based positional index rendered as a decimal string ("0", "1", ...).

**Source:** `src/compiler/sema_impl.hpp#L2921-L2932`

## Fields

### `item.field.named` — Named field definition

A struct field is `[pub] IDENT : TYPE_REF [,]`. The contextual keywords `new` and `null` are also accepted as field names. A trailing comma is permitted.

**Source:** `tools/peg_gen/grammars/logos.peg#L1191-L1202`

### `item.field.repeat-group` — Repeat-group field (quote)

`#( field_def ),*` and `#( field_def )*` denote a repeat-group of field definitions (REPEAT_GROUP, OP=1 comma-separated / OP=0 plain), for use in quoted item bodies.

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1183-L1186`

### `item.field.variadic` — Variadic field

A field of form `IDENT ... : TYPE_REF` marks a variadic field (IS_VARIADIC).

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1203-L1204`

## Unions

### `item.union.collected-as-struct` — union shares struct collection shape

A `union NAME { … }` is collected with the same named-field/type-param shape as a struct and registered as a known type, with its `is_union` flag set.

**Examples:**

```logos
union U { i: i32, f: f32 }
```

**Source:** `src/compiler/sema_collect.cpp#L1457-L1469`

### `item.union.def` — Union definition

`[pub[(vis)]] union IDENT [<type-params>] [where-clause] { field_def_or_doc* }` defines a union with named fields and optional generics. It is collected internally as a struct flagged `is_union`; no tuple shape, no methods.

**Examples:**

```logos
union U { a: i32, b: f32 }
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1163-L1172`

### `item.union.field-copy-restriction` — union field types restricted to non-move types

Each non-generic union field type must not be a move type (Vec/Box/String/owning trait object); allowed are Copy types, references, ManuallyDrop<T>, or aggregates thereof. A field whose type is a bare type-parameter is exempt at collection (checked at monomorphization); a field that is itself a union is allowed.

**Divergence:** B: generic-union Copy check is deferred to mono rather than enforced at use site as in Rust.

**Uncertainty:** Slice-1 uses is_move_type as the rejection oracle; full ManuallyDrop/tuple/array recursion is a follow-up.

**Source:** `src/compiler/sema_collect.cpp#L1502-L1530`

### `item.union.field-write-safety` — Union field write is safe, read is unsafe

Writing a union field is safe and does not require `unsafe`; only reading a union field requires an enclosing `unsafe` block.

**Source:** `src/compiler/sema_impl.hpp#L2912-L2919`

### `item.union.layout-and-unsafe-access` — Union layout and unsafe field access

A `union NAME { f: T, ... }` is a struct-shaped type whose size is max-of-fields aligned to max field alignment; reading or writing a union field requires an `unsafe` block.

**Source:** `tools/peg_gen/grammars/logos.peg#L321`

### `item.union.lowered-as-struct` — union lowered through struct path

A `union` definition is lowered through the same path as a struct (same field shape); layout/unsafe-gating is a separate concern.

**Source:** `src/compiler/sema.cpp#L7528-L7537`

### `item.union.max-of-fields-layout-unsafe-read` — union layout and unsafe field read

A type declared `union NAME { … }` has layout = max-of-fields size aligned to max field alignment (vs struct's sum-of-fields); only one field is active at a time (the active one is implementation-defined) and every field READ requires an enclosing `unsafe`.

**Source:** `src/compiler/sema_impl.hpp#L2480-L2487`

### `item.union.no-empty` — fieldless union rejected

A union with zero fields is rejected; a union must declare at least one field.

**Examples:**

```logos
union U {} // error
```

**Source:** `src/compiler/sema_collect.cpp#L1474-L1480`

### `item.union.shared-namespace` — unions share the struct/enum type namespace

Union definitions occupy the same type namespace as structs and enums; a union name conflicts with a struct/enum of the same name, and `type Alias = U;` resolves U as a type.

**Source:** `src/compiler/sema_collect.cpp#L390-L413`

## Enums

### `item.enum.def` — Enum definition

`[pub] enum NAME [<params>] [: backing_type] [where ...] { variants }` defines an enum, with optional generic params, an optional explicit backing integer type after `:`, and an optional where-clause. A metacall-named form `enum #(<expr>) ...` derives the enum name from a compile-time expression. Where-clauses are permitted only on IDENT-named (not expr-named) enums.

**Examples:**

```logos
enum Color { Red, Green, Blue }
```
```logos
enum Tags : u64 { X = 0xdead }
```
```logos
pub enum Option<T> { Some(T), None }
```

**Source:** `tools/peg_gen/grammars/logos.peg#L735-L751`

### `item.enum.default-backing-i32` — Enum default discriminant backing type is i32

An enum with no explicit backing type uses i32 as its discriminant backing type.

**Source:** `src/compiler/sema_impl.hpp#L2583`

### `item.enum.discriminant-const-expr` — enum discriminant from const expression

An enum discriminant may be given by a general const expression (e.g. `1 << 1`), evaluated via CTFE; a `metacall { <expr> }` discriminant must contain a single integer tail expression, evaluated via CTFE to the discriminant value.

**Examples:**

```logos
enum E { A = 1 << 1, B = metacall { 4 } }
```

**Divergence:** A1: const-eval at discriminant position is via metacall/CTFE rather than miri.

**Source:** `src/compiler/sema_collect.cpp#L1985-L2026`

### `item.enum.discriminant-default` — implicit enum discriminant sequencing

An enum variant without an explicit discriminant takes the value 0 for the first such variant and previous+1 thereafter; an explicit value resets the running counter to value+1.

**Source:** `src/compiler/sema_collect.cpp#L1926-L1942`, `src/compiler/sema_collect.cpp#L2097`

### `item.enum.discriminant-fits` — enum discriminant must fit backing type

When an enum has a backing type, each variant's discriminant value must fit in that backing integer type, else it is rejected.

**Source:** `src/compiler/sema_collect.cpp#L2028-L2032`

### `item.enum.discriminant-from-other-enum` — enum discriminant referencing another enum's variant

An enum discriminant may be `OtherEnum::OtherVariant` (with optional `as T` cast dropped): the referent is resolved among already-collected enums and its discriminant value is used verbatim; an unknown enum or variant is rejected.

**Source:** `src/compiler/sema_collect.cpp#L1943-L1984`

### `item.enum.empty-legal` — empty enum body is legal

An enum with an empty body is legal (an uninhabited / marker type); no diagnostic is emitted.

**Examples:**

```logos
enum Void {}
```

**Source:** `src/compiler/sema_collect.cpp#L1900-L1902`

### `item.enum.explicit-discriminant` — Enum variants carry an explicit/assigned discriminant and optional backing type

Each enum variant has an integer discriminant value; an enum may declare an explicit backing integer type for its discriminant.

**Source:** `src/compiler/sema_decl.cpp#L1410`, `src/compiler/sema_decl.cpp#L1486-L1492`

### `item.enum.repr-and-variants` — enum item form

An enum is `[pub] enum NAME [<type-params>] [: TYPE] { variant, ... }` where the optional `: TYPE` gives the discriminant representation type; each variant is `NAME [(types...)] [= [-]discriminant]`.

**Source:** `src/compiler/sema_render.cpp#L1152-L1174`, `src/compiler/sema_render.cpp#L1226-L1249`

### `item.enum.repr-int-width` — #[repr(uN/iN)] sets enum discriminant width

`#[repr(I)]` on an enum where I is an integer type (u8/u16/u32/u64/i8/i16/i32/i64/usize/isize) sets the enum's backing (discriminant) type; it conflicts with (errors against) an already-declared `enum Foo : I'` backing type when I≠I'. `#[repr(C)]` and other non-integer modes are parse-then-reject.

**Examples:**

```logos
#[repr(u8)] enum E { A, B }
```

**Source:** `src/compiler/sema_collect.cpp#L1698-L1744`

### `item.enum.struct-shape-variant` — Struct-shape enum variant carries named payload fields

An enum variant `V { x: T, y: U }` is a struct-shape variant with named payload fields (a names array parallel to payload types); user-written field names are resolved to positional indices. Tuple-shape and unit variants carry no payload field names.

**Source:** `src/compiler/sema_impl.hpp#L2562-L2574`

### `item.enum.type-param-unique` — Enum type parameters must be uniquely named

Within an enum declaration, two type parameters may not share a name; a duplicate is a compile error.

**Source:** `src/compiler/sema_decl.cpp#L1472-L1475`

### `item.enum.variant-name-unique` — Enum variant names must be unique

Within an enum declaration, two variants may not share a name; a duplicate is a compile error.

**Source:** `src/compiler/sema_decl.cpp#L1476-L1479`

### `item.enum.variant-payload-shapes` — enum variant payload shapes

Enum variant payloads may be tuple-style (positional types), struct-shape (named fields, in declaration order, names must be unique), or variadic (single type ref); payload type positions are item signatures in which `_` is rejected (E0121).

**Examples:**

```logos
enum E { Tup(i32, i32), Rec { x: i32 }, Var(i32) }
```

**Source:** `src/compiler/sema_collect.cpp#L2033-L2091`

### `item.enum.variant-shapes` — Enum variant shapes

A variant is one of: unit `Name`; tuple `Name(T, ...)`; variadic-tuple `Name(...T)`; struct-shape `Name { f: T, ... }` (fields may be `pub`); empty struct-shape `Name {}`; or a discriminant-bearing `Name = <disc>`. Variant lists allow leading doc-comments per variant and a trailing comma.

**Examples:**

```logos
Some(T)
```
```logos
Point { x: i32, y: i32 }
```
```logos
Empty {}
```
```logos
Args(...i32)
```

**Divergence:** Variadic-tuple variant `Name(...T)` has no Rust analog.

**Source:** `tools/peg_gen/grammars/logos.peg#L753-L786`, `tools/peg_gen/grammars/logos.peg#L757-L775`

### `item.enum.zoned-attr` — #[zoned]/#[borrow_carrying] on enum

`#[zoned]` on an enum sets its zoned2 flag (niche-enum Ref arm stored self-relative at rest, absolute as value); `#[borrow_carrying]` sets the borrow_carrying flag.

**Divergence:** Logos addition.

**Source:** `src/compiler/sema_collect.cpp#L1681-L1692`

## Writ datatypes

### `item.datatype.def` — Writ datatype definition

A datatype item is `[pub[(vis)]] eidos NAME [<type-params>] { field_def_or_doc* }`. It declares a Writ-fabric datatype with named/repeat-group fields; the optional generic parameter list and visibility marker are accepted.

**Examples:**

```logos
pub eidos Point<T> { x: T, y: T }
```

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1096-L1100`

### `item.datatype.explicit-inst` — Explicit datatype instantiation declaration

`[pub[(vis)]] eidos TYPE_REF ;` (no body) is an explicit-instantiation declaration that binds metadata annotations (e.g. `#[type_code=N]`) to a concrete generic instantiation, e.g. `#[type_code=42] datatype Array<i32>;`.

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1102-L1109`

### `item.datatype.is-zoned` — datatype/eidos is always zoned

A `datatype` (eidos) definition is always lowered with IS_ZONED set, including its specializations.

**Source:** `src/compiler/sema.cpp#L7757-L7761`, `src/compiler/sema.cpp#L7713-L7717`

### `item.datatype.type-code-register` — #[type_code=N] registers explicit type code

`#[type_code=N]` on a datatype registers N as the explicit type code for the datatype's fully-qualified name, making it resolvable by impl-collection in the same pass; `#[annotation]` flags the datatype as a user-annotation type.

**Divergence:** Logos addition (Writ datatype family).

**Source:** `src/compiler/sema_collect.cpp#L1654-L1667`

### `item.datatype.type-code-unique` — exclusive datatype annotations are unique

On a datatype, the exclusive annotations `#[type_code]` and `#[annotation]` may each appear at most once; a duplicate occurrence is rejected.

**Divergence:** Logos addition.

**Source:** `src/compiler/sema_collect.cpp#L1641-L1652`

## Type aliases

### `item.type-alias.def` — Type alias definition

`[pub] type NAME [<params>] = <type_ref> ;` introduces a type alias, optionally generic via a type-parameter list.

**Examples:**

```logos
type Pair = (i32, i32);
```
```logos
pub type Map<K,V> = HashMap<K,V>;
```

**Source:** `tools/peg_gen/grammars/logos.peg#L720-L727`

### `item.type-alias.duplicate` — Type alias uniqueness per package

Two type aliases with the same name in the same package are an error. A same-name alias from a different package is permitted: the incumbent (first/other-package) keeps the bare name slot and the newcomer registers only under its package-qualified key `pkg::Name`. Lookup probes `cur_package_::name` first, so user code resolves to its own alias.

**Uncertainty:** Cross-package shadowing semantics inferred from the registration logic and comment.

**Source:** `src/compiler/sema_collect.cpp#L2127-L2142`

### `item.type-alias.generic` — type alias with optional generics

A type alias is `[pub] type NAME [<type-params>] = TYPE ;`.

**Source:** `src/compiler/sema_render.cpp#L1213-L1224`

### `item.type-alias.no-inferred-rhs` — Type alias RHS may not be the inferred placeholder

A type alias RHS is resolved in item-signature context; `type T = _;` is rejected (no inference context for item signatures). (Rust E0121)

**Source:** `src/compiler/sema_collect.cpp#L2114-L2119`

## Implementations

### `item.impl.items` — Impl item kinds

An impl item is a method definition, an associated-type impl `type NAME [<params>] = T ;`, or an associated-const impl `const NAME : T = expr ;`. Doc-comments may precede impl items.

**Examples:**

```logos
type Item = i32;
```
```logos
const N: usize = 4;
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1054-L1060`, `tools/peg_gen/grammars/logos.peg#L567`

### `item.impl.method-reattach-by-package` — Impl methods are attached to their target template only within the same package

An impl method (mangled `<Struct>__<method>__[fg]__<sig>`) whose `<Struct>` names a generic template is hosted on that template only when the template's package equals the method's package; a method with no package attaches to a sole same-named candidate. A cross-package bare-name collision (e.g. user `Rc` vs stdlib `Rc<T>`) does NOT cause adoption, so the method stays with its own struct's emission.

**Uncertainty:** This is an emission/hosting invariant observable as: same-named generics in distinct packages keep their own methods; surfaced as a language-level guarantee against method mis-hosting.

**Source:** `src/compiler/sema.cpp#L7002-L7054`

### `item.impl.negative` — Negative impl

`impl [<params>] !Trait for <target> [where ...] {}` declares a negative impl (the body must be empty), asserting that the target does not implement Trait.

**Examples:**

```logos
impl !Send for Foo {}
```

**Source:** `tools/peg_gen/grammars/logos.peg#L992-L1004`

### `item.impl.target-mangling` — Impl self-type is mangled to a canonical target key by type shape

The impl target type is reduced to a canonical string key by shape: pointer/named struct → struct name (concrete generic instantiations use the monomorphized concrete name, generic/typevar instantiations keep the base name); `[T]` and `&[T]`/`&mut [T]` → `$slice$T` (typevar elem) or `$slice$<elem>` (concrete); `dyn Tr` → `$dyn$<Trait>`; `&U`/`&mut U` → `$ref_<U>`/`$mut_ref_<U>` (typevar pointee → `$ref$T`/`$mut_ref$T`); tuple `(...)` → `$tuple$N` (typevar elems) / `$tuple$N$<t1>$<t2>...` (concrete) / `$tuple$variadic` (variadic param); fn-pointer → `$fnptr$<arity>`; unit `()` → `void`. Collection and lowering use the same mangling so they agree.

**Source:** `src/compiler/sema_decl.cpp#L1688-L1820`, `src/compiler/sema_decl.cpp#L1782-L1814`

### `item.impl.targets` — Impl block forms and targets

`[unsafe] impl [<impl_params>] [Trait [<args>] for] <target> [where ...] { items }` defines an impl. Trait impls use `Trait for Target`; standalone (inherent) impls omit the trait. The target may be a simple type, pointer, reference, bare slice `[T]`, `dyn Trait`, tuple, or fn-pointer type. Each form admits an optional where-clause before the body.

**Examples:**

```logos
impl Foo { ... }
```
```logos
impl<T> Trait for Struct<T> { ... }
```
```logos
impl Debug for (A, B) { ... }
```
```logos
impl<T> MyTrait for [T] { ... }
```

**Source:** `tools/peg_gen/grammars/logos.peg#L972-L1051`, `tools/peg_gen/grammars/logos.peg#L1021-L1030`

### `item.impl.trait-and-inherent` — impl block forms

An impl block is `[unsafe] impl[<impl-type-params>] TRAIT[<type-args>] for TYPE { items }` (trait impl) or `[unsafe] impl[<type-params>] TYPE { items }` (inherent impl); negative impls are permitted.

**Source:** `src/compiler/sema_render.cpp#L1310-L1373`

### `item.impl.type-params-source` — Impl type parameters come from IMPL_TYPE_PARAMS or (inherent only) TYPE_PARAMS

An impl block's own generic parameters are taken from the generic-trait-impl form `impl<T> Trait for U<T>` (its dedicated parameter list). For an inherent impl `impl<T> U<T>` (no trait), the parameters are taken from the type-parameter list instead. These parameters are in scope throughout the impl's target type, trait args, and method signatures/bodies, and are recorded on each lowered method.

**Source:** `src/compiler/sema_decl.cpp#L1674-L1683`, `src/compiler/sema_decl.cpp#L1866-L1869`

## Traits

### `item.trait.explicit-inst` — Explicit genos/trait specialization declaration

`[pub[(vis)]] <trait-kw> TYPE_REF ;` (no body) binds annotations to a logical-family (genos) specialization of a concrete trait instantiation; implementing eidos inherit the metadata via impl.

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1111-L1118`

## Extern blocks and functions

### `item.extern.abi-whitelist` — extern ABI string whitelist

The ABI string of an `extern "ABI" { … }` block or an `extern "ABI" fn …` item must be one of "C", "C-unwind", "system", or "Rust" (enclosing quotes optional); any other string is rejected.

**Examples:**

```logos
extern "C" { fn puts(s: *const u8) -> i32; }
```

**Divergence:** A7: "C-unwind" is accepted at parse but unwinding-across-FFI is moot (panic=abort).

**Source:** `src/compiler/sema_collect.cpp#L1334-L1344`, `src/compiler/sema_collect.cpp#L1379-L1381`

### `item.extern.block` — Extern block

`[unsafe] extern ["ABI"] { extern_block_item* }` groups same-ABI externs. The optional ABI string applies to all items in the block (inherited at splice). The Rust-2024 `unsafe extern` marker is accepted with no extra semantics.

**Examples:**

```logos
unsafe extern "C" { fn puts(s: *const u8) -> i32; }
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1209-L1227`

### `item.extern.block-flatten` — extern block flattening and ABI inheritance

An `extern "ABI" { extern_fn* }` block flattens to a linear item worklist; each child extern-fn that does not carry its own ABI inherits the block's ABI string, and item collection treats grouped and flat extern fns identically.

**Source:** `src/compiler/sema_collect.cpp#L1346-L1383`

### `item.extern.block-item` — Extern block item (fn / static)

Inside an extern block, items use bare `fn IDENT(params [, ...]) [-> T] ;` (no `extern` keyword; trailing `, ...` makes it variadic) or `static [mut] IDENT : T ;`. The produced extern fn carries no ABI of its own; an extern static with no value is marked external (no initializer).

**Source:** `tools/peg_gen/grammars/logos.peg#L1228-L1243`

### `item.extern.fn-def` — Standalone extern fn declaration

`extern ["ABI"] fn IDENT(params [, ...]) [-> T] ;` declares a single FFI function carrying its ABI string verbatim. A trailing `, ...` makes it variadic. Omitting the ABI string selects the default (Logos-internal) calling convention.

**Source:** `tools/peg_gen/grammars/logos.peg#L1209-L1216`, `tools/peg_gen/grammars/logos.peg#L1244-L1255`

### `item.extern-block.abi-default-to-children` — extern block applies its ABI as a default

`extern "ABI" { extern_fn* }` splices its function items into the module item stream; the block ABI applies as a default to children that do not specify their own ABI. An omitted block ABI is the default Logos-internal ABI.

**Source:** `tools/peg_gen/grammars/logos.peg#L317`

### `item.extern-fn.implicit-pub-unsafe` — extern fn is implicitly pub + unsafe

An `extern fn` declaration is implicitly public, unsafe, and extern.

**Source:** `src/compiler/sema_collect.cpp#L4684-L4688`

### `item.extern-fn.no-mangle-abi-symbol` — extern fn keeps its bare ABI symbol

An extern fn keeps its raw name as the link symbol (no package/signature mangling); duplicate extern declarations of the same name+signature across modules deduplicate to a single symbol rather than erroring.

**Source:** `src/compiler/sema_collect.cpp#L4715-L4729`, `src/compiler/sema_collect.cpp#L4873-L4874`

### `item.module.extern-block-flatten` — extern block children flattened into item stream

An extern block's child items (extern fn declarations) are spliced in order into the flat module-item worklist; the block itself produces no item.

**Source:** `src/compiler/sema.cpp#L7378-L7391`

## Use declarations

### `item.use.path-form` — use declaration path form

A use declaration is `[pub] use NAME(.part)* ;`, where path segments after the head are dot-separated.

**Divergence:** Logos paths use `.` for package/module segments rather than Rust's `::`.

**Source:** `src/compiler/sema_render.cpp#L1036-L1050`, `src/compiler/sema_render.cpp#L1182-L1190`

## Names

### `item.name.forward-reference` — item names are visible before their definition (forward references)

Type names (struct, union, datatype, enum) and trait names are registered in a name-collection pass before bodies are collected, so an item may reference a type or trait declared later in the same or another module, and cross-file `impl Trait for X` resolves regardless of file order.

**Source:** `src/compiler/sema_collect.cpp#L313-L478`

### `item.names.duplicate-in-container` — Duplicate named member is an error

Within a named-member list of a container, any non-empty name that appears more than once is a duplicate error (`duplicate <kind> '<name>' in <container>`). The anonymous binding name `_` (and empty names) may repeat freely.

**Examples:**

```logos
struct S { x: i32, x: i32 } // error: duplicate field 'x'
```

**Source:** `src/compiler/sema_impl.hpp#L1312-L1325`

## Deduplication

### `item.dup.odr-dedup` — structurally identical duplicate items dedup; differing ones error

Two item definitions (struct/union/datatype/enum) sharing the same name in the same package are an error UNLESS their AST sub-trees are structurally equal, in which case the duplicate is silently dropped (ODR-style dedup). Structural equality ignores source-line metadata, so identical items emitted by metaprogramming at different source positions still dedup.

**Divergence:** Logos addition: ODR dedup of metacall-emitted items (Rust has no metacall splice model).

**Source:** `src/compiler/sema_collect.cpp#L25-L75`, `src/compiler/sema_collect.cpp#L267-L282`, `src/compiler/sema_collect.cpp#L378-L446`

## Representation

### `item.repr.recognized-modes` — `#[repr(...)]` minimal recognised modes

`#[repr(...)]` is recognised only on structs (`transparent`) and enums (integer-discriminant width). Other repr modes are parsed and then rejected (no silent acceptance).

**Divergence:** Only `transparent` (struct) and integer-width (enum) repr supported; Rust's `C`/`packed`/`align`/etc. not yet.

**Source:** `src/compiler/sema_impl.hpp#L1501-L1505`

## Generic specialization

### `item.genos.specialization-decl` — genos specialization decl propagates type_code to like-named eidos

A bodyless `genos Name<args>;` (trait-name TYPE, no NAME) records an instantiation annotation; its #[type_code=N] is registered under the canonical and mangled (concrete-struct) names of the like-named eidos/struct, mirrored under both the current and the template's package.

**Source:** `src/compiler/sema.cpp#L7887-L7994`

## Instantiation

### `item.instantiate.generic-only` — instantiate decl requires a generic target with type args

`instantiate T;` requires T to be a struct/datatype/enum with non-empty type args; `instantiate Foo;` on a non-generic type is an error ('only applies to generic templates'), and a non-struct/datatype/enum target is an error.

**Examples:**

```logos
instantiate Foo<i32>;
```

**Source:** `src/compiler/sema.cpp#L7440-L7470`

## Where clauses

### `item.where.clause` — Where clause

`where where_pred (, where_pred)*`. A predicate is `<subject> : trait_bound (+ trait_bound)*` where subject is an associated-type ref, a reference type (`&T`, incl. `for<'a> &'a T`), or a plain type-param; or it is a bare type_param.

**Examples:**

```logos
where T: Clone + Send, &T: Into<U>
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1257-L1271`

## Package declarations

### `module.package.decl` — Package declaration header

A compilation unit begins with `package NAME ('.' IDENT)* ';'`, optionally preceded by inner doc-comments (`//!`, `/*! */`) and inner attributes (`#![...]`). The dotted path gives the package's full name to arbitrary depth (first component = NAME, remaining components = PATH_PARTS). After the package line come zero-or-more use-declarations, then zero-or-more items.

**Examples:**

```logos
package a.b.c;
```
```logos
//! crate doc
#![no_implicit_prelude]
package app;
```

**Divergence:** Rust uses no `package` header; module name is path-derived. Logos requires an explicit `package` line with a dotted package path.

**Source:** `tools/peg_gen/grammars/logos.peg#L489-L490`

### `module.package.decl-syntax` — package declaration

A file's package is declared by a leading `package <dotted-ident>;` statement, where `<dotted-ident>` is a sequence of `[A-Za-z0-9_.]` characters. The declaration may be preceded only by comments, blank lines, and inner attribute lines (`#![...]`); the first non-trivia token must be `package` or the file has no package declaration.

**Source:** `src/compiler/module_loader.cpp#L403-L455`

## Module identity

### `module.identity.empty-disables-qualified-mangling` — No owning module ⇒ no module-qualified mangling

A module's owning-MODULE identity is the unit of distribution it belongs to: `module_id` is the mangle key and `module_name` the canonical handle. A plain user program (no module) has empty identity, which disables module-qualified symbol mangling for its items.

**Source:** `src/compiler/module_loader.hpp#L23-L30`

### `module.identity.package-dotted-name` — Module package is a dotted name; may be empty

Each module carries a dotted package name (e.g. `std.io`); a plain user program with no package declaration has an empty package name.

**Source:** `src/compiler/module_loader.hpp#L19`, `src/compiler/module_loader.hpp#L17-L19`

### `module.id.explicit-or-derived` — Effective module id: explicit id else path hash

The effective module identifier used in symbol mangling is the explicit manifest `id` (sanitized) when set; otherwise it is derived as the FNV-1a 64-bit hash of the module's target install path, formatted as 'm' followed by 16 lowercase hex digits. The 'm' prefix guarantees the token never begins with a digit.

**Source:** `src/compiler/module_manifest.cpp#L85-L98`, `src/compiler/module_manifest.hpp#L73-L79`

### `module.id.mangle-legal-sanitize` — Module id sanitized to mangle-legal token

An explicit module `id` is sanitized into a mangle-legal token: every character that is not [A-Za-z0-9_] is replaced by '_'. The resulting token is what is baked into symbol mangling to disambiguate same-named packages from different modules/versions.

**Source:** `src/compiler/module_manifest.cpp#L76-L83`, `src/compiler/module_manifest.cpp#L88`

### `module.id.one-module-per-archive` — A binary archive carries a single owning module identity

Each binary archive declares at most one owning module via its embedded index header, giving a canonical module name and a mangling id; this identity is stamped onto every item decoded from the archive and is used downstream to qualify the items' symbols.

**Related:** `module.coexist.type-module-qualification`

**Source:** `src/compiler/module_loader.cpp#L1533-L1548`, `src/compiler/module_loader.cpp#L1063-L1067`

## Module manifest

### `module.manifest.blank-and-comment-skip` — Blank and # comment lines ignored

A manifest line that is empty after trimming, or whose first non-whitespace character is '#', is ignored.

**Source:** `src/compiler/module_manifest.cpp#L26-L27`

### `module.manifest.directive-set` — Recognized manifest directives

Recognized manifest directives are: `module` (canonical name), `version`, `id` (mangle key), `root` (source directory), `depends`, `exclude`, `ast_only`, `lowering`, `tier`, `prelude`. `depends`/`exclude`/`ast_only` accumulate one value per occurrence (empty values skipped); the rest set a single value.

**Source:** `src/compiler/module_manifest.cpp#L35-L65`

### `module.manifest.line-oriented-kv` — Manifest is line-oriented key/value

A module manifest is parsed line by line. Each non-empty, non-comment line is split into a key (first whitespace-delimited token) and a value (remainder, trimmed). Surrounding whitespace on the line is trimmed before parsing.

**Source:** `src/compiler/module_manifest.cpp#L24-L33`

### `module.manifest.lowering-eager-default` — lowering directive: lazy|eager, default eager

The `lowering` directive must be exactly `lazy` or `eager`; any other value is an error. Absent the directive, lowering is eager (emit .o + LIR blob); `lazy` emits only the parsed-AST artifact and defers lowering to the consumer.

**Source:** `src/compiler/module_manifest.cpp#L42-L49`

### `module.manifest.prelude-nonempty` — prelude requires a package name

If a `prelude` directive is present, its value must be a non-empty (dotted) package name; an empty value is an error. The named package is injected as an implicit `use <pkg>;` at the head of every file in the module that lacks `#![no_implicit_prelude]`.

**Source:** `src/compiler/module_manifest.cpp#L59-L65`, `src/compiler/module_manifest.hpp#L62-L66`

### `module.manifest.required-module-and-root` — module and root are required

A manifest is invalid (parse fails with an error) unless it declares a non-empty `module` name and a non-empty `root` directory.

**Source:** `src/compiler/module_manifest.cpp#L69-L70`

### `module.manifest.tier-closed-set` — tier directive restricted to lang|mem|std

The `tier` directive must be exactly one of `lang`, `mem`, or `std`; any other (including empty) value is an error. lang = no-alloc/no-OS, mem = heap/no-OS, std = full. An absent tier means tier-not-declared (no availability enforcement).

**Source:** `src/compiler/module_manifest.cpp#L50-L58`, `src/compiler/module_manifest.hpp#L52-L60`

### `module.manifest.unknown-key-ignored` — Unknown directives ignored for forward compatibility

A manifest key that matches no known directive is silently ignored (forward compatibility); it is not an error.

**Source:** `src/compiler/module_manifest.cpp#L66`

### `module.manifest.version-default` — Version defaults to 0.0

If no `version` directive is present, the module version defaults to "0.0".

**Source:** `src/compiler/module_manifest.cpp#L71`

## Module declaration

### `module.decl.package-required-for-import` — A `.logos` file is importable only if it declares a package

Only `.logos` files carrying a `package` declaration are indexed and thus reachable via `use`; a file with no package declaration is silently skipped and cannot be imported by name.

**Source:** `src/compiler/module_loader.cpp#L1254-L1256`, `src/compiler/module_loader.cpp#L1217-L1219`

## Dependencies

### `module.deps.dependency-first-ordering` — Modules processed dependencies-first

Modules are ordered so that for any `use`-edge from package u to package v, v is processed before u. Ordering is package-granular: all files of a package move together, preserving first-seen order within a package and within an SCC; absent a forcing dep edge, original load order is preserved (stable).

**Source:** `src/compiler/module_loader.cpp#L213-L244`, `src/compiler/module_loader.cpp#L378-L388`

### `module.deps.package-cycles-allowed` — Package-level dependency cycles are legal

Cyclic `use` dependencies between packages are legal (e.g. option <-> result, where each package's methods reference the other). Mutually-dependent packages form a strongly-connected component and are compiled together.

**Source:** `src/compiler/module_loader.cpp#L228-L233`

## Module loading

### `module.load.dependency-order` — Modules loaded in dependency order, root last

Compiling a root file transitively resolves and parses all files reachable through `use <pkg>;` declarations; the resulting module sequence is topologically ordered with every dependency preceding any module that uses it and the root module last.

**Source:** `src/compiler/module_loader.hpp#L126-L146`, `src/compiler/module_loader.hpp#L142`

### `module.load.package-atomic-dedup` — Each package loaded at most once; files deduplicated by canonical path

A package is loaded at most once per build (keyed by package name, or by (module,package) under a `from` import); within a load each file is added at most once, identified by its canonical filesystem path.

**Source:** `src/compiler/module_loader.cpp#L1598-L1599`, `src/compiler/module_loader.cpp#L1619-L1620`, `src/compiler/module_loader.cpp#L1583-L1584`

### `module.load.post-order-dependency` — Imported packages are loaded before their importer (post-order)

Package dependencies declared via `use` are loaded depth-first in post-order, so a package's transitive imports are emitted before the package itself; the final module sequence is additionally topologically sorted to make dependency-first ordering uniform across source and binary loads.

**Source:** `src/compiler/module_loader.cpp#L1626-L1627`, `src/compiler/module_loader.cpp#L1664-L1665`, `src/compiler/module_loader.cpp#L1673-L1679`

## Imports

### `module.import.from-pins-module` — `use pkg from <M>` pins resolution to module M's archive

An import `use pkg from <M>;` resolves `pkg` from the archive whose embedded module canonical-name is `M`, independent of which other archive(s) also provide a package named `pkg`. This lets two distinct modules supplying a same-named package coexist; a bare `use pkg;` and `use pkg from M;` are keyed independently and both load.

**Divergence:** Logos addition (`from <module>` import selector); no Rust equivalent.

**Related:** `module.coexist.type-module-qualification`

**Source:** `src/compiler/module_loader.cpp#L1594-L1611`, `src/compiler/module_loader.cpp#L1280-L1282`

### `module.import.from-unknown-falls-through` — `from <M>` with unknown module name falls through to default resolution

If `use pkg from <M>;` names a module `M` for which no loaded archive is registered, resolution falls through to the default text-then-binary path; the precise 'no loaded module' diagnostic is emitted by later semantic analysis rather than the loader.

**Uncertainty:** Loader behavior only; the actual error is emitted in a separate sema unit.

**Source:** `src/compiler/module_loader.cpp#L1603-L1611`

### `module.import.use-pkg-from-module` — use pkg from module restricts type/enum/trait visibility

`use pkg from <module>;` restricts the package's types, enums, and traits (not only its free functions) to the named owning module; a candidate whose package is owned by a different module than the one specified is skipped during resolution.

**Source:** `src/compiler/sema_impl.hpp#L3107-L3118`

## Use declarations (module-aware)

### `module.use.brace-group-desugar` — `use pkg.{a, b, c}` with a lowercase head desugars to N wildcard imports

A grouped use whose head segment begins with a lowercase letter is treated as a package path: `use pkg.{a, b, c}` desugars to wildcard imports `pkg.a.*`, `pkg.b.*`, `pkg.c.*`. A head segment beginning uppercase is instead the enum-variant import form.

**Divergence:** note — Logos path model uses `.` for packages, `::` for items.

**Source:** `src/compiler/sema.cpp#L6835-L6861`

### `module.use.brace-group-import` — brace-group use desugars to per-item wildcard imports

`use pkg.{a, b, c};` (lowercase group head) desugars to wildcard imports `pkg.<head>.a`, `pkg.<head>.b`, `pkg.<head>.c` — bringing each listed package/item into wildcard scope. Distinguished from the enum-variant form by the lowercase first letter of the group head.

**Source:** `src/compiler/sema_collect.cpp#L115-L143`

### `module.use.dotted-path` — Dotted use path

`use a.b.c;` names the package whose dotted path is the concatenation of the head identifier and successive `.`-separated path parts: `a.b.c`. Package paths use `.` as the separator.

**Related:** `module.path.package-name`

**Source:** `src/compiler/module_loader.cpp#L135-L158`

### `module.use.duplicate-warn` — repeated use of same package warns

A `use pkg;` whose package is already in the module's wildcard import scope is a warning (duplicate import); it is otherwise a no-op.

**Source:** `src/compiler/sema_collect.cpp#L176-L183`

### `module.use.enum-variant-alias` — use of enum variants brings bare variant names into scope

`use pkg.Path.Type.{V1, V2, …};` (capitalised Type) registers each Vi as a bare-name alias resolvable unqualified, AND brings the enum type itself into scope (so both `Type::Vi` and bare `Vi` resolve). The bare variant resolves against any in-scope enum that declares it.

**Examples:**

```logos
use std.lang.ord.Ordering.{Less, Equal, Greater};
```

**Source:** `src/compiler/sema_collect.cpp#L90-L174`

### `module.use.from-module` — use with explicit source module

> **Conflict flag:** id extracted with differing titles: "use with explicit source module"; "use pkg from module restricts candidates".

> **Conflict flag:** multiple source layers describe this id with differing statements; both are preserved verbatim and must be reconciled before treating either as canonical.

1. `[pub] use pkg('.'IDENT)* IDENT use_module ';'` imports `pkg.path` from a named module; the trailing bare IDENT is the contextual `from` keyword and `use_module` is the source (a bare name or a quoted string for hyphenated ids, with quotes stripped). The from-bearing alternative is tried before the plain form.
2. `use pkg from <module>;` restricts the candidates of `pkg` to the named module. The `from` keyword is contextual (matched as a bare identifier); a missing/incorrect `from` keyword or a module name matching no loaded module is an error.

**Examples:**

```logos
use foo.Bar from "logos-lang";
```
```logos
pub use a.b.C from othermod;
```

**Divergence:** `use ... from <module>` clause has no Rust analog.
**Divergence:** Logos addition: per-import module qualification (no Rust equivalent).

**Source:** `tools/peg_gen/grammars/logos.peg#L498-L521`, `src/compiler/sema_collect.cpp#L192-L225`

### `module.use.from-module-disambiguation` — use ... from <module> disambiguates provider

> **Conflict flag:** id extracted with differing titles: "use ... from <module> disambiguates provider"; "`use pkg from <module>` restricts visibility to a required module id".

> **Conflict flag:** multiple source layers describe this id with differing statements; both are preserved verbatim and must be reconciled before treating either as canonical.

1. `use pkg from <module>;` binds the import to the named module, disambiguating which archive provides `pkg` when two modules share a package name. The `<module>` operand may be a bare identifier or a double-quoted string literal (surrounding quotes are stripped). Absence of `from` means default resolution.
2. A `use pkg from <module>;` import makes a candidate for that package visible only if its owning module id matches the named module's id; a plain `use pkg;` accepts any module. Unresolved module names make `from` clauses unresolvable.

**Divergence:** Logos-specific: type/package coexistence across modules sharing a name (no Rust analog).

**Source:** `src/compiler/module_loader.cpp#L115-L133`, `src/compiler/module_loader.cpp#L206-L207`, `src/compiler/sema_impl.hpp#L1093-L1110`

### `module.use.from-module-restriction` — `use pkg from "module"` restricts the import to a specific module id

A use of the form `use pkg from "module";` resolves the quoted module name to a module id and restricts the imported package's symbol resolution to that module's exports; the restriction is in force during lowering, not only collection.

**Divergence:** note — part of Logos's C++-style module-linkage system; no direct Rust equivalent.

**Source:** `src/compiler/sema.cpp#L6882-L6905`

### `module.use.group-desugar` — use group desugars to N imports

`use pkg.{a, b, c};` (USE_GROUP / USE_VARIANTS form) desugars to N separate package imports, one per group member, where each lowercase-leading group prefix forms a sub-package: `use pkg.sub.{a,b}` yields imports `pkg.sub.a`, `pkg.sub.b`.

**Source:** `src/compiler/module_loader.cpp#L159-L196`

### `module.use.path` — Plain use declaration

`[pub] use pkg('.'IDENT)* ';'` brings a dotted package path into scope. `pub use` re-exports it. Path components after the head use a leading-dot separator (`.IDENT`).

**Examples:**

```logos
use std.collections.HashMap;
```
```logos
pub use core.Option;
```

**Source:** `tools/peg_gen/grammars/logos.peg#L500-L516`, `tools/peg_gen/grammars/logos.peg#L526-L527`

### `module.use.pub-reexport` — pub use re-exports a package

`pub use pkg;` registers pkg as a re-export from the current package, making it visible to importers of the current package.

**Source:** `src/compiler/sema_collect.cpp#L226-L235`

### `module.use.resolution-on-search-paths` — `use` resolves a dotted package to a source file via search paths

A `use <pkg>;` declaration is resolved by locating the file for the dotted package name `pkg` within the configured search directories; failure to resolve any `use` is an error (B-mv-03/04) reported per declaration.

**Source:** `src/compiler/module_loader.hpp#L126-L127`, `src/compiler/module_loader.hpp#L144-L145`

### `module.use.self-import-noop` — self-import is a no-op

`use P;` where P is the current module's own package is a no-op (own-package symbols always resolve first) and produces a redundancy warning.

**Source:** `src/compiler/sema_collect.cpp#L184-L190`

### `module.use.variant-alias` — `use Enum::{V, W}` brings bare variant names into scope aliased to their enum

An enum-variant use form records each listed bare variant name as an alias to its qualifying enum type, so the variant may be referred to unqualified within the module.

**Source:** `src/compiler/sema.cpp#L6862-L6881`

### `module.use.variant-alias-into-scope` — `use Type::{V1, V2}` brings enum variants into bare scope

A `use pkg.Path.Type::{V1, V2, ...};` import brings the named enum variants into bare scope, so a bare `V1` resolves as `Type::V1`; on name collision, last write wins.

**Source:** `src/compiler/sema_impl.hpp#L1112-L1115`

### `module.use.variant-shorthand` — Enum-variant bare-name import

`use pkg.Path.Type.{V1, V2, ...} ;` brings the named variants of enum `Type` into bare (unqualified) scope. The last dotted component before `.{...}` is the enum type name; the brace-list (trailing comma allowed) names the variants.

**Examples:**

```logos
use core.Option.{Some, None};
```

**Divergence:** Uses `.`-separated path with `.{}` variant group; Rust spells this `use core::Option::{Some, None};` (A: `::`-item / `.`-pkg path model).

**Source:** `tools/peg_gen/grammars/logos.peg#L506-L511`, `tools/peg_gen/grammars/logos.peg#L523-L527`

### `module.use.variant-shorthand-vs-subpackage` — use {..} disambiguated by first-character case

In `use pkg.Path.X.{V1, V2, ...};` the last dotted segment `X` disambiguates by its first character's case: uppercase ⇒ enum-variant bare-name shorthand import; lowercase ⇒ grouped sub-package import.

**Source:** `tools/peg_gen/grammars/logos.peg#L309`

### `module.use.variant-vs-subpackage-by-case` — Group target classified by first-character case

In a USE_VARIANTS group `use pkg.X.{...};`, the bracketed target `X` is classified by its first character: lowercase-leading `X` is treated as a grouped sub-package import (each member becomes `pkg.X.<member>`); uppercase-leading `X` is treated as an enum-variant import, importing the enclosing package `pkg` as a wildcard so the type is in scope. This relies on the convention that enum/type names are capitalized.

**Divergence:** Disambiguation by identifier capitalization is a Logos convention, not a Rust rule.

**Source:** `src/compiler/module_loader.cpp#L167-L204`

## Re-exports

### `module.reexport.transitive-pub-use` — Transitive pub-use re-export resolution

Resolving a name in the context of imported packages searches each imported package plus, transitively and cycle-safely, all packages reachable through their `pub use` re-exports.

**Source:** `src/compiler/sema_impl.hpp#L3059-L3085`

## Prelude

### `module.prelude.binary-modules-not-augmented` — Binary-archive modules keep their original prelude

Files loaded from a binary archive are never re-augmented with the consumer's implicit prelude; the prelude in effect at their original build time is final.

**Source:** `src/compiler/module_loader.hpp#L131-L134`

### `module.prelude.cross-cutting-auto-load` — Cross-cutting foundation packages auto-load without explicit `use`

Foundation packages under prefixes `std.lang`, `std.writ`, or `logos.lang` (excluding the `logos.lang.writ` substrate) are implicitly available to every compilation: when an archive is loaded for a requested package, sibling packages with these prefixes are also loaded so cross-cutting traits and types (Default, Ord, Send, Clone, etc.) resolve without an explicit import edge.

**Divergence:** Logos addition: implicit prelude is prefix-scoped to the lang tier (transitional; manifest-tier system intended).

**Uncertainty:** Exact prefix set is a transitional heuristic per source comments.

**Source:** `src/compiler/module_loader.cpp#L1397-L1432`, `src/compiler/module_loader.cpp#L1566-L1571`

### `module.prelude.implicit-auto-import` — Implicit prelude auto-imported per file

Every source file implicitly imports the prelude package in addition to its explicit `use` declarations, unless the file opts out. The implicit prelude is deduplicated against explicit uses (no duplicate import if already named).

**Divergence:** Logos uses a named prelude *package*; the model parallels Rust's std prelude but is package-granular.

**Source:** `src/compiler/module_loader.cpp#L95-L103`

### `module.prelude.implicit-injection` — Implicit prelude injected unless opted out

> **Conflict flag:** id extracted with differing titles: "Implicit prelude injected unless opted out"; "Implicit prelude injected into source files"; "Implicit prelude is wildcard-imported into source modules"; "implicit prelude is injected unless opted out".

> **Conflict flag:** multiple source layers describe this id with differing statements; both are preserved verbatim and must be reconciled before treating either as canonical.

1. An implicit prelude package is injected into every non-binary user file that does not carry `#![no_implicit_prelude]`, making prelude names resolvable unqualified; binary (precompiled) inputs and files with the opt-out attribute receive no implicit prelude.
2. A configured implicit-prelude package is injected as an implicit `use` into every source file loaded for the current compilation, except files carrying the inner attribute `#![no_implicit_prelude]`. An empty implicit-prelude setting injects nothing.
3. Each source-side module (not binary-archive ASTs) implicitly gains a wildcard import of the configured prelude package, unless the module is the prelude itself or already imports it. A module opts out with the inner annotation `#![no_implicit_prelude]`.
4. Each non-prelude module implicitly imports the manifest-declared prelude package into wildcard scope, unless the file contains an inner attribute `#![no_implicit_prelude]`. The prelude is not re-injected into the prelude package itself, and is deduplicated against an explicit `use` of the same package.

**Examples:**

```logos
#![no_implicit_prelude]
```

**Divergence:** Logos uses a named injectable prelude package + `#![no_implicit_prelude]` opt-out (Rust-analogous but explicitly package-configured).
**Divergence:** A6/note — prelude package is Logos's package-model analogue of Rust's std prelude.

**Related:** `module.use.brace-group-desugar`

**Source:** `src/compiler/metaprog_dispatch.hpp#L76-L80`, `src/compiler/module_loader.hpp#L130-L134`, `src/compiler/sema.cpp#L6911-L6936`, `src/compiler/sema_collect.cpp#L240-L266`

### `module.prelude.implicit-unless-opted-out` — Implicit prelude visible to non-binary ASTs without no_implicit_prelude

An implicit-prelude package, when configured, is added to the wildcard import scope of every non-binary compilation unit that does not carry `#![no_implicit_prelude]`; an empty prelude name means no implicit prelude.

**Source:** `src/compiler/sema_impl.hpp#L786-L792`

### `module.prelude.no-double-load` — A prelude sibling already supplied by source is not re-loaded from binary

An auto-loaded prelude sibling package is skipped when the source (text) index already provides that package, preventing the same package's items/impls from being registered twice (which would otherwise produce duplicate-definition / conflicting-impl errors). The explicitly requested package is always loaded.

**Source:** `src/compiler/module_loader.cpp#L1566-L1571`, `src/compiler/module_loader.cpp#L1555-L1565`

### `module.prelude.opt-out-attribute` — #![no_implicit_prelude] suppresses implicit prelude

A file-level inner attribute `#![no_implicit_prelude]` suppresses the implicit prelude import for that file. It is recognized as an INNER_ANNOTATION item with name `no_implicit_prelude`.

**Source:** `src/compiler/module_loader.cpp#L57-L78`, `src/compiler/module_loader.cpp#L96`

## Visibility and module linkage

### `module.visibility.bare-cross-package-pub-check` — Cross-package items checked even via bare key

An item resolved through the bare/unqualified key is subject to the same pub-access check as a package-qualified hit when it belongs to a package different from the current one; only own-package bare entries (primitives/builtins) bypass the check.

**Source:** `src/compiler/sema_impl.hpp#L3133-L3150`

### `module.visibility.private-cross-package` — Non-pub items are private across packages

An item not marked `pub` (and not `pub(module)`) is inaccessible from a different package; cross-package reference is an error.

**Source:** `src/compiler/sema_collect.cpp#L760-L761`

### `module.visibility.pub-module-linkage` — pub(module) has module-linkage across packages

> **Conflict flag:** id extracted with differing titles: "pub(module) has module-linkage across packages"; "pub(module) types are module-private across modules".

> **Conflict flag:** multiple source layers describe this id with differing statements; both are preserved verbatim and must be reconciled before treating either as canonical.

1. A `pub(module)` item is visible to other packages only within its OWNING module: cross-package access is allowed iff the defining module id equals the current module id, otherwise it is module-private (error). Checked before the plain `pub` test since `pub(module)` also sets is_pub.
2. A `pub(module)` item accessed from outside its owning module produces a module-private diagnostic; module-linkage info (the package's owning module id) is consulted on each cross-package resolution.

**Source:** `src/compiler/sema_collect.cpp#L751-L759`, `src/compiler/sema_impl.hpp#L3119-L3129`

### `module.visibility.pub-module-only` — Restricted visibility only `pub(module)`

> **Conflict flag:** id extracted with differing titles: "Restricted visibility only `pub(module)`"; "pub(module) exports within the owning module only".

> **Conflict flag:** multiple source layers describe this id with differing statements; both are preserved verbatim and must be reconciled before treating either as canonical.

1. An item's restricted-visibility marker `pub(W)` is accepted only when W is the contextual word `module` (module-linkage). Plain `pub` and no marker are non-module. Any other word (e.g. `pub(crate)`, `pub(super)`) is rejected: `unsupported visibility `pub(W)` — only `pub(module)` is recognised`.
2. An item marked `pub(module)` is exported within its owning module (module-linkage, crossing package boundaries inside the module) but not to external consumers; it implies pub at the package level.

**Examples:**

```logos
pub(module) fn f() {}
```
```logos
pub(crate) fn g() {} // error
```

**Divergence:** Logos has only `pub` and `pub(module)`; Rust's `pub(crate)`/`pub(super)`/`pub(in path)` are not recognised.

**Source:** `src/compiler/sema_impl.hpp#L1176-L1191`, `src/compiler/sema_impl.hpp#L2420`, `src/compiler/sema_impl.hpp#L2524-L2527`, `src/compiler/sema_impl.hpp#L2638`

### `module.visibility.same-package` — Same-package access is always permitted

Visibility checks are skipped when scope context is absent (empty defining or current package); access to any item whose defining package equals the current package is always allowed regardless of `pub`.

**Source:** `src/compiler/sema_collect.cpp#L746-L750`

### `module.vis.struct-pub-and-module-only` — Struct/datatype visibility flags

A struct, datatype, or struct field is public iff marked `pub`; a struct/datatype may additionally be `pub(module)` (module-only visibility). Field publicness is read per-field from the IS_PUB flag.

**Source:** `src/compiler/sema_collect.cpp#L3849-L3851`, `src/compiler/sema_collect.cpp#L3990-L3994`

## Name resolution

### `module.resolve.scope-order` — Name resolution order: current package, imports, bare

An unqualified type/enum/trait name resolves by trying the current package key, then each imported package and its transitive `pub use` re-exports, then the bare (legacy/unqualified) key.

**Source:** `src/compiler/sema_impl.hpp#L3096-L3151`

### `module.resolve.text-over-binary` — Source package resolution precedes binary archive resolution

A `use pkg;` import resolves `pkg` by first consulting the source (text) package index built from `.logos` files; only if no source package declares `pkg` is the binary archive index consulted. If neither supplies `pkg`, compilation fails with a 'cannot find package' error.

**Source:** `src/compiler/module_loader.cpp#L1613-L1641`

## Name lookup

### `module.lookup.unqualified-name-scope` — Unqualified type name lookup scope

An unqualified type name is known if it matches a primitive, an in-scope type param, or — for structs/datatypes/enums/aliases — a binding keyed unqualified, or in the current package, or in any wildcard-imported package.

**Source:** `src/compiler/sema_collect.cpp#L4181-L4204`

## Paths

### `module.path.package-name` — Package name is dot-joined module path

A package's fully-qualified name is its module NAME with each PATH_PART name appended joined by `.` (e.g. `my.cool.pkg`).

**Divergence:** A9

**Source:** `src/compiler/sema_collect.cpp#L731-L744`

### `module.path.qualified-call` — Package-qualified call constrains free-fn resolution to that package

A call `pkg.path::fn(args)` carries a dotted package qualifier (RECEIVER + QUAL_PARTS joined by `.`); free-function resolution for that call is restricted to the named package.

**Divergence:** A9: packages are `.`-separated, items reached via `::`.

**Source:** `src/compiler/sema_expr.cpp#L2727-L2757`

### `module.path.qualified-member-fallback` — Qualified call with no matching free fn is a type-member call

If a qualified `pkg.path.Member(args)` has no free function of that name in the package, the last dotted segment is interpreted as a type and the call is resolved as a static/associated method call `pkg.path.Type::method(...)`. A matching free function takes precedence.

**Source:** `src/compiler/sema_expr.cpp#L2772-L2781`

## Calls

### `module.call.package-qualifier-disambiguates` — Explicit package qualifier filters free-fn candidates by exact package

A call written with an explicit dotted package qualifier (`logos.lang.mem::replace(..)`) restricts free-fn candidate resolution to functions whose `.package` equals that qualifier exactly, disambiguating same-named free fns across packages. An empty qualifier means an unqualified call resolved through normal import scope.

**Source:** `src/compiler/sema_impl.hpp#L3706-L3720`

## Symbol mangling

### `module.symbol.function-symbol-name` — Function link symbol = pkg/module-qualified mangle; extern and methods carved out

A function's link symbol is built from {module_id, package, base, signature, is_generic, is_method, is_extern} via the canonical sym::mangle encoder. Extern functions keep their bare ABI/C name; struct methods (base containing '__') are disambiguated by their struct's pkg-qualified name rather than re-qualified here. Two packages defining the same base+signature get distinct symbols.

**Source:** `src/compiler/sema.cpp#L1543-L1568`

### `module.symbol.mangle-by-module-id` — Symbol mangling is keyed by owning-module id

Each compilation unit carries an owning-module id that is baked into symbol mangling, so identically-named packages from different modules (or versions) produce distinct symbols (C++ module-linkage model). One package maps to one module id within a coherent build.

**Source:** `src/compiler/sema_impl.hpp#L1083-L1095`

### `module.symbol.method-link-prefix` — Methods are emitted under module-qualified link symbols

A function's emitted link symbol is its module-qualified name: methods gain the module prefix; free functions and extern functions keep their bare name. Symbol identity and forward-declaration deduplication key off this link name.

**Related:** `item.fn.unique-mangled-name`

**Source:** `src/compiler/mlir_gen_fn.cpp#L164-L168`, `src/compiler/mlir_gen_fn.cpp#L186-L208`

## Export catalogs

### `module.exports.blanket-impl-catalog` — Blanket impls catalogued by trait and bounds

An exported blanket impl `impl<T: Bound + Extra...> Trait for T` is catalogued as (trait, primary bound, extra bounds), with the target type being the type variable by definition; an unbounded blanket impl has empty primary bound.

**Source:** `src/compiler/module_loader.hpp#L79-L86`

### `module.exports.concrete-impl-catalog-drops-negative-dst` — Concrete-impl catalog excludes negative and DST-target impls

An exported concrete impl `impl Trait for Target` is catalogued as (trait, target); negative impls and DST target patterns are dropped from the catalog (which is a fast-path lookup index only, not the authoritative impl set).

**Source:** `src/compiler/module_loader.hpp#L87-L93`

### `module.exports.merge-later-archive-wins` — Export merge across archives is order-preserving, later wins on fn duplicate

When export catalogs from multiple archives are unioned, archive order is preserved and a later archive wins on a duplicate function-template mangled symbol (occurs only when a project redefines a stdlib mangled symbol).

**Source:** `src/compiler/module_loader.hpp#L106-L112`

### `module.exports.template-catalog-non-generic-excluded` — Stdlib export catalog lists exactly the generic items

A binary module's exports catalog records precisely the items whose `type_params` is non-empty (generic struct/enum/fn templates); non-generic items are not catalogued as templates.

**Source:** `src/compiler/module_loader.hpp#L40-L45`, `src/compiler/module_loader.hpp#L73-L78`

## Binary archives

### `module.binary.archive-membership` — Modules may be loaded from binary `.writ0` archives

A module may be loaded from a precompiled `.writ0` member inside a `.a` archive rather than from source; binary modules take their `module_id`/`module_name` from the archive's `@module` `.pkgi` header.

**Source:** `src/compiler/module_loader.hpp#L21`, `src/compiler/module_loader.hpp#L24-L27`

### `module.binary.lazy-local-lowering` — Lazy archives are lowered as local code

A lazy-mode binary archive ships only the parsed AST (no object text, no LIR blob); the consumer must lower such a module's items locally (sema/mono/codegen treat them as user code).

**Source:** `src/compiler/module_loader.hpp#L32-L37`

## Statics across modules

### `module.static.library-defers-to-executable` — library build declares statics extern; only the executable owns storage + init

In a library build (no `main`), every `static` is emitted as an external declaration with no storage and no startup initializer. The final executable, which transitively re-lowers all used statics from imported module metadata, is the sole owner of each static's storage and its startup initialization. Extern (`extern`-declared / FFI) statics are always external declarations regardless of build kind.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L668-L681`, `src/compiler/mlir_gen_dyn.cpp#L694-L701`, `src/compiler/mlir_gen_dyn.cpp#L716-L723`

## ABI compatibility

### `module.abi.one-directional-minor-compat` — Binary archive ABI compatibility is one-directional within a major version

A compiler may consume a binary library iff (a) the library's language major version equals the compiler's, and (b) for stable releases the library's minor version is <= the compiler's minor. A differing major is incompatible; a library built by a newer minor is rejected. An ABI-incompatible archive is not indexed (its packages become unavailable). Identical version strings are always compatible; legacy archives without a version stamp are not enforced.

**Divergence:** Logos addition: semantic-version ABI gate on binary modules (Rust has no stable cross-version library ABI).

**Source:** `src/compiler/module_loader.cpp#L1100-L1144`, `src/compiler/module_loader.cpp#L1182-L1184`

### `module.abi.prerelease-no-guarantee` — Pre-release / snapshot builds require exact version match

If either the library or the compiler is a pre-release (`-pre`) or snapshot (`+meta`) build, no ABI guarantee holds: only an exact version-string match is silently accepted; any mismatch is permitted but warned. The check is disabled entirely by environment override.

**Divergence:** Logos addition.

**Source:** `src/compiler/module_loader.cpp#L1100-L1113`, `src/compiler/module_loader.cpp#L1130-L1142`, `src/compiler/module_loader.cpp#L1110-L1110`

## Attributes

### `module.attr.inner-vs-item-attribute` — Inner attribute applies at file/module level

`#![name]` / `#![name(args)]` / `#![name=val]` is a file/module-level inner attribute, distinct from per-item `#[...]` attributes.

**Source:** `tools/peg_gen/grammars/logos.peg#L310`

## Type/package coexistence

### `module.coexist.type-module-qualification` — Type symbols are module-qualified ($M<id>) for same-pkg coexistence; stdlib exempt

Every type-keyed symbol embeds the owning module's id as a '$M<module_id>' suffix on the package, so two separately-compiled modules declaring the same pkg::Type do not collide at link. stdlib packages (prefix 'logos.') and an empty/absent pkg-to-module map yield no suffix (byte-identical output).

**Source:** `src/compiler/sema.cpp#L1404-L1415`, `src/compiler/sema.cpp#L1460-L1463`

## Exclusion

### `module.exclude.path-prefix-defers-to-binary` — Excluded path prefixes are not picked up as source

Absolute path prefixes declared as excludes (mirroring the manifest `exclude` directive) remove matching files from the text-package index: a `use` whose resolution begins with an excluded prefix is not loaded from source, deferring that package to a binary archive instead.

**Source:** `src/compiler/module_loader.hpp#L135-L141`

## Tag dispatch

### `module.tagdispatch.binary-archive-provides-tables` — Fully-binary tag systems are provided by the archive

A tag system whose every registered callee is already present in a linked binary archive is not re-defined; the consuming unit emits only external references to that system's tables, lookup function, and initializer. Tables also present in an archive use weak (deduplicating) linkage rather than triggering a duplicate-definition error.

**Divergence:** Module/separate-compilation model; no direct Rust analogue.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L219-L292`, `src/compiler/mlir_gen_dyn.cpp#L358-L368`, `src/compiler/mlir_gen_dyn.cpp#L394-L396`
