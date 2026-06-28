# Items

Scope: item-level and module-level constructs of Logos (domains `item` and `module`) — functions, constants, statics, type aliases, structs, fields, unions, enums, Writ datatypes, traits, impls, extern blocks, visibility, attributes, doc comments, conditional compilation, packages, use-imports, prelude, and name resolution. Rules are extracted from two source layers: the PEG grammar (`grammar/logos`) and the semantic-analysis passes (`sema/*`). Each rule id is a permanent, linkable address; ids are never renamed or merged. Where a grammar-layer rule and a sema-layer rule share an id, both statements are surfaced under the single id.

## Domain: item

## Item kinds

### `item.kinds.set` — Module item alternatives

A module item is one of: doc-comment, annotation, template decl, const/static def, type alias, enum def, datatype def/inst, trait def/inst, struct unit/def/inst, instantiate decl, item-position metacall, fn-macro item invocation, union def, impl block, extern block, extern fn, or fn def — each in plain and `pub` forms where visibility applies.

_Source: `tools/peg_gen/grammars/logos.peg#L529`_

## Visibility

### `item.visibility.pub-module` — Visibility marker pub / pub(module)

Item visibility is `pub` (fully exported) or `pub(IDENT)` where IDENT is a contextual keyword validated == "module" in sema, meaning module-linkage: visible to other packages of the SAME module but not exported to consumers.

```logos
pub(module) fn helper() {}
```

**Divergence:** Logos uses `pub(module)` for module-linkage; Rust uses `pub(crate)`/path-restricted visibilities.

_Source: `tools/peg_gen/grammars/logos.peg#L1273-L1284`_

## Annotations

### `item.annotation.arg-forms` — Attribute argument forms

Within an attribute argument list, an argument is one of: `IDENT(args)` (nested call), `IDENT = lit` (key-value), a bare literal (positional), or a bare IDENT (legacy). A literal may be an enum ref `IDENT::IDENT`, raw/normal string, float, integer, `true`/`false`, or a bracketed array of literals. Lists allow a trailing comma.

```logos
#[cfg(target = "x86")]
```
```logos
#[align(8)]
```
```logos
#[list([1, 2, 3])]
```

_Source: `tools/peg_gen/grammars/logos.peg#L648-L672`_

### `item.annotation.attribute-forms` — annotation/attribute syntax

An annotation is `#[NAME]`, `#[NAME = literal]`, or `#[NAME(args...)]`; arguments may be positional or `key = value`, and an argument value may be an array literal `[ ... ]`.

_Source: `src/compiler/sema_render.cpp#L1400-L1460`_

### `item.annotation.forms` — Outer attribute forms

An attribute is `#[ NAME (args) ]`, `#[ NAME = val ]`, or `#[ NAME ]`. The `= val` form admits an enum literal `IDENT::IDENT` or an integer.

```logos
#[derive(Debug)]
```
```logos
#[repr = 8]
```
```logos
#[inline]
```

_Source: `tools/peg_gen/grammars/logos.peg#L619-L641`_

### `item.annotation.inner-attribute` — Inner attribute attaches to enclosing module

An inner attribute `#![ ... ]` (with the same `(args)` / `= val` / flag payload shapes as an outer attribute) attaches to the enclosing module rather than the following item. (Currently only `#![no_implicit_prelude]`.)

```logos
#![no_implicit_prelude]
```

_Source: `tools/peg_gen/grammars/logos.peg#L631-L636`_

## Attributes

### `item.attr.datatype-promotion` — #[datatype]/#[annotation] promote a struct into the datatype pipeline

A struct-syntax item annotated `#[datatype]` or `#[annotation]` is treated as a datatype declaration; `#[zoned]` marks self-relative fields and does NOT promote a struct to a datatype.

**Divergence:** Logos addition: datatype/annotation/zoned attributes (no Rust equivalent).

_Source: `src/compiler/sema_collect.cpp#L366-L431`_

### `item.attr.unknown-warn` — unknown attribute is warned

A top-level user `#[name]` attribute that is neither a builtin attribute, a registered metaprog-handler trigger, nor the name of an `#[annotation]` datatype is a warning (likely typo, missing import, or removed handler).

_Source: `src/compiler/sema_collect.cpp#L607-L665`_

## Doc comments

### `item.doc.comment-attached-to-next-item` — Doc comments attach as documentation

Outer doc comments (`///`, `/** */`) accumulate and attach to the next item; inner doc comments (`//!`, `/*! */`) accumulate into the module-level inner documentation. The comment markers are stripped.

_Source: `tools/peg_gen/grammars/logos.peg#L305-L308`_

### `item.doc.comment-strip` — doc comment accumulation and prefix stripping

`///` line docs strip the leading `///` plus one optional space; `/** */` outer block docs and `/*! */`/`//!` inner docs accumulate; outer docs attach to the next non-doc item, inner docs accumulate into a per-module inner-doc buffer joined by newlines and never attach to a specific item.

_Source: `src/compiler/sema_collect.cpp#L1404-L1436`_

### `item.doc.inner-module` — Inner doc-comment is module summary

_This id is described by 2 source layers; both statements are preserved._

- An inner doc-comment (`//!` line or `/*! */` block) accumulates into the enclosing module's doc summary and is never attached to a specific item.
- Inner doc comments `//!` (line) and `/*! */` (block) accumulate into a module-level inner-doc buffer (stripping marker and one leading space, joining with `\n`) and are committed as the module's documentation after all items.

_Source: `tools/peg_gen/grammars/logos.peg#L541-L554`, `src/compiler/sema.cpp#L7412-L7428`, `src/compiler/sema.cpp#L8022-L8030`_

### `item.doc.outer-block` — Outer block doc-comment

An outer block doc-comment `/** ... */` is an item/member-stream element with the same next-item binding role as line doc-comments; the `/**` envelope and per-line leading `*` are stripped and lines joined with newline.

_Source: `tools/peg_gen/grammars/logos.peg#L544-L549`_

### `item.doc.outer-line` — Outer line doc-comment binds to next item

An outer line doc-comment (`///`, captured as DOC_LINE) is an item-stream element; consecutive outer doc-comments accumulate and attach to the next real item.

_Source: `tools/peg_gen/grammars/logos.peg#L536-L537`_

### `item.doc.outer-line-block` — Outer doc comments attach to next item

Outer doc comments `///` (line) and `/** */` (block) accumulate, stripping the marker prefix and joining lines with `\n`, and attach to the next lowered item.

_Source: `src/compiler/sema.cpp#L7399-L7411`_

## Conditional compilation

### `item.cfg.conditional-compilation` — cfg attributes drop items

An item whose `#[cfg(...)]` predicate evaluates false is dropped (not lowered), discarding its pending annotations and doc; this enables same-name platform switches such as `#[cfg(unix)]`/`#[cfg(windows)]`.

_Source: `src/compiler/sema.cpp#L7430-L7439`_

### `item.cfg.drop-disabled` — cfg-disabled items are dropped

Before collecting an item, `cfg_attr` activations are applied and `cfg(...)` predicates evaluated against pending annotations; if any predicate is false the item is dropped entirely (neither collected nor lowered) together with its pending annotations.

_Source: `src/compiler/sema_collect.cpp#L1437-L1446`_

### `item.cfg.gate-before-registration` — cfg-false items do not register their name

A `#[cfg(...)]` predicate is evaluated before name registration; an item whose cfg is false registers no name. This permits the same-name-under-mutually-exclusive-cfg idiom (e.g. cfg(unix)/cfg(windows) structs) without a duplicate-name error.

_Source: `src/compiler/sema_collect.cpp#L358-L365`_

## Functions

### `item.fn.all-paths-return` — Non-void fn must return on every path

_This id is described by 2 source layers; both statements are preserved._

- A fn with a non-void, non-error return type is rejected ("not all paths return a value") unless every control-flow path through its body returns (or diverges).
- A function with a non-void, non-error return type must have every control-flow path produce a value (trailing tail expressions count as implicit returns); otherwise it is an error.

_Source: `src/compiler/sema_collect.cpp#L4524-L4528`, `src/compiler/sema_decl.cpp#L1071-L1082`_

### `item.fn.antiquot-name` — Function with antiquoted name

`[pub] [unsafe] fn #(expr) [<type-params>] ( [params] ) [-> T] block` carries an expr-TOM name (NAME_VAR), valid only inside a quote body; these alts omit the where-clause because NAME_VAR and WHERE share a slot.

**Divergence:** A6

_Source: `tools/peg_gen/grammars/logos.peg#L1286-L1293`, `tools/peg_gen/grammars/logos.peg#L1312-L1319`_

### `item.fn.def` — Function definition

A function item is `[pub[(vis)]] [unsafe] fn NAME [<type-params>] ( [param_list] ) [-> T] [where-clause] block`. NAME may be IDENT or the contextual keywords `new`/`null`. The where-clause and return type are optional.

```logos
pub unsafe fn f<T>(x: T) -> T where T: Copy { x }
```

_Source: `tools/peg_gen/grammars/logos.peg#L1286-L1335`_

### `item.fn.empty-body-void` — Omitted return type defaults to void

A fn that declares no return type has return type `()` (void).

_Source: `src/compiler/sema_collect.cpp#L4477-L4479`, `src/compiler/sema_collect.cpp#L4669-L4671`_

### `item.fn.impl-trait-param-desugar` — `impl Trait` param desugars to a generic param

An `impl Trait` parameter type is desugared into a synthetic generic type-parameter (with the corresponding trait bound) appended to the fn's type-param list.

_Source: `src/compiler/sema_collect.cpp#L4656-L4666`_

### `item.fn.impl-trait-return-infer` — impl Trait return inferred from body

A function declared `-> impl Trait` resolves its return type to the single concrete type inferred from the body's return expressions; if none can be inferred it is an error.

```logos
fn f() -> impl Iterator { 0..3 }
```

_Source: `src/compiler/sema_decl.cpp#L1061-L1070`_

### `item.fn.name-underscore-reserved` — `_` reserved as a function name

A function declaration whose name is the single underscore `_` is ill-formed; `_` is reserved for ignored bindings (so `_(...)` cannot become a valid call expression).

```logos
fn _() {}  // error: '_' is reserved for ignored bindings
```

_Source: `src/compiler/sema_decl.cpp#L144-L146`_

### `item.fn.nested` — Nested function statement

A `fn name(params) [-> T] { ... }` at statement position is a nested function: its body is lifted to a top-level free function and the local name binds a fn-ptr value. A nested fn captures nothing; reads of enclosing locals are rejected (use a closure instead).

_Source: `tools/peg_gen/grammars/logos.peg#L1829-L1837`_

### `item.fn.never-fallback-precompute` — Body-diverges flag for `!` fallback

A fn whose body always diverges is flagged so that type-argument inference can apply the Rust-2024 `!`-fallback rule.

_Source: `src/compiler/sema_collect.cpp#L4673-L4679`_

### `item.fn.no-mangle` — #[no_mangle] keeps bare symbol name

`#[no_mangle]` on a function causes the bare base name to be used as the symbol name (no name mangling).

_Source: `src/compiler/sema_collect.cpp#L1753-L1764`_

### `item.fn.param-drop-epilogue` — By-value params dropped at function epilogue

By-value droppable (move-type) parameters are dropped at the function epilogue, equivalently to a `let` binding, when the body falls off the end without an explicit terminating return/break/continue. A parameter that was moved on any branch is conservatively skipped from the static epilogue drop (to avoid double-free on the move path).

```logos
fn consume(_x: Move) {}  // _x dropped at end
```

_Source: `src/compiler/sema_decl.cpp#L1083-L1117`_

### `item.fn.param-list-trailing-comma` — Parameter list trailing comma

A parameter list is `param (, param)* (,)?`, but a trailing comma is forbidden when immediately followed by `...` (the variadic marker), so `, ...` separators are unambiguous.

_Source: `tools/peg_gen/grammars/logos.peg#L1337-L1342`_

### `item.fn.param-pattern` — Pattern-binding parameters

A parameter may bind an irrefutable pattern: a tuple-destructure `(a, b, ...) : T`, a struct pattern `Name { f, .. } : T`, or a slice pattern `[h, t] : T`. Refutable patterns at the fn boundary are rejected in sema with the same diagnostic as for `let`.

```logos
fn f(Point { x, y }: Point) {}
```

_Source: `tools/peg_gen/grammars/logos.peg#L1356-L1393`_

### `item.fn.param-self-shorthand` — Self-receiver / ref-binding parameter shorthand

A parameter may be `&[mut] IDENT` (reference binding, type elided), `ref IDENT : T`, or `mut IDENT : T` (mutable local binding, mutability invisible to callers).

_Source: `tools/peg_gen/grammars/logos.peg#L1344-L1355`_

### `item.fn.param-variadic` — Variadic parameter

`IDENT : T ...` marks a variadic parameter (IS_VARIADIC); plain `IDENT : T` is the ordinary typed parameter.

**Divergence:** A6

_Source: `tools/peg_gen/grammars/logos.peg#L1379-L1382`_

### `item.fn.runtime-abi-no-mangle` — main, no_mangle, metacall thunks keep bare symbol

`main`, `#[no_mangle]` functions, and `__metacall_thunk_*` functions suppress package/signature mangling and keep their bare names as link symbols.

_Source: `src/compiler/sema_collect.cpp#L4858-L4868`_

### `item.fn.self-ref-param-type` — self-receiver parameter type

A `&self` / `&mut self` receiver parameter takes the type `&Self` / `&mut Self`, where `Self` is the in-scope Self type; mutability follows the `mut` marker.

_Source: `src/compiler/sema_collect.cpp#L4498-L4502`, `src/compiler/sema_collect.cpp#L4643-L4647`_

### `item.fn.signature-form` — function item signature form

A function is `[pub] [unsafe] [extern] fn NAME [<type-params>] (params) [-> RET_TYPE] BLOCK`, or terminated with `;` when bodyless (declaration only).

_Source: `src/compiler/sema_render.cpp#L1375-L1398`_

### `item.fn.signature-overloading` — Functions overloadable by signature

Functions are keyed by a signature derived from base name, parameter types, and vararg-ness, allowing multiple same-named functions to coexist; only an exact symbol-name collision (same package, base, signature) is a "duplicate function" error.

**Divergence:** Rust does not permit free-function overloading by signature.

_Source: `src/compiler/sema_collect.cpp#L4712-L4713`, `src/compiler/sema_collect.cpp#L4837-L4881`_

### `item.fn.tail-expr-is-return` — Tail expression is implicit return

Inside a fn body, a block's tail expression acts as an implicit return value (typed against the declared return type) for both lowering and reachability analysis.

_Source: `src/compiler/sema_collect.cpp#L4519-L4523`_

### `item.fn.tail-match-as-return` — Tail match arms are return values

When a non-void function's body ends with a `match` expression, each EXPR arm of that tail match is treated as the function's return value.

_Source: `src/compiler/sema_decl.cpp#L953-L972`_

### `item.fn.test-attributes` — Test attributes on functions

`#[test]`, `#[ignore]`, and `#[should_panic]` on a function tag it accordingly; `#[should_panic(expected = "...")]` records the expected panic substring (a quoted string literal).

_Source: `src/compiler/sema.cpp#L7791-L7836`_

### `item.fn.test-attrs` — test-harness function attributes

`#[test]`, `#[ignore]`, and `#[should_panic]` are recognised on a function; `#[should_panic(expected = "…")]` records the expected panic substring from its string-literal argument.

_Source: `src/compiler/sema_collect.cpp#L1767-L1797`_

## Function parameters

### `item.fn-param.datanode-by-value` — DataNode eidos cannot be passed by value

A parameter whose type is (or contains) a DataNode datatype (one holding relative-pointer fields) is rejected by value; it must be passed as `DataRef<T>` because the relative pointers require a zone base pointer.

**Divergence:** Logos addition (zoned/DataNode model); no Rust analog

_Source: `src/compiler/sema_decl.cpp#L700-L713`_

### `item.fn-param.mut-binding` — `mut` parameter binding

A typed parameter `mut x: T` makes `x` a mutable, caller-invisible local binding: the body may reassign or take `&mut` of it. Desugared to an immutable synthetic parameter plus prologue `let mut x = synth;` (a move of the param value into the user local).

```logos
fn f(mut x: i32) { x += 1; }
```

_Source: `src/compiler/sema_decl.cpp#L714-L741`, `src/compiler/sema_decl.cpp#L1046-L1060`_

### `item.fn-param.owning-box-dyn` — By-value Box<dyn Trait> param owns the box

A by-value parameter of owning trait-object type (`Box<dyn Trait>`) makes the callee own the box: it is dropped at the callee epilogue (vtable drop_in_place + dealloc) and call sites coerce the argument to a heap fat handle.

```logos
fn f(b: Box<dyn Trait>) {}
```

_Source: `src/compiler/sema_decl.cpp#L742-L759`_

### `item.fn-param.self-reserved` — `self` reserved for impl receivers

A parameter named `self` is an error outside an impl-block; `self` is only the magic receiver inside impl methods.

```logos
fn f(self: i32) {}  // error outside impl
```

_Source: `src/compiler/sema_decl.cpp#L687-L694`_

### `item.fn-param.struct-pattern` — Struct-pattern function parameter

A parameter may be an irrefutable struct pattern `Name { a, b, ... }: Name`. Each named field (or its `f: binding` rename) becomes a body-visible binding of the field's type; `..` rest is ignored. The pattern is desugared to a synthetic parameter plus prologue `let bind = synth.field;`.

```logos
fn f(Point { x, y }: Point) -> i32 { x + y }
```

_Source: `src/compiler/sema_decl.cpp#L604-L648`, `src/compiler/sema_decl.cpp#L999-L1045`_

### `item.fn-param.tuple-pattern` — Tuple-destructure function parameter

A parameter may be an irrefutable tuple pattern `(a, b, ...): (T1, T2, ...)`. Each non-`_` element name becomes a body-visible binding of the corresponding tuple element type, desugared to a synthetic parameter plus prologue `let a = synth.0;` etc.

```logos
fn f((a, b): (i32, i32)) -> i32 { a + b }
```

_Source: `src/compiler/sema_decl.cpp#L654-L684`, `src/compiler/sema_decl.cpp#L975-L994`_

### `item.fn-param.unique-names` — Parameter names must be unique

All parameter names within one function signature must be distinct.

_Source: `src/compiler/sema_decl.cpp#L765-L768`_

## Parameter inference

### `item.param.no-infer-placeholder` — `_` rejected in fn signature type positions

The inferred-type placeholder `_` is rejected (E0121) when it appears in a fn signature's parameter or return type positions.

_Source: `src/compiler/sema_collect.cpp#L4660-L4662`, `src/compiler/sema_collect.cpp#L4667-L4672`_

### `item.param.self-receiver-and-modifiers` — function parameter and self-receiver forms

A parameter is `[mut] NAME [: TYPE] [...]`; a self-receiver is rendered as `&[mut] self` (a reference parameter without an explicit type). The `...` suffix marks a variadic parameter.

_Source: `src/compiler/sema_render.cpp#L1101-L1125`_

## Extern functions

### `item.extern-fn.implicit-pub-unsafe` — extern fn is implicitly pub + unsafe

An `extern fn` declaration is implicitly public, unsafe, and extern.

_Source: `src/compiler/sema_collect.cpp#L4684-L4688`_

### `item.extern-fn.no-mangle-abi-symbol` — extern fn keeps its bare ABI symbol

An extern fn keeps its raw name as the link symbol (no package/signature mangling); duplicate extern declarations of the same name+signature across modules deduplicate to a single symbol rather than erroring.

_Source: `src/compiler/sema_collect.cpp#L4715-L4729`, `src/compiler/sema_collect.cpp#L4873-L4874`_

## Constants

### `item.const.def` — Module-level constant definition

A module constant is `[pub] (const|let) NAME [<params>] : T = expr ;`. The `const` keyword admits an optional type-parameter list, making the RHS a generic compile-time factory substituted at each use site; `let` stays non-generic. Both forms require an explicit type annotation and an initializer.

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

_Source: `tools/peg_gen/grammars/logos.peg#L688-L699`_

### `item.const.generic-and-typed` — const item with optional generics and type

A const item is `[pub] const NAME [<type-params>] [: TYPE] = VALUE ;`; const items may be generic.

**Divergence:** Generic const items (const with type parameters) are a Logos extension.

_Source: `src/compiler/sema_render.cpp#L1192-L1211`_

### `item.const.inlined-value` — const carries an inlined value

`const` definitions carry a name, type, and value expression; the value is inlined at each use site.

_Source: `src/compiler/sema.cpp#L7845-L7853`_

## Statics

### `item.static.def` — Module-level static definition

`[pub] static [mut] NAME : T = expr ;` defines a true global with stable storage and address (one global symbol; `&STATIC` identity holds), distinct from `const` inline substitution. The `mut` form (matched before the immutable form) marks mutable storage; without `mut`, reads are safe and writes are rejected.

```logos
static COUNTER: u64 = 0;
```
```logos
static mut FLAG: bool = false;
```

_Source: `tools/peg_gen/grammars/logos.peg#L705-L716`_

### `item.static.global-storage` — static defines global storage

`static` defines real global storage emitted as one global symbol per item (versus `const`, which is inlined at each use). `static mut` is mutable; a `static` without an initializer value is external. The symbol is module-qualified (falling back to `pkg$name`).

_Source: `src/compiler/sema.cpp#L7854-L7876`_

### `item.static.global-storage-and-mut-safety` — static items have global storage; mut access is unsafe

`static [mut] NAME: T = expr;` is a true global with a stable address and `&STATIC` identity. Reads and writes of a `static mut` require `unsafe`. A static with no initializer is an extern (external-linkage) declaration.

_Source: `tools/peg_gen/grammars/logos.peg#L322`_

### `item.static.link-symbol` — static link symbol qualification

A `static` with a value is registered with a module-qualified link symbol `<module_id>.<pkg>$<name>` (bare `<pkg>$<name>` when module_id empty) so two modules declaring the same `pkg::NAME` do not collide; an extern static (no value) links against the bare name.

_Source: `src/compiler/sema_collect.cpp#L1859-L1880`_

### `item.static.unsafe-access` — static mut and extern static require unsafe

A `static mut` is recorded as a mutable static (its reads/writes require `unsafe`); an extern static (declared with no value) is recorded as an extern static, every access of which requires `unsafe`.

_Source: `src/compiler/sema_collect.cpp#L1881-L1887`_

## Static methods

### `item.static-fn.def` — Static (associated) function definition

`[pub] static [unsafe] fn NAME [<params>] (params) [-> T] { ... }` defines an associated/free function with no `self` receiver; its own optional type-parameter list follows the name, matching instance/free fn generics. The name may be the `new` keyword.

```logos
static fn make<T>(x: T) -> Self { ... }
```
```logos
pub static fn new() -> Self { ... }
```

**Divergence:** `static fn` spelling for associated (no-self) functions; Rust uses an `fn` without a `self` parameter inside an impl.

_Source: `tools/peg_gen/grammars/logos.peg#L1067-L1093`_

## Type aliases

### `item.type-alias.def` — Type alias definition

`[pub] type NAME [<params>] = <type_ref> ;` introduces a type alias, optionally generic via a type-parameter list.

```logos
type Pair = (i32, i32);
```
```logos
pub type Map<K,V> = HashMap<K,V>;
```

_Source: `tools/peg_gen/grammars/logos.peg#L720-L727`_

### `item.type-alias.duplicate` — Type alias uniqueness per package

Two type aliases with the same name in the same package are an error. A same-name alias from a different package is permitted: the incumbent (first/other-package) keeps the bare name slot and the newcomer registers only under its package-qualified key `pkg::Name`. Lookup probes `cur_package_::name` first, so user code resolves to its own alias.

**Uncertainty:** Cross-package shadowing semantics inferred from the registration logic and comment.

_Source: `src/compiler/sema_collect.cpp#L2127-L2142`_

### `item.type-alias.generic` — type alias with optional generics

A type alias is `[pub] type NAME [<type-params>] = TYPE ;`.

_Source: `src/compiler/sema_render.cpp#L1213-L1224`_

### `item.type-alias.no-inferred-rhs` — Type alias RHS may not be the inferred placeholder

A type alias RHS is resolved in item-signature context; `type T = _;` is rejected (no inference context for item signatures). (Rust E0121)

_Source: `src/compiler/sema_collect.cpp#L2114-L2119`_

## Structs

### `item.struct.attr-flags` — structural struct attribute flags

Recognised structural struct attributes set per-struct flags: no_auto_drop, self_describing, rel_ptr, pinned, zone_mut, zoned (zoned2), borrow_carrying, non_null.

**Divergence:** Logos addition (zone/memory-model attributes).

_Source: `src/compiler/sema_collect.cpp#L1557-L1573`_

### `item.struct.explicit-inst` — Explicit struct instantiation declaration

`[pub[(vis)]] struct TYPE_REF ;` where TYPE_REF carries type arguments (e.g. `struct Foo<i64>;`) is an explicit-instantiation declaration binding annotations to a generic struct instantiation. The dedicated `instantiate Foo<T>;` form is preferred.

**Divergence:** A6: see B-item-92 — bare `struct Foo;` is the unit struct, generic form kept for the unbound-typevar diagnostic

_Source: `tools/peg_gen/grammars/logos.peg#L1133-L1138`_

### `item.struct.explicit-instantiation-needs-concrete-args` — Bodyless struct/datatype instantiation requires concrete type arguments

A bodyless `struct Foo<...>;` / `datatype Foo<...>;` (no NAME, only a generic-inst type) is an explicit instantiation: every type argument must be concrete. A `Foo<T>` with an unbound type variable is an error directing the user to write a generic definition with a body. A bare `struct Foo;` referring to an undefined name is an error.

_Source: `src/compiler/sema.cpp#L7541-L7620`, `src/compiler/sema.cpp#L7668-L7711`_

### `item.struct.field-name-unique` — Struct field names must be unique

Within a struct declaration, two fields may not share a name; a duplicate is a compile error.

_Source: `src/compiler/sema_decl.cpp#L1291-L1294`_

### `item.struct.fields-and-inherent-methods` — struct item form with optional inherent methods

A struct is `[pub] struct NAME [<type-params>] { fields... }`, or `[pub] struct NAME [<type-params>] ;` when field-less; each field is `[pub] NAME : TYPE [...]`. Inherent methods may be declared in the struct body, which is equivalent to a separate `impl NAME { ... }` block.

**Divergence:** Legacy `struct Foo { fields, fn ... }` form (methods inside the struct body) is accepted; not a Rust form.

_Source: `src/compiler/sema_render.cpp#L1140-L1150`, `src/compiler/sema_render.cpp#L1251-L1308`_

### `item.struct.generic-inline-method-self` — Inline methods of a generic struct bind Self to the generic self-type

For a generic struct `Struct<T...>`, methods declared in the struct body are lowered as if inside `impl<T...> Struct<T...>`: `Self` is bound to `Struct<T...>`, the struct's type params are recorded as the method's impl type-params, and the impl target pattern is `Struct<T...>` — so `-> Self` (and other Self uses) substitute correctly at monomorphization. Non-generic structs lower body methods with their own type params directly.

```logos
struct Pair<A,B>{a:A,b:B; fn make(a:A,b:B)->Self{Self{a,b}}}  // Pair::<i32,i32>::make(..) yields Pair<i32,i32>
```

_Source: `src/compiler/sema_decl.cpp#L1307-L1372`_

### `item.struct.generic-method-drops-struct-params` — Generic struct body methods keep only method-level type params

When lowering a body method of a generic struct, type parameters that coincide with the struct's own type parameters are removed from the method's TYPE_PARAMS (mono re-injects them via IMPL_TYPE_PARAMS); only method-introduced type parameters remain method-level.

_Source: `src/compiler/sema_decl.cpp#L1342-L1364`_

### `item.struct.inline-methods-self-binding` — Inline struct-body methods get Self + struct type-params in scope

Methods declared inline in a struct body are collected with `Self` bound to the struct's (possibly generic) self-type and the struct's type parameters installed as impl type-params, identically to `impl`-block methods. For a generic struct, `Self = Name<TVs...>` and generic methods are routed so static calls (`Pair::<i32,i32>::make()`) substitute the struct's params; for a non-generic struct `Self = Name`.

_Source: `src/compiler/sema_collect.cpp#L4084-L4128`_

### `item.struct.lifetime-param-unique` — Struct lifetime parameters must be uniquely named

Within a struct declaration, two lifetime parameters may not share a name; a duplicate is a compile error.

_Source: `src/compiler/sema_decl.cpp#L1271-L1273`_

### `item.struct.named-def` — Named-field struct definition

`[pub[(vis)]] struct IDENT [<type-params>] [where-clause] { field_def_or_doc* method_def_or_doc* }` defines a struct with named fields, optional generics, an optional where-clause, and optional inline method definitions.

```logos
pub struct S<T> where T: Clone { x: T, fn get(&self) -> &T { &self.x } }
```

_Source: `tools/peg_gen/grammars/logos.peg#L1149-L1150`, `tools/peg_gen/grammars/logos.peg#L1160-L1161`_

### `item.struct.repr-other-rejected` — non-transparent struct repr modes rejected

On a struct, `#[repr]` with no argument is an error, and any repr mode other than `transparent` (e.g. `C`, `packed`, `align(...)`) is parse-then-reject (not silently accepted).

_Source: `src/compiler/sema_collect.cpp#L1583-L1610`_

### `item.struct.repr-transparent` — #[repr(transparent)] requires single field

`#[repr(transparent)]` on a struct sets repr_transparent (the wrapper inherits its single field's layout) and requires the struct to have exactly one field, else it is rejected.

```logos
#[repr(transparent)] struct W(i32)
```

_Source: `src/compiler/sema_collect.cpp#L1581-L1604`_

### `item.struct.transparent-collapses-layout` — repr(transparent) collapses to the single field's layout

A struct annotated `#[repr(transparent)]` has the layout (size/alignment/ABI) of its single field.

**Uncertainty:** Single-field constraint is enforced elsewhere; this unit only propagates the flag.

_Source: `src/compiler/sema_decl.cpp#L1205-L1207`_

### `item.struct.tuple-def` — Tuple struct definition

`[pub[(vis)]] struct IDENT [<type-params>] ( tuple_field (, tuple_field)* ) ;` defines a tuple struct whose fields are types only; field names are synthesized as "0","1",… so `foo.0` and pattern `Foo(a,b)` work uniformly with named-field structs. Each tuple_field may carry its own `pub`.

```logos
pub struct Pair(pub i32, i32);
```

_Source: `tools/peg_gen/grammars/logos.peg#L1151-L1152`, `tools/peg_gen/grammars/logos.peg#L1174-L1180`_

### `item.struct.tuple-struct-fields` — Tuple-struct field shape and synthetic names

A struct whose first field definition carries no NAME is a tuple struct. Its positional fields are assigned synthetic decimal names "0", "1", … in declaration order, so member access (`foo.0`) and patterns (`Foo(a, b)`) reuse named-field machinery.

_Source: `src/compiler/sema_collect.cpp#L4007-L4015`, `src/compiler/sema_collect.cpp#L4035-L4048`_

### `item.struct.type-param-unique` — Struct type parameters must be uniquely named

Within a struct declaration, two type parameters may not share a name; a duplicate is a compile error.

_Source: `src/compiler/sema_decl.cpp#L1265-L1269`_

### `item.struct.unit-decl` — Unit struct declaration

`[pub] struct IDENT ;` declares a zero-field (unit) struct. A bare IDENT immediately followed by `;` is a unit struct; `struct Foo<...>;` (IDENT then `<`) is instead parsed as an explicit instantiation. This rule MUST be matched before struct_inst.

```logos
pub struct Foo;
```

_Source: `tools/peg_gen/grammars/logos.peg#L1120-L1131`_

### `item.struct.where-clause-named-only` — Where-clause only on IDENT-name struct alternatives

A struct/enum definition where-clause is accepted only on the IDENT-NAME alternatives, not on the antiquot (NAME_VAR / `#`-prefixed) alternatives, because WHERE and NAME_VAR share an AST slot.

**Uncertainty:** Slot-sharing is an implementation constraint surfaced as a grammar restriction.

_Source: `tools/peg_gen/grammars/logos.peg#L1140-L1150`_

### `item.struct.zoned-promotes-to-datatype` — #[zoned] promotes a struct to a zoned datatype

A `struct` carrying a `#[zoned]` attribute is lowered as a zoned datatype (IS_ZONED set) with annotation fields applied, equivalent to a `datatype`/`eidos` declaration.

**Divergence:** Logos addition: zoned/datatype types are not present in Rust.

_Source: `src/compiler/sema.cpp#L7625-L7660`_

## Fields

### `item.field.named` — Named field definition

A struct field is `[pub] IDENT : TYPE_REF [,]`. The contextual keywords `new` and `null` are also accepted as field names. A trailing comma is permitted.

_Source: `tools/peg_gen/grammars/logos.peg#L1191-L1202`_

### `item.field.repeat-group` — Repeat-group field (quote)

`#( field_def ),*` and `#( field_def )*` denote a repeat-group of field definitions (REPEAT_GROUP, OP=1 comma-separated / OP=0 plain), for use in quoted item bodies.

**Divergence:** A6

_Source: `tools/peg_gen/grammars/logos.peg#L1183-L1186`_

### `item.field.variadic` — Variadic field

A field of form `IDENT ... : TYPE_REF` marks a variadic field (IS_VARIADIC).

**Divergence:** A6

_Source: `tools/peg_gen/grammars/logos.peg#L1203-L1204`_

## Unions

### `item.union.collected-as-struct` — union shares struct collection shape

A `union NAME { … }` is collected with the same named-field/type-param shape as a struct and registered as a known type, with its `is_union` flag set.

```logos
union U { i: i32, f: f32 }
```

_Source: `src/compiler/sema_collect.cpp#L1457-L1469`_

### `item.union.def` — Union definition

`[pub[(vis)]] union IDENT [<type-params>] [where-clause] { field_def_or_doc* }` defines a union with named fields and optional generics. It is collected internally as a struct flagged `is_union`; no tuple shape, no methods.

```logos
union U { a: i32, b: f32 }
```

_Source: `tools/peg_gen/grammars/logos.peg#L1163-L1172`_

### `item.union.field-copy-restriction` — union field types restricted to non-move types

Each non-generic union field type must not be a move type (Vec/Box/String/owning trait object); allowed are Copy types, references, ManuallyDrop<T>, or aggregates thereof. A field whose type is a bare type-parameter is exempt at collection (checked at monomorphization); a field that is itself a union is allowed.

**Divergence:** B: generic-union Copy check is deferred to mono rather than enforced at use site as in Rust.

**Uncertainty:** Slice-1 uses is_move_type as the rejection oracle; full ManuallyDrop/tuple/array recursion is a follow-up.

_Source: `src/compiler/sema_collect.cpp#L1502-L1530`_

### `item.union.layout-and-unsafe-access` — Union layout and unsafe field access

A `union NAME { f: T, ... }` is a struct-shaped type whose size is max-of-fields aligned to max field alignment; reading or writing a union field requires an `unsafe` block.

_Source: `tools/peg_gen/grammars/logos.peg#L321`_

### `item.union.lowered-as-struct` — union lowered as struct

A `union` definition is lowered through the same path as a struct (identical field shape); layout/unsafe-gating is deferred.

**Uncertainty:** Slice-1 behavior; union soundness gating is a noted follow-up, so semantics may change.

_Source: `src/compiler/sema.cpp#L7524-L7537`_

### `item.union.no-empty` — fieldless union rejected

A union with zero fields is rejected; a union must declare at least one field.

```logos
union U {} // error
```

_Source: `src/compiler/sema_collect.cpp#L1474-L1480`_

### `item.union.shared-namespace` — unions share the struct/enum type namespace

Union definitions occupy the same type namespace as structs and enums; a union name conflicts with a struct/enum of the same name, and `type Alias = U;` resolves U as a type.

_Source: `src/compiler/sema_collect.cpp#L390-L413`_

## Enums

### `item.enum.def` — Enum definition

`[pub] enum NAME [<params>] [: backing_type] [where ...] { variants }` defines an enum, with optional generic params, an optional explicit backing integer type after `:`, and an optional where-clause. A metacall-named form `enum #(<expr>) ...` derives the enum name from a compile-time expression. Where-clauses are permitted only on IDENT-named (not expr-named) enums.

```logos
enum Color { Red, Green, Blue }
```
```logos
enum Tags : u64 { X = 0xdead }
```
```logos
pub enum Option<T> { Some(T), None }
```

_Source: `tools/peg_gen/grammars/logos.peg#L735-L751`_

### `item.enum.discriminant-const-expr` — enum discriminant from const expression

An enum discriminant may be given by a general const expression (e.g. `1 << 1`), evaluated via CTFE; a `metacall { <expr> }` discriminant must contain a single integer tail expression, evaluated via CTFE to the discriminant value.

```logos
enum E { A = 1 << 1, B = metacall { 4 } }
```

**Divergence:** A1: const-eval at discriminant position is via metacall/CTFE rather than miri.

_Source: `src/compiler/sema_collect.cpp#L1985-L2026`_

### `item.enum.discriminant-default` — implicit enum discriminant sequencing

An enum variant without an explicit discriminant takes the value 0 for the first such variant and previous+1 thereafter; an explicit value resets the running counter to value+1.

_Source: `src/compiler/sema_collect.cpp#L1926-L1942`, `src/compiler/sema_collect.cpp#L2097`_

### `item.enum.discriminant-fits` — enum discriminant must fit backing type

When an enum has a backing type, each variant's discriminant value must fit in that backing integer type, else it is rejected.

_Source: `src/compiler/sema_collect.cpp#L2028-L2032`_

### `item.enum.discriminant-from-other-enum` — enum discriminant referencing another enum's variant

An enum discriminant may be `OtherEnum::OtherVariant` (with optional `as T` cast dropped): the referent is resolved among already-collected enums and its discriminant value is used verbatim; an unknown enum or variant is rejected.

_Source: `src/compiler/sema_collect.cpp#L1943-L1984`_

### `item.enum.empty-legal` — empty enum body is legal

An enum with an empty body is legal (an uninhabited / marker type); no diagnostic is emitted.

```logos
enum Void {}
```

_Source: `src/compiler/sema_collect.cpp#L1900-L1902`_

### `item.enum.explicit-discriminant` — Enum variants carry an explicit/assigned discriminant and optional backing type

Each enum variant has an integer discriminant value; an enum may declare an explicit backing integer type for its discriminant.

_Source: `src/compiler/sema_decl.cpp#L1410`, `src/compiler/sema_decl.cpp#L1486-L1492`_

### `item.enum.repr-and-variants` — enum item form

An enum is `[pub] enum NAME [<type-params>] [: TYPE] { variant, ... }` where the optional `: TYPE` gives the discriminant representation type; each variant is `NAME [(types...)] [= [-]discriminant]`.

_Source: `src/compiler/sema_render.cpp#L1152-L1174`, `src/compiler/sema_render.cpp#L1226-L1249`_

### `item.enum.repr-int-width` — #[repr(uN/iN)] sets enum discriminant width

`#[repr(I)]` on an enum where I is an integer type (u8/u16/u32/u64/i8/i16/i32/i64/usize/isize) sets the enum's backing (discriminant) type; it conflicts with (errors against) an already-declared `enum Foo : I'` backing type when I≠I'. `#[repr(C)]` and other non-integer modes are parse-then-reject.

```logos
#[repr(u8)] enum E { A, B }
```

_Source: `src/compiler/sema_collect.cpp#L1698-L1744`_

### `item.enum.type-param-unique` — Enum type parameters must be uniquely named

Within an enum declaration, two type parameters may not share a name; a duplicate is a compile error.

_Source: `src/compiler/sema_decl.cpp#L1472-L1475`_

### `item.enum.variant-name-unique` — Enum variant names must be unique

Within an enum declaration, two variants may not share a name; a duplicate is a compile error.

_Source: `src/compiler/sema_decl.cpp#L1476-L1479`_

### `item.enum.variant-payload-shapes` — enum variant payload shapes

Enum variant payloads may be tuple-style (positional types), struct-shape (named fields, in declaration order, names must be unique), or variadic (single type ref); payload type positions are item signatures in which `_` is rejected (E0121).

```logos
enum E { Tup(i32, i32), Rec { x: i32 }, Var(i32) }
```

_Source: `src/compiler/sema_collect.cpp#L2033-L2091`_

### `item.enum.variant-shapes` — Enum variant shapes

A variant is one of: unit `Name`; tuple `Name(T, ...)`; variadic-tuple `Name(...T)`; struct-shape `Name { f: T, ... }` (fields may be `pub`); empty struct-shape `Name {}`; or a discriminant-bearing `Name = <disc>`. Variant lists allow leading doc-comments per variant and a trailing comma.

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

_Source: `tools/peg_gen/grammars/logos.peg#L753-L786`, `tools/peg_gen/grammars/logos.peg#L757-L775`_

### `item.enum.zoned-attr` — #[zoned]/#[borrow_carrying] on enum

`#[zoned]` on an enum sets its zoned2 flag (niche-enum Ref arm stored self-relative at rest, absolute as value); `#[borrow_carrying]` sets the borrow_carrying flag.

**Divergence:** Logos addition.

_Source: `src/compiler/sema_collect.cpp#L1681-L1692`_

## Writ datatypes

### `item.datatype.def` — Writ datatype definition

A datatype item is `[pub[(vis)]] eidos NAME [<type-params>] { field_def_or_doc* }`. It declares a Writ-fabric datatype with named/repeat-group fields; the optional generic parameter list and visibility marker are accepted.

```logos
pub eidos Point<T> { x: T, y: T }
```

**Divergence:** A6

_Source: `tools/peg_gen/grammars/logos.peg#L1096-L1100`_

### `item.datatype.explicit-inst` — Explicit datatype instantiation declaration

`[pub[(vis)]] eidos TYPE_REF ;` (no body) is an explicit-instantiation declaration that binds metadata annotations (e.g. `#[type_code=N]`) to a concrete generic instantiation, e.g. `#[type_code=42] datatype Array<i32>;`.

**Divergence:** A6

_Source: `tools/peg_gen/grammars/logos.peg#L1102-L1109`_

### `item.datatype.is-zoned` — datatype declarations are zoned

A `datatype`/`eidos` declaration is always lowered with the IS_ZONED flag set.

**Divergence:** Logos addition: datatype/eidos kind has no Rust analog.

_Source: `src/compiler/sema.cpp#L7757-L7763`_

### `item.datatype.type-code-register` — #[type_code=N] registers explicit type code

`#[type_code=N]` on a datatype registers N as the explicit type code for the datatype's fully-qualified name, making it resolvable by impl-collection in the same pass; `#[annotation]` flags the datatype as a user-annotation type.

**Divergence:** Logos addition (Writ datatype family).

_Source: `src/compiler/sema_collect.cpp#L1654-L1667`_

### `item.datatype.type-code-unique` — exclusive datatype annotations are unique

On a datatype, the exclusive annotations `#[type_code]` and `#[annotation]` may each appear at most once; a duplicate occurrence is rejected.

**Divergence:** Logos addition.

_Source: `src/compiler/sema_collect.cpp#L1641-L1652`_

## Genos specialization

### `item.genos.specialization-decl` — Bodyless genos specialization declaration

A bodyless trait-position decl `#[type_code=N] genos T<Args>;` (no NAME, TYPE is a generic-inst over a trait name) declares a genos specialization: it must carry type arguments, all of which must resolve, and the type_code is registered against the like-named eidos/struct's mangled and canonical names (including under the template's home package when it differs).

**Divergence:** Logos addition: genos/eidos has no Rust analog.

_Source: `src/compiler/sema.cpp#L7887-L7994`_

## Traits

### `item.trait.explicit-inst` — Explicit genos/trait specialization declaration

`[pub[(vis)]] <trait-kw> TYPE_REF ;` (no body) binds annotations to a logical-family (genos) specialization of a concrete trait instantiation; implementing eidos inherit the metadata via impl.

**Divergence:** A6

_Source: `tools/peg_gen/grammars/logos.peg#L1111-L1118`_

## Impl blocks

### `item.impl.items` — Impl item kinds

An impl item is a method definition, an associated-type impl `type NAME [<params>] = T ;`, or an associated-const impl `const NAME : T = expr ;`. Doc-comments may precede impl items.

```logos
type Item = i32;
```
```logos
const N: usize = 4;
```

_Source: `tools/peg_gen/grammars/logos.peg#L1054-L1060`, `tools/peg_gen/grammars/logos.peg#L567`_

### `item.impl.method-reattach-by-package` — Impl methods are attached to their target template only within the same package

An impl method (mangled `<Struct>__<method>__[fg]__<sig>`) whose `<Struct>` names a generic template is hosted on that template only when the template's package equals the method's package; a method with no package attaches to a sole same-named candidate. A cross-package bare-name collision (e.g. user `Rc` vs stdlib `Rc<T>`) does NOT cause adoption, so the method stays with its own struct's emission.

**Uncertainty:** This is an emission/hosting invariant observable as: same-named generics in distinct packages keep their own methods; surfaced as a language-level guarantee against method mis-hosting.

_Source: `src/compiler/sema.cpp#L7002-L7054`_

### `item.impl.negative` — Negative impl

`impl [<params>] !Trait for <target> [where ...] {}` declares a negative impl (the body must be empty), asserting that the target does not implement Trait.

```logos
impl !Send for Foo {}
```

_Source: `tools/peg_gen/grammars/logos.peg#L992-L1004`_

### `item.impl.target-mangling` — Impl self-type is mangled to a canonical target key by type shape

The impl target type is reduced to a canonical string key by shape: pointer/named struct → struct name (concrete generic instantiations use the monomorphized concrete name, generic/typevar instantiations keep the base name); `[T]` and `&[T]`/`&mut [T]` → `$slice$T` (typevar elem) or `$slice$<elem>` (concrete); `dyn Tr` → `$dyn$<Trait>`; `&U`/`&mut U` → `$ref_<U>`/`$mut_ref_<U>` (typevar pointee → `$ref$T`/`$mut_ref$T`); tuple `(...)` → `$tuple$N` (typevar elems) / `$tuple$N$<t1>$<t2>...` (concrete) / `$tuple$variadic` (variadic param); fn-pointer → `$fnptr$<arity>`; unit `()` → `void`. Collection and lowering use the same mangling so they agree.

_Source: `src/compiler/sema_decl.cpp#L1688-L1820`, `src/compiler/sema_decl.cpp#L1782-L1814`_

### `item.impl.targets` — Impl block forms and targets

`[unsafe] impl [<impl_params>] [Trait [<args>] for] <target> [where ...] { items }` defines an impl. Trait impls use `Trait for Target`; standalone (inherent) impls omit the trait. The target may be a simple type, pointer, reference, bare slice `[T]`, `dyn Trait`, tuple, or fn-pointer type. Each form admits an optional where-clause before the body.

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

_Source: `tools/peg_gen/grammars/logos.peg#L972-L1051`, `tools/peg_gen/grammars/logos.peg#L1021-L1030`_

### `item.impl.trait-and-inherent` — impl block forms

An impl block is `[unsafe] impl[<impl-type-params>] TRAIT[<type-args>] for TYPE { items }` (trait impl) or `[unsafe] impl[<type-params>] TYPE { items }` (inherent impl); negative impls are permitted.

_Source: `src/compiler/sema_render.cpp#L1310-L1373`_

### `item.impl.type-params-source` — Impl type parameters come from IMPL_TYPE_PARAMS or (inherent only) TYPE_PARAMS

An impl block's own generic parameters are taken from the generic-trait-impl form `impl<T> Trait for U<T>` (its dedicated parameter list). For an inherent impl `impl<T> U<T>` (no trait), the parameters are taken from the type-parameter list instead. These parameters are in scope throughout the impl's target type, trait args, and method signatures/bodies, and are recorded on each lowered method.

_Source: `src/compiler/sema_decl.cpp#L1674-L1683`, `src/compiler/sema_decl.cpp#L1866-L1869`_

## Extern blocks

### `item.extern.abi-whitelist` — extern ABI string whitelist

The ABI string of an `extern "ABI" { … }` block or an `extern "ABI" fn …` item must be one of "C", "C-unwind", "system", or "Rust" (enclosing quotes optional); any other string is rejected.

```logos
extern "C" { fn puts(s: *const u8) -> i32; }
```

**Divergence:** A7: "C-unwind" is accepted at parse but unwinding-across-FFI is moot (panic=abort).

_Source: `src/compiler/sema_collect.cpp#L1334-L1344`, `src/compiler/sema_collect.cpp#L1379-L1381`_

### `item.extern.block` — Extern block

`[unsafe] extern ["ABI"] { extern_block_item* }` groups same-ABI externs. The optional ABI string applies to all items in the block (inherited at splice). The Rust-2024 `unsafe extern` marker is accepted with no extra semantics.

```logos
unsafe extern "C" { fn puts(s: *const u8) -> i32; }
```

_Source: `tools/peg_gen/grammars/logos.peg#L1209-L1227`_

### `item.extern.block-flatten` — extern block flattening and ABI inheritance

An `extern "ABI" { extern_fn* }` block flattens to a linear item worklist; each child extern-fn that does not carry its own ABI inherits the block's ABI string, and item collection treats grouped and flat extern fns identically.

_Source: `src/compiler/sema_collect.cpp#L1346-L1383`_

### `item.extern.block-item` — Extern block item (fn / static)

Inside an extern block, items use bare `fn IDENT(params [, ...]) [-> T] ;` (no `extern` keyword; trailing `, ...` makes it variadic) or `static [mut] IDENT : T ;`. The produced extern fn carries no ABI of its own; an extern static with no value is marked external (no initializer).

_Source: `tools/peg_gen/grammars/logos.peg#L1228-L1243`_

### `item.extern.fn-def` — Standalone extern fn declaration

`extern ["ABI"] fn IDENT(params [, ...]) [-> T] ;` declares a single FFI function carrying its ABI string verbatim. A trailing `, ...` makes it variadic. Omitting the ABI string selects the default (Logos-internal) calling convention.

_Source: `tools/peg_gen/grammars/logos.peg#L1209-L1216`, `tools/peg_gen/grammars/logos.peg#L1244-L1255`_

## Extern blocks (ABI)

### `item.extern-block.abi-default-to-children` — extern block applies its ABI as a default

`extern "ABI" { extern_fn* }` splices its function items into the module item stream; the block ABI applies as a default to children that do not specify their own ABI. An omitted block ABI is the default Logos-internal ABI.

_Source: `tools/peg_gen/grammars/logos.peg#L317`_

## Module items

### `item.module.extern-block-flatten` — extern block children flattened into the item stream

An `extern { ... }` block is not itself an item; its child extern-fn declarations are spliced inline, in order, into the module's item worklist for lowering.

_Source: `src/compiler/sema.cpp#L7378-L7391`_

## Instantiation

### `item.instantiate.generic-only` — instantiate declaration requires a generic target with concrete args

`instantiate T;` / `pub instantiate T;` pins a monomorphization root. The target must resolve to a struct, datatype, or enum; `instantiate T;` on a non-generic type (no type arguments) is an error. `pub` marks the pin for library re-export.

**Divergence:** Logos addition: explicit `instantiate` (C++ `template class Foo<int>;` analog).

_Source: `src/compiler/sema.cpp#L7440-L7496`_

## Duplicate definitions

### `item.dup.odr-dedup` — structurally identical duplicate items dedup; differing ones error

Two item definitions (struct/union/datatype/enum) sharing the same name in the same package are an error UNLESS their AST sub-trees are structurally equal, in which case the duplicate is silently dropped (ODR-style dedup). Structural equality ignores source-line metadata, so identical items emitted by metaprogramming at different source positions still dedup.

**Divergence:** Logos addition: ODR dedup of metacall-emitted items (Rust has no metacall splice model).

_Source: `src/compiler/sema_collect.cpp#L25-L75`, `src/compiler/sema_collect.cpp#L267-L282`, `src/compiler/sema_collect.cpp#L378-L446`_

## Name resolution

### `item.name.forward-reference` — item names are visible before their definition (forward references)

Type names (struct, union, datatype, enum) and trait names are registered in a name-collection pass before bodies are collected, so an item may reference a type or trait declared later in the same or another module, and cross-file `impl Trait for X` resolves regardless of file order.

_Source: `src/compiler/sema_collect.cpp#L313-L478`_

## Where clauses

### `item.where.clause` — Where clause

`where where_pred (, where_pred)*`. A predicate is `<subject> : trait_bound (+ trait_bound)*` where subject is an associated-type ref, a reference type (`&T`, incl. `for<'a> &'a T`), or a plain type-param; or it is a bare type_param.

```logos
where T: Clone + Send, &T: Into<U>
```

_Source: `tools/peg_gen/grammars/logos.peg#L1257-L1271`_

## Use rendering

### `item.use.path-form` — use declaration path form

A use declaration is `[pub] use NAME(.part)* ;`, where path segments after the head are dot-separated.

**Divergence:** Logos paths use `.` for package/module segments rather than Rust's `::`.

_Source: `src/compiler/sema_render.cpp#L1036-L1050`, `src/compiler/sema_render.cpp#L1182-L1190`_

## Domain: module

## Package declaration (module)

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

_Source: `tools/peg_gen/grammars/logos.peg#L489-L490`_

## Path resolution (module)

### `module.path.package-name` — Package name from NAME plus dotted path parts

A module's fully-qualified package name is its NAME, followed by each PATH_PARTS entry's NAME joined with '.' in order (e.g. NAME='c' under parts ['a','b'] => 'a.b.c'-style dotted name). A module with no NAME has the empty package name.

_Source: `src/compiler/sema_collect.cpp#L731-L744`_

### `module.path.qualified-call` — Package-qualified call constrains free-fn resolution to that package

A call `pkg.path::fn(args)` carries a dotted package qualifier (RECEIVER + QUAL_PARTS joined by `.`); free-function resolution for that call is restricted to the named package.

**Divergence:** A9: packages are `.`-separated, items reached via `::`.

_Source: `src/compiler/sema_expr.cpp#L2727-L2757`_

### `module.path.qualified-member-fallback` — Qualified call with no matching free fn is a type-member call

If a qualified `pkg.path.Member(args)` has no free function of that name in the package, the last dotted segment is interpreted as a type and the call is resolved as a static/associated method call `pkg.path.Type::method(...)`. A matching free function takes precedence.

_Source: `src/compiler/sema_expr.cpp#L2772-L2781`_

## Use imports (module)

### `module.use.brace-group-desugar` — `use pkg.{a, b, c}` with a lowercase head desugars to N wildcard imports

A grouped use whose head segment begins with a lowercase letter is treated as a package path: `use pkg.{a, b, c}` desugars to wildcard imports `pkg.a.*`, `pkg.b.*`, `pkg.c.*`. A head segment beginning uppercase is instead the enum-variant import form.

**Divergence:** note — Logos path model uses `.` for packages, `::` for items.

_Source: `src/compiler/sema.cpp#L6835-L6861`_

### `module.use.brace-group-import` — brace-group use desugars to per-item wildcard imports

`use pkg.{a, b, c};` (lowercase group head) desugars to wildcard imports `pkg.<head>.a`, `pkg.<head>.b`, `pkg.<head>.c` — bringing each listed package/item into wildcard scope. Distinguished from the enum-variant form by the lowercase first letter of the group head.

_Source: `src/compiler/sema_collect.cpp#L115-L143`_

### `module.use.duplicate-warn` — repeated use of same package warns

A `use pkg;` whose package is already in the module's wildcard import scope is a warning (duplicate import); it is otherwise a no-op.

_Source: `src/compiler/sema_collect.cpp#L176-L183`_

### `module.use.enum-variant-alias` — use of enum variants brings bare variant names into scope

`use pkg.Path.Type.{V1, V2, …};` (capitalised Type) registers each Vi as a bare-name alias resolvable unqualified, AND brings the enum type itself into scope (so both `Type::Vi` and bare `Vi` resolve). The bare variant resolves against any in-scope enum that declares it.

```logos
use std.lang.ord.Ordering.{Less, Equal, Greater};
```

_Source: `src/compiler/sema_collect.cpp#L90-L174`_

### `module.use.from-module` — use with explicit source module

_This id is described by 2 source layers; both statements are preserved._

- `[pub] use pkg('.'IDENT)* IDENT use_module ';'` imports `pkg.path` from a named module; the trailing bare IDENT is the contextual `from` keyword and `use_module` is the source (a bare name or a quoted string for hyphenated ids, with quotes stripped). The from-bearing alternative is tried before the plain form.
- `use pkg from <module>;` restricts the candidates of `pkg` to the named module. The `from` keyword is contextual (matched as a bare identifier); a missing/incorrect `from` keyword or a module name matching no loaded module is an error.

```logos
use foo.Bar from "logos-lang";
```
```logos
pub use a.b.C from othermod;
```

**Divergence:** `use ... from <module>` clause has no Rust analog.

**Divergence:** Logos addition: per-import module qualification (no Rust equivalent).

_Source: `tools/peg_gen/grammars/logos.peg#L498-L521`, `src/compiler/sema_collect.cpp#L192-L225`_

### `module.use.from-module-restriction` — `use pkg from "module"` restricts the import to a specific module id

A use of the form `use pkg from "module";` resolves the quoted module name to a module id and restricts the imported package's symbol resolution to that module's exports; the restriction is in force during lowering, not only collection.

**Divergence:** note — part of Logos's C++-style module-linkage system; no direct Rust equivalent.

_Source: `src/compiler/sema.cpp#L6882-L6905`_

### `module.use.path` — Plain use declaration

`[pub] use pkg('.'IDENT)* ';'` brings a dotted package path into scope. `pub use` re-exports it. Path components after the head use a leading-dot separator (`.IDENT`).

```logos
use std.collections.HashMap;
```
```logos
pub use core.Option;
```

_Source: `tools/peg_gen/grammars/logos.peg#L500-L516`, `tools/peg_gen/grammars/logos.peg#L526-L527`_

### `module.use.pub-reexport` — pub use re-exports a package

`pub use pkg;` registers pkg as a re-export from the current package, making it visible to importers of the current package.

_Source: `src/compiler/sema_collect.cpp#L226-L235`_

### `module.use.self-import-noop` — self-import is a no-op

`use P;` where P is the current module's own package is a no-op (own-package symbols always resolve first) and produces a redundancy warning.

_Source: `src/compiler/sema_collect.cpp#L184-L190`_

### `module.use.variant-alias` — `use Enum::{V, W}` brings bare variant names into scope aliased to their enum

An enum-variant use form records each listed bare variant name as an alias to its qualifying enum type, so the variant may be referred to unqualified within the module.

_Source: `src/compiler/sema.cpp#L6862-L6881`_

### `module.use.variant-shorthand` — Enum-variant bare-name import

`use pkg.Path.Type.{V1, V2, ...} ;` brings the named variants of enum `Type` into bare (unqualified) scope. The last dotted component before `.{...}` is the enum type name; the brace-list (trailing comma allowed) names the variants.

```logos
use core.Option.{Some, None};
```

**Divergence:** Uses `.`-separated path with `.{}` variant group; Rust spells this `use core::Option::{Some, None};` (A: `::`-item / `.`-pkg path model).

_Source: `tools/peg_gen/grammars/logos.peg#L506-L511`, `tools/peg_gen/grammars/logos.peg#L523-L527`_

### `module.use.variant-shorthand-vs-subpackage` — use {..} disambiguated by first-character case

In `use pkg.Path.X.{V1, V2, ...};` the last dotted segment `X` disambiguates by its first character's case: uppercase ⇒ enum-variant bare-name shorthand import; lowercase ⇒ grouped sub-package import.

_Source: `tools/peg_gen/grammars/logos.peg#L309`_

## Prelude (module)

### `module.prelude.implicit-injection` — implicit prelude is injected unless opted out

_This id is described by 2 source layers; both statements are preserved._

- Each non-prelude module implicitly imports the manifest-declared prelude package into wildcard scope, unless the file contains an inner attribute `#![no_implicit_prelude]`. The prelude is not re-injected into the prelude package itself, and is deduplicated against an explicit `use` of the same package.
- Each source-side module (not binary-archive ASTs) implicitly gains a wildcard import of the configured prelude package, unless the module is the prelude itself or already imports it. A module opts out with the inner annotation `#![no_implicit_prelude]`.

```logos
#![no_implicit_prelude]
```

**Divergence:** A6/note — prelude package is Logos's package-model analogue of Rust's std prelude.

_Source: `src/compiler/sema_collect.cpp#L240-L266`, `src/compiler/sema.cpp#L6911-L6936`_

## Name lookup (module)

### `module.lookup.unqualified-name-scope` — Unqualified type name lookup scope

An unqualified type name is known if it matches a primitive, an in-scope type param, or — for structs/datatypes/enums/aliases — a binding keyed unqualified, or in the current package, or in any wildcard-imported package.

_Source: `src/compiler/sema_collect.cpp#L4181-L4204`_

## Visibility (module)

### `module.vis.struct-pub-and-module-only` — Struct/datatype visibility flags

A struct, datatype, or struct field is public iff marked `pub`; a struct/datatype may additionally be `pub(module)` (module-only visibility). Field publicness is read per-field from the IS_PUB flag.

_Source: `src/compiler/sema_collect.cpp#L3849-L3851`, `src/compiler/sema_collect.cpp#L3990-L3994`_

## Visibility and linkage (module)

### `module.visibility.private-cross-package` — Non-pub items are package-private

An item without `pub` declared in package P is inaccessible from any package other than P; access is an error 'is private to package'.

_Source: `src/compiler/sema_collect.cpp#L760-L761`_

### `module.visibility.pub-module-linkage` — pub(module) has module-linkage visibility

A `pub(module)` item (which also sets is_pub) is visible to other packages only when the accessing code belongs to the SAME owning module-id as the item's defining module; cross-module access is an error 'is module-private'. This module-linkage check precedes the plain is_pub check.

_Source: `src/compiler/sema_collect.cpp#L751-L759`_

### `module.visibility.same-package` — Items are always visible within their own package

Access to an item declared in package P from code whose current package is also P is always permitted, regardless of pub/pub(module). Visibility is unchecked when either the defining package or the accessing package context is empty (no scope context).

_Source: `src/compiler/sema_collect.cpp#L746-L750`_

## Cross-module coexistence (module)

### `module.coexist.type-module-qualification` — Same pkg::Type from two modules coexist via module-id suffix

Every type-keyed symbol embeds the type's owning module_id as `$M<id>` so two separately-compiled modules each declaring the same pkg::Type do not collide at link. stdlib packages (`logos.*`) are globally unique and never qualified; an empty pkg→module map yields no suffix (byte-identical output for non-module compiles).

**Divergence:** A6: full type-coexistence is a Logos module-model addition.

_Source: `src/compiler/sema.cpp#L1373-L1415`_

## Symbol naming (module)

### `module.symbol.function-symbol-name` — Function link symbol embeds module/package; extern and methods carve-outs

A function's link symbol is mangled from {module_id, package, base, signature, is_generic, is_method, is_extern}; two packages defining the same base+signature fn get distinct symbols. Extern fns keep their bare ABI C name; struct methods (base contains `__`) are disambiguated via their struct's pkg-qualified name.

_Source: `src/compiler/sema.cpp#L1543-L1568`_

## Module attributes (module)

### `module.attr.inner-vs-item-attribute` — Inner attribute applies at file/module level

`#![name]` / `#![name(args)]` / `#![name=val]` is a file/module-level inner attribute, distinct from per-item `#[...]` attributes.

_Source: `tools/peg_gen/grammars/logos.peg#L310`_
