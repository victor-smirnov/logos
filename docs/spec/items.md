# Items

Module-level item declarations — kinds, syntax, and semantic rules — as extracted from the grammar (PEG), sema, mono, and MLIR-lowering layers (`tools/spec-extract/rules/`, domain `item`).

## Item kinds

### `item.kinds.set` — Module item alternatives

A module item is one of: doc-comment, annotation, template decl, const/static def, type alias, enum def, datatype def/inst, trait def/inst, struct unit/def/inst, instantiate decl, item-position metacall, fn-macro item invocation, union def, impl block, extern block, extern fn, or fn def — each in plain and `pub` forms where visibility applies.

*Source: `tools/peg_gen/grammars/logos.peg#L529` — testability: transitive*

## Visibility

### `item.visibility.pub-module` — Visibility marker pub / pub(module)

Item visibility is `pub` (fully exported) or `pub(IDENT)` where IDENT is a contextual keyword validated == "module" in sema, meaning module-linkage: visible to other packages of the SAME module but not exported to consumers.

```logos
pub(module) fn helper() {}
```

**Divergence (from Rust):** Logos uses `pub(module)` for module-linkage; Rust uses `pub(crate)`/path-restricted visibilities.

*Source: `tools/peg_gen/grammars/logos.peg#L1273-L1284`*

## Names and namespaces

### `item.name.forward-reference` — item names are visible before their definition (forward references)

Type names (struct, union, schema, datatype, enum) across every compiled module, and trait names, are registered in name-collection passes before any item body is collected, so an item may reference a type or trait declared later in the same or another module, and cross-file `impl Trait for X` resolves regardless of file order.

*Source: `src/compiler/sema_collect.cpp#L284-L500`*

### `item.names.duplicate-in-container` — Duplicate named member is an error

Within a named-member list of a container, any non-empty name that appears more than once is a duplicate error (`duplicate <kind> '<name>' in <container>`). The anonymous binding name `_` (and empty names) may repeat freely.

```logos
struct S { x: i32, x: i32 } // error: duplicate field 'x'
```

*Source: `src/compiler/sema_impl.hpp#L1312-L1325` — testability: untestable*

## `use` declarations

### `item.use.path-form` — use declaration path form

A use declaration is `[pub] use NAME(.part)* ;`, where path segments after the head are dot-separated.

**Divergence (from Rust):** Logos paths use `.` for package/module segments rather than Rust's `::`.

*Source: `src/compiler/sema_render.cpp#L1036-L1050` · `src/compiler/sema_render.cpp#L1182-L1190`*

## Modules and the item stream

### `item.module.extern-block-flatten` — extern block children flattened into item stream

An extern block's child items (extern fn declarations) are spliced in order into the flat module-item worklist; the block itself produces no item.

*Source: `src/compiler/sema.cpp#L7392-L7405` — testability: untestable*

## Documentation comments

### `item.doc.comment-attached-to-next-item` — Doc comments attach as documentation

Outer doc comments (`///`, `/** */`) accumulate and attach to the next item; inner doc comments (`//!`, `/*! */`) accumulate into the module-level inner documentation. The comment markers are stripped.

*Source: `tools/peg_gen/grammars/logos.peg#L305-L308` — testability: untestable*

### `item.doc.outer-line` — Outer line doc-comment binds to next item

An outer line doc-comment (`///`, captured as DOC_LINE) is an item-stream element; consecutive outer doc-comments accumulate and attach to the next real item.

*Source: `tools/peg_gen/grammars/logos.peg#L536-L537` — testability: untestable*

### `item.doc.outer-block` — Outer block doc-comment

An outer block doc-comment `/** ... */` is an item/member-stream element with the same next-item binding role as line doc-comments; the `/**` envelope and per-line leading `*` are stripped and lines joined with newline.

*Source: `tools/peg_gen/grammars/logos.peg#L544-L549` — testability: untestable*

### `item.doc.inner-module` — Inner doc-comments form the enclosing module's doc summary

An inner doc-comment — a `//!` line (with the leading `//! ` stripped) or a `/*! ... */` block (with the `/*!`/`*/` envelope and per-line `*` indent stripped) — accumulates, in source order joined by newlines, into the enclosing module's inner-doc summary and is never attached to any specific item. The buffer is committed as the module's inner documentation after all items are processed.

*Source: `tools/peg_gen/grammars/logos.peg#L541-L554` · `src/compiler/sema.cpp#L7412-L7428` · `src/compiler/sema.cpp#L8022-L8030` — testability: untestable*

### `item.doc.outer-line-block` — Outer doc-comments (/// and /**) attach to next item

`///` line doc-comments (with leading '/// ' stripped, joined by newline) and `/** ... */` outer block doc-comments accumulate into the pending doc buffer and become the DOC of the next item.

*Source: `src/compiler/sema.cpp#L7428-L7440` — testability: untestable*

### `item.doc.comment-strip` — doc comment accumulation and prefix stripping

`///` line docs strip the leading `///` plus one optional space and accumulate into a pending per-item doc buffer; `/** … */` outer block docs strip a 3-character prefix. `//!`/`/*! … */` inner docs accumulate separately into a per-module inner-doc buffer (joined by newlines) that never attaches to a specific item. Both collection phases accumulate identically, and any item that doesn't consume the pending doc buffer does not leak it to the next item.

*Source: `src/compiler/sema_collect.cpp#L1425-L1457` · `src/compiler/sema_collect.cpp#L1930-L1932` — testability: untestable*

## Attribute syntax

### `item.annotation.forms` — Outer attribute forms

An attribute is `#[ NAME (args) ]`, `#[ NAME = val ]`, or `#[ NAME ]`. The `= val` form admits an enum literal `IDENT::IDENT` or an integer.

```logos
#[derive(Debug)]
#[repr = 8]
#[inline]
```

*Source: `tools/peg_gen/grammars/logos.peg#L619-L641` — testability: untestable*

### `item.annotation.inner-attribute` — Inner attribute attaches to enclosing module

An inner attribute `#![ ... ]` (with the same `(args)` / `= val` / flag payload shapes as an outer attribute) attaches to the enclosing module rather than the following item. (Currently only `#![no_implicit_prelude]`.)

```logos
#![no_implicit_prelude]
```

*Source: `tools/peg_gen/grammars/logos.peg#L631-L636` — testability: untestable*

### `item.annotation.arg-forms` — Attribute argument forms

Within an attribute argument list, an argument is one of: `IDENT(args)` (nested call), `IDENT = lit` (key-value), a bare literal (positional), or a bare IDENT (legacy). A literal may be an enum ref `IDENT::IDENT`, raw/normal string, float, integer, `true`/`false`, or a bracketed array of literals. Lists allow a trailing comma.

```logos
#[cfg(target = "x86")]
#[align(8)]
#[list([1, 2, 3])]
```

*Source: `tools/peg_gen/grammars/logos.peg#L648-L672` — testability: untestable*

### `item.annotation.attribute-forms` — annotation/attribute syntax

An annotation is `#[NAME]`, `#[NAME = literal]`, or `#[NAME(args...)]`; arguments may be positional or `key = value`, and an argument value may be an array literal `[ ... ]`.

*Source: `src/compiler/sema_render.cpp#L1400-L1460`*

## Attribute semantics

### `item.attr.datatype-promotion` — #[datatype]/#[annotation] promote a struct into the datatype pipeline

A struct-syntax item annotated `#[datatype]` or `#[annotation]` is treated as a datatype declaration; `#[zoned]` marks self-relative fields and does NOT promote a struct to a datatype.

**Divergence (from Rust):** Logos addition: datatype/annotation/zoned attributes (no Rust equivalent).

*Source: `src/compiler/sema_collect.cpp#L367-L373` · `src/compiler/sema_collect.cpp#L374` · `src/compiler/sema_collect.cpp#L435` — testability: transitive*

### `item.attr.unknown-warn` — unknown attribute is warned

A top-level user `#[name]` attribute that is neither a builtin attribute target, a registered metaprog-handler trigger, nor the name of an `#[annotation]` datatype is a warning (likely typo, missing import, or removed handler). Evaluated only after all modules are collected, so the handler/annotation-type registries are complete.

*Source: `src/compiler/sema_collect.cpp#L628-L686` — testability: untestable*

### `item.attr.target-kind-validity` — Built-in attributes restricted to declared item kinds

Each compiler-recognised attribute is valid only on a fixed set of item kinds: `type_code`→{struct,datatype,enum,trait}; `zoned`→{struct,enum}; `datatype`→{struct}; `self_describing`/`rel_ptr`/`pinned`/`zone_mut`/`no_auto_drop`/`non_null`→{struct}; `borrow_carrying`→{struct,enum}; `annotation`→{struct,datatype}; `tag_dispatch`→{trait}; `metaprog_handler`/`no_mangle`/`fn_macro`/`token_macro`/`test`/`should_panic`/`ignore`→{fn}; `cfg`/`cfg_attr`→{all item kinds}; `repr`→{struct,enum}. Applying a built-in attribute to a disallowed kind is an error; an unrecognised name is treated as a user `#[annotation]` lookup.

```logos
#[zoned] enum E {}  // ok
#[datatype] enum E {} // error (struct only)
```

*Source: `src/compiler/sema_impl.hpp#L1462-L1507`*

### `item.attr.struct-enum-flag-set` — Struct/enum attribute flag vocabulary

The recognised struct/enum modifier attributes are exactly: `datatype`, `annotation`, `zoned`, `zone_mut`, `rel_ptr`, `self_describing`, `pinned`, `borrow_carrying`, `no_auto_drop`, `non_null`. A struct bearing `#[datatype]` or `#[annotation]` is promoted to the datatype pipeline.

**Divergence (from Rust):** Logos-specific memory/zone attribute set; no Rust analogue.

*Source: `src/compiler/sema_impl.hpp#L1430-L1460` — testability: transitive*

## Representation attributes

### `item.repr.recognized-modes` — `#[repr(...)]` minimal recognised modes

`#[repr(...)]` is recognised only on structs (`transparent`) and enums (integer-discriminant width). Other repr modes are parsed and then rejected (no silent acceptance).

**Divergence (from Rust):** Only `transparent` (struct) and integer-width (enum) repr supported; Rust's `C`/`packed`/`align`/etc. not yet.

*Source: `src/compiler/sema_impl.hpp#L1501-L1505`*

## Conditional compilation (`cfg`)

### `item.cfg.conditional-compilation` — cfg attributes gate item lowering

An item whose pending cfg attributes evaluate false (cfg_attrs_drop_item) is dropped before lowering; its pending annotations and doc are consumed and discarded.

```logos
#[cfg(unix)] fn f() {}
```

*Source: `src/compiler/sema.cpp#L7459-L7468`*

### `item.cfg.drop-disabled` — cfg-disabled items are dropped

Before collecting an item, `cfg_attr(pred, cfg(...))` activation is folded into the drop-predicate set first, then if any accumulated `#[cfg(...)]` predicate on the item is false, the item is dropped entirely — neither collected nor lowered — together with its pending annotations. This gate is applied uniformly across both collection phases (type/fn/trait/impl items, and const/static/type-alias items), so `#[cfg(...)]`-gated consts/statics are honored the same as gated types.

*Source: `src/compiler/sema_collect.cpp#L1416-L1424` · `src/compiler/sema_collect.cpp#L1458-L1467`*

### `item.cfg.gate-before-registration` — cfg-false items do not register their name

A `#[cfg(...)]` predicate is evaluated before name registration; an item whose cfg is false registers no name. This permits the same-name-under-mutually-exclusive-cfg idiom (e.g. cfg(unix)/cfg(windows) structs) without a duplicate-name error.

*Source: `src/compiler/sema_collect.cpp#L359-L366`*

## Functions

### `item.fn.def` — Function definition

A function item is `[pub[(vis)]] [unsafe] fn NAME [<type-params>] ( [param_list] ) [-> T] [where-clause] block`. NAME may be IDENT or the contextual keywords `new`/`null`. The where-clause and return type are optional.

```logos
pub unsafe fn f<T>(x: T) -> T where T: Copy { x }
```

*Source: `tools/peg_gen/grammars/logos.peg#L1286-L1335`*

### `item.fn.signature-form` — function item signature form

A function is `[pub] [unsafe] [extern] fn NAME [<type-params>] (params) [-> RET_TYPE] BLOCK`, or terminated with `;` when bodyless (declaration only).

*Source: `src/compiler/sema_render.cpp#L1375-L1398`*

### `item.fn.vararg-extern-only` — Variadic functions are extern-only C-ABI declarations

A function declared variadic (vararg) is emitted only as an external declaration through the LLVM-dialect function op (`llvm.func`, `isVarArg=true`, external linkage) — the `func` dialect used for ordinary Logos functions has no vararg support — and is a declaration only in this path, with no Logos-level body generated for it.

**Uncertainty:** This unit shows only the vararg lowering path (llvm.func extern-style declaration, no body); whether a non-extern fn with a vararg parameter is rejected elsewhere (sema) is not visible in this slice.

*Source: `src/compiler/mlir_gen_fn.cpp#L243-L259` — testability: behavioral*

### `item.fn.unique-mangled-name` — Each mangled function symbol must have at most one body

Two distinct functions resolving to the same mangled link symbol is an error; in particular a private function in one package and a pub function of the same base name in an imported package must not collide, requiring rename to disambiguate.

**Related:** `module.symbol.method-link-prefix`

*Source: `src/compiler/mlir_gen_fn.cpp#L294-L310` — testability: untestable*

### `item.fn.nested` — Nested function statement

A `fn name(params) [-> T] { ... }` at statement position is a nested function: its body is lifted to a top-level free function and the local name binds a fn-ptr value. A nested fn captures nothing; reads of enclosing locals are rejected (use a closure instead).

*Source: `tools/peg_gen/grammars/logos.peg#L1829-L1837` — testability: untestable*

### `item.fn.antiquot-name` — Function with antiquoted name

`[pub] [unsafe] fn #(expr) [<type-params>] ( [params] ) [-> T] block` carries an expr-TOM name (NAME_VAR), valid only inside a quote body; these alts omit the where-clause because NAME_VAR and WHERE share a slot.

**Divergence (from Rust):** A6

*Source: `tools/peg_gen/grammars/logos.peg#L1286-L1293` · `tools/peg_gen/grammars/logos.peg#L1312-L1319` — testability: untestable*

### `item.fn.param-list-trailing-comma` — Parameter list trailing comma

A parameter list is `param (, param)* (,)?`, but a trailing comma is forbidden when immediately followed by `...` (the variadic marker), so `, ...` separators are unambiguous.

*Source: `tools/peg_gen/grammars/logos.peg#L1337-L1342` — testability: untestable*

### `item.fn.param-self-shorthand` — Self-receiver / ref-binding parameter shorthand

A parameter may be `&[mut] IDENT` (reference binding, type elided), `ref IDENT : T`, or `mut IDENT : T` (mutable local binding, mutability invisible to callers).

*Source: `tools/peg_gen/grammars/logos.peg#L1344-L1355`*

### `item.fn.param-pattern` — Pattern-binding parameters

A parameter may bind an irrefutable pattern: a tuple-destructure `(a, b, ...) : T`, a struct pattern `Name { f, .. } : T`, or a slice pattern `[h, t] : T`. Refutable patterns at the fn boundary are rejected in sema with the same diagnostic as for `let`.

```logos
fn f(Point { x, y }: Point) {}
```

*Source: `tools/peg_gen/grammars/logos.peg#L1356-L1393` — testability: untestable*

### `item.fn.param-variadic` — Variadic parameter

`IDENT : T ...` marks a variadic parameter (IS_VARIADIC); plain `IDENT : T` is the ordinary typed parameter.

**Divergence (from Rust):** A6

*Source: `tools/peg_gen/grammars/logos.peg#L1379-L1382`*

### `item.fn.test-attributes` — #[test] / #[ignore] / #[should_panic] flag functions

On a function, #[test] marks it a test, #[ignore] marks it ignored, and #[should_panic] marks expect-panic; #[should_panic(expected="msg")] records the expected panic substring (string literal, quotes stripped). These flags are reset per-function before reading.

*Source: `src/compiler/sema.cpp#L7839-L7873` — testability: untestable*

### `item.fn.no-mangle` — #[no_mangle] keeps bare symbol name

`#[no_mangle]` on a function is detected during collection so the bare, unmangled base name is used as the function's symbol name (for program entry points and inline-asm callees).

*Source: `src/compiler/sema_collect.cpp#L1792-L1803` — testability: untestable*

### `item.fn.test-attrs` — test-harness function attributes

`#[test]`, `#[ignore]`, and `#[should_panic]` are recognised on a function item; `#[should_panic(expected = "…")]` extracts and unquotes the expected panic-message substring from its `ANNOT_KV` `expected` argument.

*Source: `src/compiler/sema_collect.cpp#L1797-L1836` — testability: untestable*

### `item.fn.self-ref-param-type` — Self-receiver parameter type resolution

A parameter marked IS_REF (a `self`/`&self`/`&mut self` receiver) resolves to type `&Self` or `&mut Self` per its IS_MUT marker, substituting the currently in-scope `Self` type, regardless of any explicit written type on the node.

> Re-extracted independently (consistent): A `&self` / `&mut self` receiver parameter takes the type `&Self` / `&mut Self`, where `Self` is the in-scope Self type; mutability follows the `mut` marker.

*Source: `src/compiler/sema_collect.cpp#L4710-L4719` · `src/compiler/sema_collect.cpp#L4855-L4859` · `src/compiler/sema_collect.cpp#L4498-L4502` · `src/compiler/sema_collect.cpp#L4643-L4647` — testability: behavioral*

### `item.fn.no-mangle-bare-symbol` — main / no_mangle / metacall-thunk fns keep bare symbol

A function named `main`, marked `#[no_mangle]` (pending_no_mangle_), or whose base name starts with `__metacall_thunk_`, is registered under its bare base name as symbol_name — package/signature mangling is suppressed for these.

*Source: `src/compiler/sema_collect.cpp#L5074-L5080` — testability: transitive*

### `item.fn.tail-expr-is-return` — Tail expression is implicit return

Inside a fn body, a block's tail expression acts as an implicit return value (typed against the declared return type) for both lowering and reachability analysis.

*Source: `src/compiler/sema_collect.cpp#L4519-L4523`*

### `item.fn.empty-body-void` — Omitted return type defaults to void

A fn that declares no return type has return type `()` (void).

*Source: `src/compiler/sema_collect.cpp#L4477-L4479` · `src/compiler/sema_collect.cpp#L4669-L4671`*

### `item.fn.never-fallback-precompute` — Body-diverges flag for `!` fallback

A fn whose body always diverges is flagged so that type-argument inference can apply the Rust-2024 `!`-fallback rule.

*Source: `src/compiler/sema_collect.cpp#L4673-L4679` — testability: transitive*

### `item.fn.signature-overloading` — Functions overloadable by signature

Functions are keyed by a signature derived from base name, parameter types, and vararg-ness, allowing multiple same-named functions to coexist; only an exact symbol-name collision (same package, base, signature) is a "duplicate function" error.

**Divergence (from Rust):** Rust does not permit free-function overloading by signature.

*Source: `src/compiler/sema_collect.cpp#L4712-L4713` · `src/compiler/sema_collect.cpp#L4837-L4881`*

### `item.fn.runtime-abi-no-mangle` — main, no_mangle, metacall thunks keep bare symbol

`main`, `#[no_mangle]` functions, and `__metacall_thunk_*` functions suppress package/signature mangling and keep their bare names as link symbols.

*Source: `src/compiler/sema_collect.cpp#L4858-L4868` — testability: untestable*

### `item.fn.impl-trait-param-desugar` — `impl Trait` argument-position param desugars to a fresh generic param

A top-level `impl <bound>` parameter type in argument position is desugared into a fresh synthetic generic type-parameter (carrying the corresponding trait bound) appended to the fn's type-param list, so the function becomes an ordinary generic. `impl Trait` in RETURN position is not desugared this way and instead retains opaque-type handling.

*Source: `src/compiler/sema_collect.cpp#L4656-L4666` · `src/compiler/sema_impl.hpp#L2703-L2710`*

### `item.fn.name-underscore-reserved` — `_` reserved as a function name

A function declaration whose name is the single underscore `_` is ill-formed; `_` is reserved for ignored bindings (so `_(...)` cannot become a valid call expression).

```logos
fn _() {}  // error: '_' is reserved for ignored bindings
```

*Source: `src/compiler/sema_decl.cpp#L144-L146` — testability: diagnostic*

### `item.fn.tail-match-as-return` — Tail match arms are return values

When a non-void function's body's last statement is a `match` expression, that match is lowered in tail-return position: its EXPR arms are treated as the function's return value.

*Source: `src/compiler/sema_decl.cpp#L954-L968` — testability: behavioral*

### `item.fn.impl-trait-return-infer` — impl Trait return inferred from body

A function declared `-> impl Trait` has its return type resolved, after body lowering, to the concrete type inferred from the body's return expressions; failure to infer a concrete type is a compile error.

```logos
fn f() -> impl Iterator { 0..3 }
```

*Source: `src/compiler/sema_decl.cpp#L1062-L1071` — testability: diagnostic*

### `item.fn.all-paths-return` — Non-void fn must return on every path

A function whose declared return type is neither `void` nor an error type is rejected ("not all paths return a value") unless every control-flow path through its body returns or diverges; trailing tail expressions count as implicit returns while this check runs (tail-as-return context).

*Source: `src/compiler/sema_decl.cpp#L1072-L1083` — testability: diagnostic*

### `item.fn.param-drop-epilogue` — By-value params dropped at function epilogue

A by-value function parameter of a droppable (move/owning) type is dropped at the function epilogue — mirroring `let`-binding drop semantics — when the body falls off the end without an explicit terminating `return`/`break`/`continue`. A parameter that was moved on any control-flow branch is conservatively excluded from this static epilogue drop, to avoid a double-free on the moved path (at the cost of a possible leak on the non-moved path).

```logos
fn consume(_x: Move) {}  // _x dropped at end
```

*Source: `src/compiler/sema_decl.cpp#L1084-L1117` — testability: behavioral*

### `item.fn.test-modifiers-require-test` — `#[should_panic]`/`#[ignore]` are `#[test]` modifiers

`#[test]` marks a free function as a test case; `#[should_panic]` and `#[ignore]` are modifiers valid only in combination with `#[test]`. All three apply to functions only.

**Uncertainty:** The 'only valid in combination with #[test]' constraint is enforced downstream, not in this unit (comment-stated).

*Source: `src/compiler/sema_impl.hpp#L1488-L1493` — testability: untestable*

## Function parameters

### `item.fn-param.struct-pattern` — Struct-pattern function parameter

A parameter may be an irrefutable struct pattern `Name { a, b, ... }: Name`. Each named field (or its `f: binding` rename, skipping `..` rest and unnamed items) becomes a body-visible binding typed from the matching struct field; binding name `_` is not registered. Desugared to a synthetic parameter plus a prologue `let bind = synth.field;` per binding.

```logos
fn f(Point { x, y }: Point) -> i32 { x + y }
```

*Source: `src/compiler/sema_decl.cpp#L604-L649` · `src/compiler/sema_decl.cpp#L996-L1046` — testability: behavioral*

### `item.fn-param.tuple-pattern` — Tuple-destructure function parameter

A parameter may be an irrefutable tuple pattern `(a, b, ...): (T1, T2, ...)`. Each non-`_` element name becomes a body-visible binding of the corresponding tuple-element type, desugared to a synthetic parameter plus prologue `let a = synth.0; let b = synth.1; ...` (tuple_index reads).

```logos
fn f((a, b): (i32, i32)) -> i32 { a + b }
```

*Source: `src/compiler/sema_decl.cpp#L651-L684` · `src/compiler/sema_decl.cpp#L974-L995` — testability: behavioral*

### `item.fn-param.self-reserved` — `self` reserved for impl receivers

A parameter literally named `self` is a compile error outside an impl-block context; `self` is only the magic receiver name inside impl methods.

```logos
fn f(self: i32) {}  // error outside impl
```

*Source: `src/compiler/sema_decl.cpp#L686-L694` — testability: diagnostic*

### `item.fn-param.datanode-by-value` — DataNode eidos cannot be passed by value

A parameter whose type is (or is an array of) a DataNode datatype (one holding relative-pointer fields) is rejected by value at signature-collection time; the relative pointers require a zone base pointer unavailable in that position — use `DataRef<T>` instead.

**Divergence (from Rust):** Logos addition (zoned/DataNode model); no Rust analog

*Source: `src/compiler/sema_decl.cpp#L700-L713` — testability: diagnostic*

### `item.fn-param.mut-binding` — `mut` parameter binding

A typed parameter `mut x: T` makes `x` a mutable, caller-invisible local binding: the body may reassign or take `&mut` of it. Desugared to an immutable synthetic SSA parameter plus a prologue `let mut x = synth;` (a move of the param value into the user-visible local); the synth name is deliberately not registered as a tracked scope variable, so it is not itself drop-glued.

```logos
fn f(mut x: i32) { x += 1; }
```

*Source: `src/compiler/sema_decl.cpp#L714-L741` · `src/compiler/sema_decl.cpp#L1047-L1061` — testability: behavioral*

### `item.fn-param.owning-box-dyn` — By-value `Box<dyn Trait>` param owns the box

A by-value parameter whose type resolves to an owning trait-object (`Box<dyn Trait>`, collapsed to `TraitObject` with an owning bit) makes the callee own the box: the binding is tagged `owning_dyn` so the epilogue emits vtable `drop_in_place` + dealloc-data + dealloc-handle, and the parameter is tagged `owning_box_dyn` so call sites coerce the argument to a heap fat handle matching the callee's free().

```logos
fn f(b: Box<dyn Trait>) {}
```

*Source: `src/compiler/sema_decl.cpp#L742-L759` — testability: behavioral*

### `item.fn-param.unique-names` — Parameter names must be unique

All parameter names within one function signature (including the user-visible names of destructured/mut-binding params) must be pairwise distinct; a duplicate is a compile error naming the function.

*Source: `src/compiler/sema_decl.cpp#L765-L768` — testability: diagnostic*

### `item.param.no-infer-placeholder` — `_` rejected in fn signature type positions

The inferred-type placeholder `_` is rejected (E0121) when it appears in a fn signature's parameter or return type positions.

*Source: `src/compiler/sema_collect.cpp#L4660-L4662` · `src/compiler/sema_collect.cpp#L4667-L4672`*

### `item.param.self-receiver-and-modifiers` — function parameter and self-receiver forms

A parameter is `[mut] NAME [: TYPE] [...]`; a self-receiver is rendered as `&[mut] self` (a reference parameter without an explicit type). The `...` suffix marks a variadic parameter.

*Source: `src/compiler/sema_render.cpp#L1101-L1125`*

## Static (associated) functions

### `item.static-fn.def` — Static (associated) function definition

`[pub] static [unsafe] fn NAME [<params>] (params) [-> T] { ... }` defines an associated/free function with no `self` receiver; its own optional type-parameter list follows the name, matching instance/free fn generics. The name may be the `new` keyword.

```logos
static fn make<T>(x: T) -> Self { ... }
pub static fn new() -> Self { ... }
```

**Divergence (from Rust):** `static fn` spelling for associated (no-self) functions; Rust uses an `fn` without a `self` parameter inside an impl.

*Source: `tools/peg_gen/grammars/logos.peg#L1067-L1093`*

## External blocks and `extern fn`

### `item.extern.block` — Extern block

`[unsafe] extern ["ABI"] { extern_block_item* }` groups same-ABI externs. The optional ABI string applies to all items in the block (inherited at splice). The Rust-2024 `unsafe extern` marker is accepted with no extra semantics.

```logos
unsafe extern "C" { fn puts(s: *const u8) -> i32; }
```

*Source: `tools/peg_gen/grammars/logos.peg#L1209-L1227`*

### `item.extern.block-item` — Extern block item (fn / static)

Inside an extern block, items use bare `fn IDENT(params [, ...]) [-> T] ;` (no `extern` keyword; trailing `, ...` makes it variadic) or `static [mut] IDENT : T ;`. The produced extern fn carries no ABI of its own; an extern static with no value is marked external (no initializer).

*Source: `tools/peg_gen/grammars/logos.peg#L1228-L1243`*

### `item.extern.fn-def` — Standalone extern fn declaration

`extern ["ABI"] fn IDENT(params [, ...]) [-> T] ;` declares a single FFI function carrying its ABI string verbatim. A trailing `, ...` makes it variadic. Omitting the ABI string selects the default (Logos-internal) calling convention.

*Source: `tools/peg_gen/grammars/logos.peg#L1209-L1216` · `tools/peg_gen/grammars/logos.peg#L1244-L1255`*

### `item.extern.abi-whitelist` — extern ABI string whitelist

The ABI string of an `extern "ABI" { … }` block or an `extern "ABI" fn …` item must be one of "C", "C-unwind", "system", or "Rust" (enclosing quotes optional-stripped); any other string is rejected.

```logos
extern "C" { fn puts(s: *const u8) -> i32; }
```

**Divergence (from Rust):** A7: "C-unwind" is accepted as a whitelisted ABI string at parse time even though unwinding-across-FFI is moot (panic strategy is abort-only).

*Source: `src/compiler/sema_collect.cpp#L1355-L1366` · `src/compiler/sema_collect.cpp#L1400-L1401`*

### `item.extern.block-flatten` — extern block flattening and ABI inheritance

An `extern "ABI" { extern_fn* }` block flattens to a linear item worklist before collection; each child extern-fn that does not carry its own ABI (VALUE slot) inherits the block's ABI string, and later passes treat grouped and flat extern fns identically.

*Source: `src/compiler/sema_collect.cpp#L1348-L1404` — testability: untestable*

### `item.extern-block.abi-default-to-children` — extern block applies its ABI as a default

`extern "ABI" { extern_fn* }` splices its function items into the module item stream; the block ABI applies as a default to children that do not specify their own ABI. An omitted block ABI is the default Logos-internal ABI.

*Source: `tools/peg_gen/grammars/logos.peg#L317`*

### `item.extern-fn.implicit-pub-unsafe` — extern fn is implicitly pub + unsafe

An `extern fn` item is implicitly `pub`, `unsafe`, and extern, independent of any explicit visibility or unsafety markers written on the item.

*Source: `src/compiler/sema_collect.cpp#L4896-L4899` · `src/compiler/sema_collect.cpp#L4684-L4688`*

### `item.extern-fn.dedup-signature` — Repeated identical extern declarations coalesce

Extern fns keep their raw (unmangled) name as the registration/link key. A second `extern fn` declaration with the same base name and the same parameter-type/vararg signature as an already-registered extern is accepted silently (no re-registration, no error) rather than triggering "duplicate function" — so multiple modules may redeclare the same ABI symbol (e.g. libc `malloc`/`free`).

*Source: `src/compiler/sema_collect.cpp#L4927-L4941` — testability: behavioral*

### `item.extern-fn.no-mangle-abi-symbol` — extern fn keeps its bare ABI symbol

An extern fn keeps its raw name as the link symbol (no package/signature mangling); duplicate extern declarations of the same name+signature across modules deduplicate to a single symbol rather than erroring.

*Source: `src/compiler/sema_collect.cpp#L4715-L4729` · `src/compiler/sema_collect.cpp#L4873-L4874` — testability: untestable*

## Structs and fields

### `item.struct.unit-decl` — Unit struct declaration

`[pub] struct IDENT ;` declares a zero-field (unit) struct. A bare IDENT immediately followed by `;` is a unit struct; `struct Foo<...>;` (IDENT then `<`) is instead parsed as an explicit instantiation. This rule MUST be matched before struct_inst.

```logos
pub struct Foo;
```

*Source: `tools/peg_gen/grammars/logos.peg#L1120-L1131` — testability: untestable*

### `item.struct.explicit-inst` — Explicit struct instantiation declaration

`[pub[(vis)]] struct TYPE_REF ;` where TYPE_REF carries type arguments (e.g. `struct Foo<i64>;`) is an explicit-instantiation declaration binding annotations to a generic struct instantiation. The dedicated `instantiate Foo<T>;` form is preferred.

**Divergence (from Rust):** A6: see B-item-92 — bare `struct Foo;` is the unit struct, generic form kept for the unbound-typevar diagnostic

*Source: `tools/peg_gen/grammars/logos.peg#L1133-L1138`*

### `item.struct.named-def` — Named-field struct definition

`[pub[(vis)]] struct IDENT [<type-params>] [where-clause] { field_def_or_doc* method_def_or_doc* }` defines a struct with named fields, optional generics, an optional where-clause, and optional inline method definitions.

```logos
pub struct S<T> where T: Clone { x: T, fn get(&self) -> &T { &self.x } }
```

*Source: `tools/peg_gen/grammars/logos.peg#L1149-L1150` · `tools/peg_gen/grammars/logos.peg#L1160-L1161`*

### `item.struct.tuple-def` — Tuple struct definition

`[pub[(vis)]] struct IDENT [<type-params>] ( tuple_field (, tuple_field)* ) ;` defines a tuple struct whose fields are types only; field names are synthesized as "0","1",… so `foo.0` and pattern `Foo(a,b)` work uniformly with named-field structs. Each tuple_field may carry its own `pub`.

```logos
pub struct Pair(pub i32, i32);
```

*Source: `tools/peg_gen/grammars/logos.peg#L1151-L1152` · `tools/peg_gen/grammars/logos.peg#L1174-L1180`*

### `item.struct.where-clause-named-only` — Where-clause only on IDENT-name struct alternatives

A struct/enum definition where-clause is accepted only on the IDENT-NAME alternatives, not on the antiquot (NAME_VAR / `#`-prefixed) alternatives, because WHERE and NAME_VAR share an AST slot.

**Uncertainty:** Slot-sharing is an implementation constraint surfaced as a grammar restriction.

*Source: `tools/peg_gen/grammars/logos.peg#L1140-L1150` — testability: untestable*

### `item.struct.zoned-promotes-to-datatype` — #[zoned] struct lowered as a datatype (zoned struct)

A struct carrying #[zoned] (promotes_to_datatype) is lowered with IS_ZONED set, treated as a zoned struct/datatype rather than a plain struct.

*Source: `src/compiler/sema.cpp#L7663-L7664` · `src/compiler/sema.cpp#L7679-L7697` — testability: transitive*

### `item.struct.explicit-instantiation-needs-concrete-args` — Explicit struct/datatype instantiation requires concrete type args

A bodyless `struct Foo<args>;` / `datatype Foo<args>;` (NAME absent, TYPE present) is an explicit instantiation: type args must be concrete (no unbound type vars), else it is an error directing to write the body; a bare `struct Foo;` referencing an undefined name is also an error.

*Source: `src/compiler/sema.cpp#L7578-L7628` · `src/compiler/sema.cpp#L7706-L7749`*

### `item.struct.repr-transparent` — #[repr(transparent)] requires single field

`#[repr(transparent)]` on a struct requires the struct to have exactly one field; when satisfied it sets `repr_transparent` so the struct inherits that field's layout exactly, else it is rejected.

```logos
#[repr(transparent)] struct W(i32)
```

*Source: `src/compiler/sema_collect.cpp#L1615-L1625`*

### `item.struct.repr-other-rejected` — non-transparent struct repr modes rejected

On a struct, `#[repr]` with no argument is an error, and any repr mode other than `transparent` (e.g. `C`, `packed`, `align(...)`) parses successfully but is rejected with an explicit "not yet supported" diagnostic — no silent acceptance.

*Source: `src/compiler/sema_collect.cpp#L1604-L1631`*

### `item.struct.attr-flags` — structural struct attribute flags

Recognised structural struct attributes set (OR-accumulate) per-struct SemaStructInfo bit flags: no_auto_drop, self_describing, rel_ptr, pinned, zone_mut, zoned (zoned2), borrow_carrying, non_null.

**Divergence (from Rust):** A6: these are Logos-only zone/memory-model struct attributes with no Rust counterpart.

*Source: `src/compiler/sema_collect.cpp#L1578-L1594` — testability: untestable*

### `item.struct.tuple-struct-fields` — Tuple-struct field shape and synthetic names

A struct is classified as a tuple-struct (`struct W<T>(T);`) when its first FIELD_DEF field carries no NAME slot. Each such unnamed field is given a synthesized decimal name ("0", "1", …) by field position, so ordinary named-field machinery uniformly serves both member access (`foo.0`) and destructuring pattern shape (`Foo(a, b)`).

*Source: `src/compiler/sema_collect.cpp#L4210-L4219` · `src/compiler/sema_collect.cpp#L4239-L4251` — testability: behavioral*

### `item.struct.inline-methods-self-binding` — Inline struct-body methods get Self + struct type-params in scope

Methods declared inline in a struct body (`method_def_or_doc*`) are collected exactly like impl-block methods: `Self` is bound in current_type_params_ to the struct's own type before its methods are collected — a generic self_type built from fresh TypeVars over the struct's type params when it has any (also setting impl_type_params_ to the struct's type params so a generic method's params combine with them, routing it through generic_funcs_ for static-call substitution such as `Pair::<i32,i32>::make()`), or the plain concrete struct type otherwise. Any prior Self/impl_type_params_ binding is saved and restored afterward.

*Source: `src/compiler/sema_collect.cpp#L4292-L4331` — testability: behavioral*

### `item.struct.type-param-unique` — Struct type parameters must be uniquely named

Within a struct declaration, two type parameters may not share a name; a duplicate is a compile error.

*Source: `src/compiler/sema_decl.cpp#L1295-L1298`*

### `item.struct.lifetime-param-unique` — Struct lifetime parameters must be uniquely named

Within a struct declaration, two lifetime parameters may not share a name; a duplicate is a compile error.

*Source: `src/compiler/sema_decl.cpp#L1299-L1302`*

### `item.struct.field-name-unique` — Struct field names must be unique

Within a struct declaration, two fields may not share a name; a duplicate is a compile error.

*Source: `src/compiler/sema_decl.cpp#L1320-L1323`*

### `item.struct.transparent-collapses-layout` — repr(transparent) collapses to the single field's layout

A struct annotated `#[repr(transparent)]` has the layout (size/alignment/ABI) of its single field.

**Uncertainty:** Single-field constraint is enforced elsewhere; this unit only propagates the flag.

*Source: `src/compiler/sema_decl.cpp#L1234-L1236`*

### `item.struct.generic-inline-method-self` — Inline methods of a generic struct bind Self to the generic self-type

For a generic struct `Struct<T...>`, methods declared in the struct body are lowered as if inside `impl<T...> Struct<T...>`: `Self` is bound to `Struct<T...>`, the struct's type params are recorded as the method's impl type-params, and the impl target pattern is `Struct<T...>` — so `-> Self` (and other Self uses) substitute correctly at monomorphization. Non-generic structs lower body methods with their own type params directly.

```logos
struct Pair<A,B>{a:A,b:B; fn make(a:A,b:B)->Self{Self{a,b}}}  // Pair::<i32,i32>::make(..) yields Pair<i32,i32>
```

*Source: `src/compiler/sema_decl.cpp#L1336-L1359` · `src/compiler/sema_decl.cpp#L1365-L1368`*

### `item.struct.generic-method-drops-struct-params` — Generic struct body methods keep only method-level type params

When lowering a body method of a generic struct, type parameters that coincide with the struct's own type parameters are removed from the method's TYPE_PARAMS (mono re-injects them via IMPL_TYPE_PARAMS); only method-introduced type parameters remain method-level.

*Source: `src/compiler/sema_decl.cpp#L1370-L1394` — testability: transitive*

### `item.struct.zoned-field-promotes-to-datatype` — Struct with a zoned-struct field is not plain data

A struct is plain-data (is_data_plain) unless any of its fields has zoned-struct kind, in which case it is a (non-plain) zoned datatype.

*Source: `src/compiler/sema_impl.hpp#L2432` — testability: untestable*

### `item.struct.tuple-struct-positional` — Tuple struct: positional fields, call-form ctor and pattern

`struct Foo(T1, T2);` declares a tuple struct with positional fields; its constructor is the call form `Foo(a, b)` and its pattern is `Foo(x, y)`.

*Source: `src/compiler/sema_impl.hpp#L2434`*

### `item.struct.no-auto-drop` — #[no_auto_drop] suppresses compiler-emitted drop

A struct marked `#[no_auto_drop]` receives NO compiler-emitted automatic Drop (neither user-drop invocation nor field drop glue) — the `ManuallyDrop<T>` lang-item shape.

*Source: `src/compiler/sema_impl.hpp#L2435`*

### `item.struct.custom-dst-last-field-unsized` — Trailing unsized field makes the struct a custom DST

A struct whose LAST field has unsized type (`[T]`, `dyn Trait`, or nested DST) is itself unsized (is_dst); such a struct may appear only behind `&`/`&mut`/`*const`/`*mut`/`Box`, and is constructed via unsafe raw-parts assembly (never by value).

*Source: `src/compiler/sema_impl.hpp#L2436-L2442` — testability: transitive*

### `item.struct.self-describing-thin-ptr` — #[self_describing] custom-DST uses a thin raw pointer

A custom-DST struct marked `#[self_describing]` has in-band recoverable tail length/metadata, so raw `*const T`/`*mut T` to it is a THIN pointer (metadata recovered at deref) rather than a fat DstRef.

*Source: `src/compiler/sema_impl.hpp#L2443-L2447` — testability: untestable*

### `item.struct.fields-and-inherent-methods` — struct item form with optional inherent methods

A struct is `[pub] struct NAME [<type-params>] { fields... }`, or `[pub] struct NAME [<type-params>] ;` when field-less; each field is `[pub] NAME : TYPE [...]`. Inherent methods may be declared in the struct body, which is equivalent to a separate `impl NAME { ... }` block.

**Divergence (from Rust):** Legacy `struct Foo { fields, fn ... }` form (methods inside the struct body) is accepted; not a Rust form.

*Source: `src/compiler/sema_render.cpp#L1140-L1150` · `src/compiler/sema_render.cpp#L1251-L1308`*

### `item.tuple-struct.synthetic-field-names` — Tuple-struct fields named by ordinal

Tuple-struct fields are named by their zero-based positional index rendered as a decimal string ("0", "1", ...); the backing strings are pool-allocated so string_view field names stay valid for the struct registry's lifetime.

*Source: `src/compiler/sema_impl.hpp#L2945-L2956`*

### `item.field.named` — Named field definition

A struct field is `[pub] IDENT : TYPE_REF [,]`. The contextual keywords `new` and `null` are also accepted as field names. A trailing comma is permitted.

*Source: `tools/peg_gen/grammars/logos.peg#L1191-L1202`*

### `item.field.variadic` — Variadic field

A field of form `IDENT ... : TYPE_REF` marks a variadic field (IS_VARIADIC).

**Divergence (from Rust):** A6

*Source: `tools/peg_gen/grammars/logos.peg#L1203-L1204`*

### `item.field.repeat-group` — Repeat-group field (quote)

`#( field_def ),*` and `#( field_def )*` denote a repeat-group of field definitions (REPEAT_GROUP, OP=1 comma-separated / OP=0 plain), for use in quoted item bodies.

**Divergence (from Rust):** A6

*Source: `tools/peg_gen/grammars/logos.peg#L1183-L1186` — testability: untestable*

## Enums

### `item.enum.def` — Enum definition

`[pub] enum NAME [<params>] [: backing_type] [where ...] { variants }` defines an enum, with optional generic params, an optional explicit backing integer type after `:`, and an optional where-clause. A metacall-named form `enum #(<expr>) ...` derives the enum name from a compile-time expression. Where-clauses are permitted only on IDENT-named (not expr-named) enums.

```logos
enum Color { Red, Green, Blue }
enum Tags : u64 { X = 0xdead }
pub enum Option<T> { Some(T), None }
```

*Source: `tools/peg_gen/grammars/logos.peg#L735-L751`*

### `item.enum.variant-shapes` — Enum variant shapes

A variant is one of: unit `Name`; tuple `Name(T, ...)`; variadic-tuple `Name(...T)`; struct-shape `Name { f: T, ... }` (fields may be `pub`); empty struct-shape `Name {}`; or a discriminant-bearing `Name = <disc>`. Variant lists allow leading doc-comments per variant and a trailing comma.

```logos
Some(T)
Point { x: i32, y: i32 }
Empty {}
Args(...i32)
```

**Divergence (from Rust):** Variadic-tuple variant `Name(...T)` has no Rust analog.

*Source: `tools/peg_gen/grammars/logos.peg#L753-L786` · `tools/peg_gen/grammars/logos.peg#L757-L775`*

### `item.enum.repr-int-width` — #[repr(uN/iN)] sets enum discriminant width

`#[repr(I)]` on an enum, where I ∈ {u8,u16,u32,u64,i8,i16,i32,i64,usize,isize}, sets the enum's backing (discriminant) type; if the enum already has a declared backing type via `enum Foo : I' {…}` and I≠I', this is a conflict error. Any other `#[repr(...)]` mode on an enum (e.g. `C`) parses then is rejected as not-yet-supported.

```logos
#[repr(u8)] enum E { A, B }
```

*Source: `src/compiler/sema_collect.cpp#L1719-L1766`*

### `item.enum.zoned-attr` — #[zoned]/#[borrow_carrying] on enum

`#[zoned]` on an enum sets its `zoned2` flag (the niche enum's Ref arm is stored self-relative at rest, absolute as a computed value); `#[borrow_carrying]` sets the `borrow_carrying` flag. Both mirror the equivalent struct-level attributes.

**Divergence (from Rust):** A6: Logos-only zone/niche-enum representation attribute.

*Source: `src/compiler/sema_collect.cpp#L1698-L1713` — testability: transitive*

### `item.enum.empty-legal` — empty enum body is legal

An enum with an empty body is legal (an uninhabited / marker type); no diagnostic is emitted.

```logos
enum Void {}
```

*Source: `src/compiler/sema_collect.cpp#L1939-L1941`*

### `item.enum.discriminant-default` — implicit enum discriminant sequencing

An enum variant without an explicit discriminant takes the value 0 for the first variant and `previous + 1` thereafter; any explicit discriminant (literal, cross-enum reference, or const-expression) resets the running counter to `value + 1` for the next variant.

*Source: `src/compiler/sema_collect.cpp#L1965` · `src/compiler/sema_collect.cpp#L1977` · `src/compiler/sema_collect.cpp#L2136`*

### `item.enum.discriminant-fits` — enum discriminant must fit backing type

When an enum has a backing type, each variant's discriminant value must fit within that backing integer type's range, else it is rejected naming the offending variant.

*Source: `src/compiler/sema_collect.cpp#L2067-L2071`*

### `item.enum.discriminant-const-expr` — enum discriminant from const expression

An enum discriminant may be a general const expression (e.g. `1 << 1`, a bare non-BLOCK node), evaluated via the CTFE channel; or a `metacall { <expr> }` block whose single required tail expression is likewise evaluated via CTFE to produce the discriminant. A metacall discriminant block with no resolvable tail expression is a compile error.

```logos
enum E { A = 1 << 1, B = metacall { 4 } }
```

**Divergence (from Rust):** A1: const-eval at discriminant position runs through metacall/CTFE splicing rather than miri-style const folding.

*Source: `src/compiler/sema_collect.cpp#L2024-L2065`*

### `item.enum.discriminant-from-other-enum` — enum discriminant referencing another enum's variant

An enum discriminant may be written `OtherEnum::OtherVariant` (an optional `as T` cast is dropped — width is governed by the enclosing enum's own backing type); the referenced enum must already be collected and must contain the named variant, else the unresolved enum/variant is reported.

*Source: `src/compiler/sema_collect.cpp#L1984-L2023`*

### `item.enum.variant-payload-shapes` — enum variant payload shapes

An enum variant payload may be: tuple-style (positional type list), struct-shape (named+typed fields in declaration order, field names required unique within the variant), or variadic (single payload type). Payload type positions are resolved as item signatures, where `_` is rejected.

```logos
enum E { Tup(i32, i32), Rec { x: i32 }, Var(i32) }
```

*Source: `src/compiler/sema_collect.cpp#L2081-L2130`*

### `item.enum.type-param-unique` — Enum type parameters must be uniquely named

Within an enum declaration, two type parameters may not share a name; a duplicate is a compile error.

*Source: `src/compiler/sema_decl.cpp#L1504-L1507`*

### `item.enum.variant-name-unique` — Enum variant names must be unique

Within an enum declaration, two variants may not share a name; a duplicate is a compile error.

*Source: `src/compiler/sema_decl.cpp#L1508-L1511`*

### `item.enum.explicit-discriminant` — Enum variants carry an explicit/assigned discriminant and optional backing type

Each enum variant has an integer discriminant value; an enum may declare an explicit backing integer type controlling its discriminant representation.

*Source: `src/compiler/sema_decl.cpp#L1442` · `src/compiler/sema_decl.cpp#L1513-L1518`*

### `item.enum.default-backing-i32` — Enum default discriminant backing type is i32

An enum with no explicit backing type uses i32 as its discriminant backing type.

*Source: `src/compiler/sema_impl.hpp#L2607`*

### `item.enum.struct-shape-variant` — Struct-shape enum variant carries named payload fields

An enum variant `V { x: T, y: U }` is a struct-shape variant with named payload fields (a names array parallel to payload types); user-written field names are resolved to positional indices. Tuple-shape and unit variants carry no payload field names.

*Source: `src/compiler/sema_impl.hpp#L2589-L2596`*

### `item.enum.repr-and-variants` — enum item form

An enum is `[pub] enum NAME [<type-params>] [: TYPE] { variant, ... }` where the optional `: TYPE` gives the discriminant representation type; each variant is `NAME [(types...)] [= [-]discriminant]`.

*Source: `src/compiler/sema_render.cpp#L1152-L1174` · `src/compiler/sema_render.cpp#L1226-L1249`*

## Unions

### `item.union.def` — Union definition

`[pub[(vis)]] union IDENT [<type-params>] [where-clause] { field_def_or_doc* }` defines a union with named fields and optional generics. It is collected internally as a struct flagged `is_union`; no tuple shape, no methods.

```logos
union U { a: i32, b: f32 }
```

*Source: `tools/peg_gen/grammars/logos.peg#L1163-L1172`*

### `item.union.layout-and-unsafe-access` — Union layout and unsafe field access

A `union NAME { f: T, ... }` is a struct-shaped type whose size is max-of-fields aligned to max field alignment; reading or writing a union field requires an `unsafe` block.

> ⚠ CONFLICT: this grammar-layer rule states that *writing* a union field also requires `unsafe`, but the sema-layer rules `item.union.field-write-safety` and `item.union.max-of-fields-layout-unsafe-read` state that writes are safe and only reads require `unsafe`. The sema evidence reflects implemented behavior; both claims are preserved pending reconciliation.

*Source: `tools/peg_gen/grammars/logos.peg#L321`*

### `item.union.lowered-as-struct` — union lowered through struct path

A `union` definition is lowered through the same path as a struct (same field shape); layout/unsafe-gating is a separate concern.

*Source: `src/compiler/sema.cpp#L7557-L7566`*

### `item.union.collected-as-struct` — union shares struct collection shape

A `union NAME { … }` is collected through `collect_struct` with the same named-field/type-param shape as a struct and registered as a known type, with its `is_union` flag set on the resulting SemaStructInfo. A prior `struct NAME`/`union NAME` name collision is caught before this point, at name pre-registration.

```logos
union U { i: i32, f: f32 }
```

*Source: `src/compiler/sema_collect.cpp#L1478-L1490`*

### `item.union.no-empty` — fieldless union rejected

A union with zero fields is rejected at item-collection time; a union must declare at least one field.

```logos
union U {} // error
```

*Source: `src/compiler/sema_collect.cpp#L1495-L1501`*

### `item.union.field-copy-restriction` — union field types restricted to non-move types

Each concretely-typed union field must not be a move type (Vec/Box/String/owning trait object); allowed are Copy types, references, `ManuallyDrop<T>`, or aggregates thereof. A field whose type is a bare unresolved type-parameter (TypeVar) is exempt at collection time and re-checked post-monomorphization; a field that is itself another union type is allowed regardless of that union's own Copy-ness.

**Uncertainty:** Rejection uses is_move_type as the oracle; full ManuallyDrop-recognition/tuple/array recursion is noted in-source as a follow-up refinement.

*Source: `src/compiler/sema_collect.cpp#L1523-L1551`*

### `item.union.shared-namespace` — unions share the struct/enum type namespace

Union definitions occupy the same type namespace as structs (registered in the same name table), so a union name conflicts with a struct/union of the same name, and `type Alias = U;` resolves U through the struct-name lookup.

*Source: `src/compiler/sema_collect.cpp#L391-L414`*

### `item.union.max-of-fields-layout-unsafe-read` — union layout and unsafe field read

A type declared `union NAME { … }` has layout = max-of-fields size aligned to max field alignment (vs struct's sum-of-fields); only one field is active at a time (the active one is implementation-defined) and every field READ requires an enclosing `unsafe`.

> ⚠ CONFLICT: agrees with `item.union.field-write-safety` (only reads gated) and contradicts the grammar-layer `item.union.layout-and-unsafe-access` on write safety.

*Source: `src/compiler/sema_impl.hpp#L2488-L2495`*

### `item.union.field-write-safety` — Union field write is safe, read is unsafe

Writing a union field is safe and does not require `unsafe`; only reading a union field requires an enclosing `unsafe` block. A transient write-lhs flag, set before lowering a place-assign's LHS and RAII-restored after, tells field-read lowering to skip the union-read unsafe gate for that write.

> ⚠ CONFLICT: contradicts the grammar-layer `item.union.layout-and-unsafe-access`, which claims writes also require `unsafe`. This rule carries the implemented sema behavior (write-lhs flag skips the unsafe gate); both claims are preserved pending reconciliation.

*Source: `src/compiler/sema_impl.hpp#L2936-L2943`*

## Type aliases

### `item.type-alias.def` — Type alias definition

`[pub] type NAME [<params>] = <type_ref> ;` introduces a type alias, optionally generic via a type-parameter list.

```logos
type Pair = (i32, i32);
pub type Map<K,V> = HashMap<K,V>;
```

*Source: `tools/peg_gen/grammars/logos.peg#L720-L727`*

### `item.type-alias.duplicate` — Type alias uniqueness per package

Two type aliases with the same name in the same package are an error. A same-name alias from a different package is permitted: the incumbent (first/other-package) keeps the bare name slot and the newcomer registers only under its package-qualified key `pkg::Name`. Lookup probes `cur_package_::name` first, so user code resolves to its own alias.

**Uncertainty:** Cross-package shadowing semantics inferred from the registration logic and comment.

*Source: `src/compiler/sema_collect.cpp#L2127-L2142` — testability: untestable*

### `item.type-alias.no-inferred-rhs` — Type alias RHS may not be the inferred placeholder

A type alias RHS is resolved in item-signature context; `type T = _;` is rejected (no inference context for item signatures). (Rust E0121)

*Source: `src/compiler/sema_collect.cpp#L2114-L2119`*

### `item.type-alias.generic-params` — Type alias may declare type and lifetime parameters

A `type` alias declaration may carry its own generic type parameters and lifetime parameters (e.g. `type Foo<'z, T> = ...;`); a non-generic alias has an empty type-params list.

*Source: `src/compiler/sema_impl.hpp#L2887-L2893` — testability: behavioral*

### `item.type-alias.generic` — type alias with optional generics

A type alias is `[pub] type NAME [<type-params>] = TYPE ;`.

*Source: `src/compiler/sema_render.cpp#L1213-L1224`*

## Constants

### `item.const.def` — Module-level constant definition

A module constant is `[pub] (const|let) NAME [<params>] : T = expr ;`. The `const` keyword admits an optional type-parameter list, making the RHS a generic compile-time factory substituted at each use site; `let` stays non-generic. Both forms require an explicit type annotation and an initializer.

```logos
pub const MAX: i32 = 100;
const PMap<K,V>: WritStatic = @{...};
let X: u8 = 1;
```

**Divergence (from Rust):** `let` accepted as a const keyword at module level; generic `const NAME<...>` factory has no direct Rust analog.

*Source: `tools/peg_gen/grammars/logos.peg#L688-L699`*

### `item.const.inlined-value` — const carries an inlined value expression

A `const` item stores its initializer as a VALUE expression that downstream codegen inlines at each use site (contrasted with statics, which have one global per item).

```logos
const K: i32 = 10;
```

*Source: `src/compiler/sema.cpp#L7882-L7890`*

### `item.const.generic-and-typed` — const item with optional generics and type

A const item is `[pub] const NAME [<type-params>] [: TYPE] = VALUE ;`; const items may be generic.

**Divergence (from Rust):** Generic const items (const with type parameters) are a Logos extension.

*Source: `src/compiler/sema_render.cpp#L1192-L1211`*

## Statics

### `item.static.def` — Module-level static definition

`[pub] static [mut] NAME : T = expr ;` defines a true global with stable storage and address (one global symbol; `&STATIC` identity holds), distinct from `const` inline substitution. The `mut` form (matched before the immutable form) marks mutable storage; without `mut`, reads are safe and writes are rejected.

```logos
static COUNTER: u64 = 0;
static mut FLAG: bool = false;
```

*Source: `tools/peg_gen/grammars/logos.peg#L705-L716` — testability: transitive*

### `item.static.runtime-initialized-storage` — static items get zero-init storage filled at program startup

A non-extern `static` has global storage that is zero-initialized at link time and assigned its declared initializer value at program startup (before `main`), via a synthesized startup initializer running every static's init expression in declaration order. A `static`'s initializer is thus an ordinary runtime-evaluated expression, not a compile-time constant.

**Divergence (from Rust):** Rust requires `static` initializers to be const-evaluable; Logos evaluates them at runtime startup instead.

*Source: `src/compiler/mlir_gen_dyn.cpp#L702-L714` · `src/compiler/mlir_gen_dyn.cpp#L716-L758`*

### `item.static.immutability-not-by-const-global` — immutable static stays writable storage; immutability enforced at sema

Storage for an immutable (non-`mut`) `static` is NOT a read-only constant; it is writable storage assigned once at startup. Immutability of a non-`mut` static is enforced by rejecting writes during semantic analysis, not by making the storage constant.

*Source: `src/compiler/mlir_gen_dyn.cpp#L702-L708`*

### `item.static.aggregate-init-by-copy` — aggregate static initialized by value-copy

If a static's type is an aggregate (struct, zoned struct, tuple, array, slice, closure, or a tagged enum) and its initializer evaluates to a pointer to the value, the static is initialized by copying the full value (size = size_of(T)) into the static's storage; scalar (non-aggregate) statics are initialized by a single store.

*Source: `src/compiler/mlir_gen_dyn.cpp#L741-L756`*

### `item.static.global-storage-and-mut-safety` — static items have global storage; mut access is unsafe

`static [mut] NAME: T = expr;` is a true global with a stable address and `&STATIC` identity. Reads and writes of a `static mut` require `unsafe`. A static with no initializer is an extern (external-linkage) declaration.

*Source: `tools/peg_gen/grammars/logos.peg#L322`*

### `item.static.global-storage` — static gets global storage with symbol, mut/extern flags

A `static` item is lowered with IS_STATIC set, real global storage keyed by a module-qualified symbol (fallback pkg$name); `static mut` sets IS_MUT; a static lacking VALUE is extern (IS_EXTERN, no initializer emitted).

```logos
static X: i32 = 5;
static mut Y: i32 = 0;
```

*Source: `src/compiler/sema.cpp#L7891-L7913`*

### `item.static.link-symbol` — static link symbol qualification

A `static` with an initializer gets a module-qualified link symbol `[<module_id>.]<package>$<name>` (module_id prefix omitted when empty) so that two modules independently declaring the same `pkg::NAME` do not collide at link; an extern static (no initializer) links against the bare, unqualified name.

*Source: `src/compiler/sema_collect.cpp#L1898-L1919` — testability: transitive*

### `item.static.unsafe-access` — static mut and extern static require unsafe

A `static` declared `mut` is recorded in `module_static_muts_`; a `static` with no initializer (extern-linked) is recorded in `module_extern_statics_`. Both categories require `unsafe` at every read/write access.

*Source: `src/compiler/sema_collect.cpp#L1920-L1926`*

### `item.static.extern-requires-unsafe` — Access to an extern-block static requires unsafe

A static declared in an extern block (declaration only, foreign storage) requires `unsafe` at every access.

*Source: `src/compiler/sema_impl.hpp#L1931-L1933` — testability: untestable*

### `item.static.mut-requires-unsafe` — static mut access requires unsafe

Reading or place-assigning a `static mut` item requires an enclosing `unsafe` block.

*Source: `src/compiler/sema_impl.hpp#L2904-L2908`*

### `item.static.address-place-machinery` — static items addressed as places

Every `static [mut]` item has link symbol `<pkg>$<NAME>` (extern-block-declared statics keep the bare name); reads lower as a dereference of the static's address (`Deref(VarRef("__static_addr:<sym>", *T))`) and writes lower as a store through the same address expression.

*Source: `src/compiler/sema_impl.hpp#L2910-L2916` · `src/compiler/sema_impl.hpp#L2929-L2934` — testability: transitive*

### `item.static.shadowing-by-binding` — Local/param binding shadows a module static

A module static name is treated as a static reference only when not shadowed by an in-scope local binding or a type/const-generic parameter of the same name.

*Source: `src/compiler/sema_impl.hpp#L2918-L2927`*

## Traits

### `item.trait.explicit-inst` — Explicit genos/trait specialization declaration

`[pub[(vis)]] <trait-kw> TYPE_REF ;` (no body) binds annotations to a logical-family (genos) specialization of a concrete trait instantiation; implementing eidos inherit the metadata via impl.

**Divergence (from Rust):** A6

*Source: `tools/peg_gen/grammars/logos.peg#L1111-L1118`*

## Implementations

### `item.impl.targets` — Impl block forms and targets

`[unsafe] impl [<impl_params>] [Trait [<args>] for] <target> [where ...] { items }` defines an impl. Trait impls use `Trait for Target`; standalone (inherent) impls omit the trait. The target may be a simple type, pointer, reference, bare slice `[T]`, `dyn Trait`, tuple, or fn-pointer type. Each form admits an optional where-clause before the body.

```logos
impl Foo { ... }
impl<T> Trait for Struct<T> { ... }
impl Debug for (A, B) { ... }
impl<T> MyTrait for [T] { ... }
```

*Source: `tools/peg_gen/grammars/logos.peg#L972-L1051` · `tools/peg_gen/grammars/logos.peg#L1021-L1030`*

### `item.impl.negative` — Negative impl

`impl [<params>] !Trait for <target> [where ...] {}` declares a negative impl (the body must be empty), asserting that the target does not implement Trait.

```logos
impl !Send for Foo {}
```

*Source: `tools/peg_gen/grammars/logos.peg#L992-L1004`*

### `item.impl.items` — Impl item kinds

An impl item is a method definition, an associated-type impl `type NAME [<params>] = T ;`, or an associated-const impl `const NAME : T = expr ;`. Doc-comments may precede impl items.

```logos
type Item = i32;
const N: usize = 4;
```

*Source: `tools/peg_gen/grammars/logos.peg#L1054-L1060` · `tools/peg_gen/grammars/logos.peg#L567`*

### `item.impl.method-reattach-by-package` — Impl methods are attached to their target template only within the same package

An impl method (mangled `<Struct>__<method>__[fg]__<sig>`) whose `<Struct>` names a generic template is hosted on that template only when the template's package equals the method's package; a method with no package attaches to a sole same-named candidate. A cross-package bare-name collision (e.g. user `Rc` vs stdlib `Rc<T>`) does NOT cause adoption, so the method stays with its own struct's emission.

**Uncertainty:** This is an emission/hosting invariant observable as: same-named generics in distinct packages keep their own methods; surfaced as a language-level guarantee against method mis-hosting.

*Source: `src/compiler/sema.cpp#L7002-L7054` — testability: untestable*

### `item.impl.type-params-source` — Impl type parameters come from IMPL_TYPE_PARAMS or (inherent only) TYPE_PARAMS

An impl block's own generic parameters are taken from the generic-trait-impl form `impl<T> Trait for U<T>` (its dedicated IMPL_TYPE_PARAMS list) when present. For an inherent impl `impl<T> U<T>` (no trait name), the parameters are taken from the plain type-parameter list instead. The chosen list is pushed into scope for resolving the target type, trait args, and method signatures/bodies, and is recorded on the impl so lowered methods carry it as their own type_params.

*Source: `src/compiler/sema_decl.cpp#L1716-L1727` — testability: behavioral*

### `item.impl.target-mangling` — Impl self-type is mangled to a canonical target key by type shape

The impl target type is reduced to a canonical string key by shape: pointer/named struct/datatype → struct name (concrete generic instantiations use the monomorphized concrete name; instantiations with unbound TypeVars keep the base name); `[T]` and `&[T]`/`&mut [T]` → `$slice$T` (TypeVar elem) or `$slice$<elem>` (concrete elem), with reference-to-slice targets rebound to the UnsizedSlice form under the same key as bare `[T]`; `dyn Tr` → `$dyn$<Trait>`; `&U`/`&mut U` → `$ref_<U>`/`$mut_ref_<U>` (unbound-TypeVar pointee → `$ref$T`/`$mut_ref$T` sentinel); a generic instantiation `Foo<Args>` mangles to the base name, remangled to the concrete struct name only when the impl itself has no type params and every arg is concrete — otherwise the unsubstituted resolved pattern is captured so mono can pattern-unify a concrete receiver against the impl's own TypeVars rather than binding positionally; tuple `(...)` → `void` (unit), `$tuple$variadic` (single variadic-param element bound as `impl<A...> Trait for (A...)`), else `$tuple$N` (any TypeVar elem) or `$tuple$N$<t1>$<t2>...` (all concrete); fn-pointer → `$fnptr$<arity>` (type-erased by parameter count); any other simple/primitive type → its bare name. Collection-time and lowering-time mangling must agree.

*Source: `src/compiler/sema_decl.cpp#L1730-L1793` · `src/compiler/sema_decl.cpp#L1794-L1818` · `src/compiler/sema_decl.cpp#L1819-L1858` · `src/compiler/sema_decl.cpp#L1859-L1864` — testability: transitive*

### `item.impl.trait-and-inherent` — impl block forms

An impl block is `[unsafe] impl[<impl-type-params>] TRAIT[<type-args>] for TYPE { items }` (trait impl) or `[unsafe] impl[<type-params>] TYPE { items }` (inherent impl); negative impls are permitted.

*Source: `src/compiler/sema_render.cpp#L1310-L1373`*

## Where clauses

### `item.where.clause` — Where clause

`where where_pred (, where_pred)*`. A predicate is `<subject> : trait_bound (+ trait_bound)*` where subject is an associated-type ref, a reference type (`&T`, incl. `for<'a> &'a T`), or a plain type-param; or it is a bare type_param.

```logos
where T: Clone + Send, &T: Into<U>
```

*Source: `tools/peg_gen/grammars/logos.peg#L1257-L1271`*

## Genos specialization and explicit instantiation

### `item.genos.specialization-decl` — genos specialization decl propagates type_code to like-named eidos

A bodyless `genos Name<args>;` (trait-name TYPE, no NAME) records an instantiation annotation; its #[type_code=N] is registered under the canonical and mangled (concrete-struct) names of the like-named eidos/struct, mirrored under both the current and the template's package.

*Source: `src/compiler/sema.cpp#L7930-L8032`*

### `item.instantiate.generic-only` — instantiate decl requires a generic target with type args

`instantiate T;` requires T to be a struct/datatype/enum with non-empty type args; `instantiate Foo;` on a non-generic type is an error ('only applies to generic templates'), and a non-struct/datatype/enum target is an error.

```logos
instantiate Foo<i32>;
```

*Source: `src/compiler/sema.cpp#L7478-L7498`*

## Writ datatypes and schemas

### `item.datatype.def` — Writ datatype definition

A datatype item is `[pub[(vis)]] eidos NAME [<type-params>] { field_def_or_doc* }`. It declares a Writ-fabric datatype with named/repeat-group fields; the optional generic parameter list and visibility marker are accepted.

```logos
pub eidos Point<T> { x: T, y: T }
```

**Divergence (from Rust):** A6

*Source: `tools/peg_gen/grammars/logos.peg#L1096-L1100` — testability: untestable*

### `item.datatype.explicit-inst` — Explicit datatype instantiation declaration

`[pub[(vis)]] eidos TYPE_REF ;` (no body) is an explicit-instantiation declaration that binds metadata annotations (e.g. `#[type_code=N]`) to a concrete generic instantiation, e.g. `#[type_code=42] datatype Array<i32>;`.

**Divergence (from Rust):** A6

*Source: `tools/peg_gen/grammars/logos.peg#L1102-L1109`*

### `item.datatype.is-zoned` — datatype/eidos is always zoned

A `datatype` (eidos) definition is always lowered with IS_ZONED set, including its specializations, unconditionally (unlike a plain struct which requires #[zoned]).

*Source: `src/compiler/sema.cpp#L7750-L7754` · `src/compiler/sema.cpp#L7794-L7798`*

### `item.datatype.type-code-unique` — exclusive datatype annotations are unique

On a datatype item, the exclusive annotation names `type_code` and `annotation` may each appear at most once; a duplicate occurrence of either on the same item is a compile error.

**Divergence (from Rust):** A6: part of the Writ datatype/type-code fabric, Logos-only.

*Source: `src/compiler/sema_collect.cpp#L1662-L1674`*

### `item.datatype.type-code-register` — #[type_code=N] registers explicit type code

`#[type_code=N]` on a datatype registers N under the datatype's fully-qualified name (`pkg::Name`, or bare `Name` with no current package) in the explicit-type-code table, made visible to `collect_impl` within the same collection pass; `#[annotation]` separately flags the datatype's SemaStructInfo as a user-annotation type.

**Divergence (from Rust):** A6: Writ datatype-family mechanism, Logos-only.

*Source: `src/compiler/sema_collect.cpp#L1675-L1687` — testability: untestable*

### `item.datatype.explicit-instantiation-skip` — nameless datatype/struct nodes skip collection

A DATATYPE or STRUCT item node carrying no NAME key (an explicit generic-instantiation declaration that only binds annotations onto an existing generic instantiation) is not collected as a new type; collection is skipped for that node.

*Source: `src/compiler/sema_collect.cpp#L1561` · `src/compiler/sema_collect.cpp#L1637-L1638` — testability: transitive*

### `item.schema.struct-shaped-collection` — schema / schema-enum collected as struct-shaped view

`schema S { … }` is collected as a Struct-shaped view; `schema enum E { … }` is collected as a Struct-shaped union view. Both run struct-target annotation validation with type-params disallowed at this call site.

*Source: `src/compiler/sema_collect.cpp#L1768-L1785` — testability: transitive*

### `item.schema.shares-struct-namespace` — schema/schema-enum declarations share the struct namespace

`schema S { … }` and `schema enum E { … }` item names are pre-registered into the same name table as STRUCT/UNION_DEF, mirroring struct/union registration so forward references (e.g. `type Alias = S;`) resolve before the schema body is collected in the later fields/variants pass. A name collision with an existing struct/union/schema of the same name is an error ('duplicate schema/struct'), suppressed only when the two definitions are ODR-equal.

**Related:** `item.union.shared-namespace`, `item.dup.odr-dedup`

*Source: `src/compiler/sema_collect.cpp#L415-L434`*

## Duplicate items (ODR)

### `item.dup.odr-dedup` — structurally identical duplicate items dedup; differing ones error

Two item definitions (struct/union/schema/datatype/enum) sharing the same name in the same package are an error UNLESS their AST sub-trees are structurally equal, in which case the duplicate is silently dropped (ODR-style dedup). Structural equality recurses through TinyObjectMap-by-bitmap-key, Array-by-index and WritString-by-content, ignores SRC_LINE metadata (so identical items emitted by metaprogramming at different source positions still dedup), and treats any other value-kind pair as conservatively unequal.

**Divergence (from Rust):** Logos addition: ODR dedup of metacall-emitted items (Rust has no metacall splice model).

*Source: `src/compiler/sema_collect.cpp#L25-L76` · `src/compiler/sema_collect.cpp#L267-L282` · `src/compiler/sema_collect.cpp#L374-L467` — testability: diagnostic*
