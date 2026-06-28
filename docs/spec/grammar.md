# Grammar

> Scope: the surface syntax of the Logos source language plus the two ancillary literal/IDL grammars (Writ data notation, Hest RPC IDL). Source layers: PEG grammars under `tools/peg_gen/grammars/` (`logos.peg`, `writ.peg`, `hrpc.peg`), with a few rules corrected against the compiler decoder (`src/compiler/`). Each rule's `id` is its permanent linkable address.

> **Conflicting ids (surfaced, not merged):** the following ids are defined in more than one grammar artifact (the Logos-embedded Writ literal grammar vs. the standalone Writ data grammar). Both definitions are emitted below under their respective sections; they are distinct rules that happen to share an id and are flagged inline:
>   - `grammar.writ.array-literal` (in: logos, writ)
>   - `grammar.writ.map-literal` (in: logos, writ)
>   - `grammar.writ.param-placeholder` (in: writ)

# Logos source grammar

## `keyword`

### `lex.keyword.async-await-reserved` — async/await reserved but unused

`async` and `await` are tokenized as keywords but reserved with no grammar use (kept for a future stackless-coroutine path on wasm32/64).

*Note (uncertainty):* Reserved-without-use status is stated in the source comment; actual rejection behavior in surface grammar is defined in the %rules unit.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L377-L380`

### `lex.keyword.reserved-set` — Reserved keyword set

The following are reserved keywords matched as distinct tokens and unavailable as ordinary identifiers: continue, quote_item, quote_expr, quote_ty, template, package, instantiate, eidos, genos, auto, metacall, static, return, extern, struct, union, match, while, break, false, trait, const, type, impl, enum, loop, else, true, for, use, mut, let, dyn, tagged, pub, new, fn, if, in, as, where, unsafe, move, typeof, offset_of, ref, null, async, await.

*Divergence:* Adds Logos-specific keywords absent in Rust: quote_item/quote_expr/quote_ty/template/package/instantiate/eidos/genos/auto/metacall/tagged/new/typeof/offset_of/null; lacks Rust keywords (mod, pub(crate), crate, self, Self, fn-async forms, etc.) handled elsewhere.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L328-L380`

## `punct`

### `lex.punct.symbols` — Punctuation and operator tokens

Single- and multi-character delimiter/operator tokens are recognized, including: { } [ ] ( ) : :: , ; -> => . .. ..= ... * & && | || ! ? = == != < <= > >= << >> <<= >>= + - / % += -= *= /= %= &= |= ^= ^ @ # $ . Multi-character operators are matched in longest-match-first order (e.g. <<= before <<, ..= and ... before .., && before &).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L382-L434`

## `token`

### `lex.token.ident` — Identifier token

IDENT = `[a-zA-Z_][a-zA-Z0-9_]*` — ASCII letter/underscore followed by ASCII alphanumerics/underscores.

*Divergence:* Identifiers are ASCII-only; Rust permits Unicode (XID) identifiers and raw identifiers `r#name`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L467`

### `lex.token.lifetime` — Lifetime token

LIFETIME = `'[a-z_][a-z0-9_]*` — an apostrophe followed by a lowercase-initiated identifier (no closing apostrophe).

*Divergence:* Lifetime names must start with a lowercase letter or `_`; uppercase-initial lifetimes (allowed in Rust) are not recognized.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L466`

## `literal`

### `expr.literal.kinds` — Primary literal forms

Primary literals: integer, float, char, string, raw string, byte string, and `true`/`false` booleans. A byte-string literal lowers to a `[u8; N]` array literal of its decoded bytes (escapes \n \t \r \0 \\ \" \x.. supported).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2762-L2773`, `tools/peg_gen/grammars/logos.peg#L2764-L2768`

### `lex.literal.char` — Char literal

A char literal CHAR_LIT = `'(\\.|[^'\\])'` is a single `\`-escape or one Unicode codepoint between apostrophes; it is matched BEFORE LIFETIME so `'A'` (with closing apostrophe) wins over a lifetime read. The body decodes to the scalar codepoint value.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L458-L465`

### `lex.literal.char-before-lifetime` — Char-vs-lifetime disambiguation

When the source could begin either a char literal or a lifetime, the lexer prefers the char literal: `'a'` lexes as a char, `'a` (no closing apostrophe) lexes as a lifetime.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L458-L466`

### `lex.literal.float` — Float literal syntax

A float literal matches an optional leading `-`, an integer part, a mandatory `.` with a fractional part (both `[0-9][0-9_]*`), an optional exponent `([eE][+-]?[0-9][0-9_]*)`, and an optional suffix `f32` or `f64`. `_` digit separators are permitted.

*Divergence:* A leading `-` is part of the float token (Rust parses `-` as separate unary minus). A fractional part is mandatory (no `1.` form); float-width suffix set is {f32,f64}.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L456`

### `lex.literal.integer` — Integer literal syntax and width suffixes

An integer literal matches an optional leading `-`, then a decimal (`[0-9][0-9_]*`), hex (`0x[0-9a-fA-F_]+`), binary (`0b[01_]+`), or octal (`0o[0-7_]+`) magnitude, with `_` digit separators, optionally suffixed by a width tag drawn from {i8,i16,i24,i32,i56,i64,i128,u8,u16,u24,u32,u56,u64,u128,usize,isize}.

*Divergence:* A11: width set includes Writ-fabric widths i24/u24/i56/u56 beyond Rust's {8,16,32,64,128}+size. Also: a leading `-` is part of the integer token itself (Rust treats `-` as a separate unary operator).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L457`

### `lex.literal.string` — String, raw-string, and byte-string literals

STRING = `"([^"\\]|\\.)*"` (escapes via backslash). The bare `RAW_STRING` grammar token is `r"[^"]*"`, but the hand-rolled literal decoder also accepts hash-delimited raw strings `r#"..."#` / `r##"..."##` (see `lex.litstr.raw-hash-count`), which CAN contain `"`. BYTE_STRING = `b"([^"\\]|\\.)*"`.

*Note (uncertainty):* The PEG token regex under-specifies raw strings vs the actual decoder; behavior corrected against the compiler (raw strings are Rust-like, hash-delimited forms supported), so this is NOT a divergence.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L453-L455`, `src/compiler/mlir_gen_expr.cpp#L317-L336`

*Related:* `lex.litstr.raw-hash-count`

## `comment`

### `lex.comment.doc-tokens` — Doc comments emitted as tokens

Doc comments are lexed as real tokens (not skipped): DOC_LINE = `///[^\n]*` (outer line), DOC_INNER = `//![^\n]*` (inner module-level line), DOC_BLOCK = `/**...*/` (outer block), DOC_BLOCK_INNER = `/*!...*/` (inner block). These attach as documentation to the following item / enclosing module.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L436-L450`

### `lex.comment.skip` — Whitespace and ordinary comments skipped

Inter-token skip whitespace is `[ \t\n\r]+`; ordinary line comments `//[^\n]*` and block comments `/*...*/` are skipped. The `///`, `//!`, `/**`, `/*!` doc forms are excluded from the skip rules so their dedicated doc-comment tokens win.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L469-L472`, `tools/peg_gen/grammars/logos.peg#L436-L439`

## `package`

### `module.package.decl` — Package declaration header

A compilation unit begins with `package NAME ('.' IDENT)* ';'`, optionally preceded by inner doc-comments (`//!`, `/*! */`) and inner attributes (`#![...]`). The dotted path gives the package's full name to arbitrary depth (first component = NAME, remaining components = PATH_PARTS). After the package line come zero-or-more use-declarations, then zero-or-more items.

Examples:

```logos
package a.b.c;
//! crate doc
#![no_implicit_prelude]
package app;
```

*Divergence:* Rust uses no `package` header; module name is path-derived. Logos requires an explicit `package` line with a dotted package path.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L489-L490`

## `use`

### `module.use.variant-shorthand-vs-subpackage` — use {..} disambiguated by first-character case

In `use pkg.Path.X.{V1, V2, ...};` the last dotted segment `X` disambiguates by its first character's case: uppercase ⇒ enum-variant bare-name shorthand import; lowercase ⇒ grouped sub-package import.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L309`

### `module.use.from-module` — use with explicit source module

`[pub] use pkg('.'IDENT)* IDENT use_module ';'` imports `pkg.path` from a named module; the trailing bare IDENT is the contextual `from` keyword and `use_module` is the source (a bare name or a quoted string for hyphenated ids, with quotes stripped). The from-bearing alternative is tried before the plain form.

Examples:

```logos
use foo.Bar from "logos-lang";
pub use a.b.C from othermod;
```

*Divergence:* `use ... from <module>` clause has no Rust analog.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L498-L521`

### `module.use.path` — Plain use declaration

`[pub] use pkg('.'IDENT)* ';'` brings a dotted package path into scope. `pub use` re-exports it. Path components after the head use a leading-dot separator (`.IDENT`).

Examples:

```logos
use std.collections.HashMap;
pub use core.Option;
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L500-L516`, `tools/peg_gen/grammars/logos.peg#L526-L527`

### `module.use.variant-shorthand` — Enum-variant bare-name import

`use pkg.Path.Type.{V1, V2, ...} ;` brings the named variants of enum `Type` into bare (unqualified) scope. The last dotted component before `.{...}` is the enum type name; the brace-list (trailing comma allowed) names the variants.

Examples:

```logos
use core.Option.{Some, None};
```

*Divergence:* Uses `.`-separated path with `.{}` variant group; Rust spells this `use core::Option::{Some, None};` (A: `::`-item / `.`-pkg path model).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L506-L511`, `tools/peg_gen/grammars/logos.peg#L523-L527`

## `module`

### `grammar.module.from-contextual-keyword` — `from` in `use pkg from <module>` is a contextual keyword

In `use pkg from <module>;` the token `from` is a contextual keyword: it is lexed as a bare identifier and recognized as the import-source separator only by position, not reserved globally. Hence `from` remains usable as an ordinary identifier elsewhere (e.g. the `From::from` method).

Examples:

```logos
use net from "std/net";
let x = From::from(y);
```

*Note (uncertainty):* Production and the sema-side check `ident == "from"` live in grammar/logos/rule-module; this unit only documents the contextual-keyword status.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L91-L96`

## `kinds`

### `stmt.kinds.dispatch` — Statement forms

A statement is one of: nested-fn, labeled-loop, let-else, let, for, while, loop, return, break, continue, deref-write, if-expr, match, destructure-assign, assign, compound-assign, place-assign, unsafe-block, block, `expr ;` (expression statement), or a trailing `expr` (block tail value). A block without a trailing `;` yields its final expression as the block value.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1839-L1866`

### `item.kinds.set` — Module item alternatives

A module item is one of: doc-comment, annotation, template decl, const/static def, type alias, enum def, datatype def/inst, trait def/inst, struct unit/def/inst, instantiate decl, item-position metacall, fn-macro item invocation, union def, impl block, extern block, extern fn, or fn def — each in plain and `pub` forms where visibility applies.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L529`

## `doc`

### `item.doc.comment-attached-to-next-item` — Doc comments attach as documentation

Outer doc comments (`///`, `/** */`) accumulate and attach to the next item; inner doc comments (`//!`, `/*! */`) accumulate into the module-level inner documentation. The comment markers are stripped.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L305-L308`

### `item.doc.inner-module` — Inner doc-comment is module summary

An inner doc-comment (`//!` line or `/*! */` block) accumulates into the enclosing module's doc summary and is never attached to a specific item.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L541-L554`

### `item.doc.outer-block` — Outer block doc-comment

An outer block doc-comment `/** ... */` is an item/member-stream element with the same next-item binding role as line doc-comments; the `/**` envelope and per-line leading `*` are stripped and lines joined with newline.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L544-L549`

### `item.doc.outer-line` — Outer line doc-comment binds to next item

An outer line doc-comment (`///`, captured as DOC_LINE) is an item-stream element; consecutive outer doc-comments accumulate and attach to the next real item.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L536-L537`

## `annotation`

### `item.annotation.arg-forms` — Attribute argument forms

Within an attribute argument list, an argument is one of: `IDENT(args)` (nested call), `IDENT = lit` (key-value), a bare literal (positional), or a bare IDENT (legacy). A literal may be an enum ref `IDENT::IDENT`, raw/normal string, float, integer, `true`/`false`, or a bracketed array of literals. Lists allow a trailing comma.

Examples:

```logos
#[cfg(target = "x86")]
#[align(8)]
#[list([1, 2, 3])]
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L648-L672`

### `item.annotation.forms` — Outer attribute forms

An attribute is `#[ NAME (args) ]`, `#[ NAME = val ]`, or `#[ NAME ]`. The `= val` form admits an enum literal `IDENT::IDENT` or an integer.

Examples:

```logos
#[derive(Debug)]
#[repr = 8]
#[inline]
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L619-L641`

### `item.annotation.inner-attribute` — Inner attribute attaches to enclosing module

An inner attribute `#![ ... ]` (with the same `(args)` / `= val` / flag payload shapes as an outer attribute) attaches to the enclosing module rather than the following item. (Currently only `#![no_implicit_prelude]`.)

Examples:

```logos
#![no_implicit_prelude]
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L631-L636`

## `attr`

### `module.attr.inner-vs-item-attribute` — Inner attribute applies at file/module level

`#![name]` / `#![name(args)]` / `#![name=val]` is a file/module-level inner attribute, distinct from per-item `#[...]` attributes.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L310`

## `visibility`

### `item.visibility.pub-module` — Visibility marker pub / pub(module)

Item visibility is `pub` (fully exported) or `pub(IDENT)` where IDENT is a contextual keyword validated == "module" in sema, meaning module-linkage: visible to other packages of the SAME module but not exported to consumers.

Examples:

```logos
pub(module) fn helper() {}
```

*Divergence:* Logos uses `pub(module)` for module-linkage; Rust uses `pub(crate)`/path-restricted visibilities.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1273-L1284`

## `const`

### `item.const.def` — Module-level constant definition

A module constant is `[pub] (const|let) NAME [<params>] : T = expr ;`. The `const` keyword admits an optional type-parameter list, making the RHS a generic compile-time factory substituted at each use site; `let` stays non-generic. Both forms require an explicit type annotation and an initializer.

Examples:

```logos
pub const MAX: i32 = 100;
const PMap<K,V>: WritStatic = @{...};
let X: u8 = 1;
```

*Divergence:* `let` accepted as a const keyword at module level; generic `const NAME<...>` factory has no direct Rust analog.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L688-L699`

## `static`

### `item.static.global-storage-and-mut-safety` — static items have global storage; mut access is unsafe

`static [mut] NAME: T = expr;` is a true global with a stable address and `&STATIC` identity. Reads and writes of a `static mut` require `unsafe`. A static with no initializer is an extern (external-linkage) declaration.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L322`

### `item.static.def` — Module-level static definition

`[pub] static [mut] NAME : T = expr ;` defines a true global with stable storage and address (one global symbol; `&STATIC` identity holds), distinct from `const` inline substitution. The `mut` form (matched before the immutable form) marks mutable storage; without `mut`, reads are safe and writes are rejected.

Examples:

```logos
static COUNTER: u64 = 0;
static mut FLAG: bool = false;
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L705-L716`

## `type-alias`

### `item.type-alias.def` — Type alias definition

`[pub] type NAME [<params>] = <type_ref> ;` introduces a type alias, optionally generic via a type-parameter list.

Examples:

```logos
type Pair = (i32, i32);
pub type Map<K,V> = HashMap<K,V>;
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L720-L727`

## `enum`

### `const.enum.discriminant` — Enum discriminant value forms

A variant discriminant `Name = D` may be: a bare (optionally negated) integer literal that is the complete value (no trailing binary operator); `metacall <block>`; a cross-enum reference `OtherEnum::Variant` (with optional `as T` cast whose type is dropped, width governed by the enclosing enum's backing/repr); or a general constant expression evaluated via CTFE. A bare literal alt only matches when no binary operator follows; otherwise the value falls through to the const-expr alternative.

Examples:

```logos
Green = 5
Lo = -1
Purple = 1 << 1
X = Other::Y as u8
```

*Divergence:* Cross-enum discriminant reference `OtherEnum::Variant` as a discriminant value has no Rust analog.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L788-L812`, `tools/peg_gen/grammars/logos.peg#L760-L763`

### `item.enum.def` — Enum definition

`[pub] enum NAME [<params>] [: backing_type] [where ...] { variants }` defines an enum, with optional generic params, an optional explicit backing integer type after `:`, and an optional where-clause. A metacall-named form `enum #(<expr>) ...` derives the enum name from a compile-time expression. Where-clauses are permitted only on IDENT-named (not expr-named) enums.

Examples:

```logos
enum Color { Red, Green, Blue }
enum Tags : u64 { X = 0xdead }
pub enum Option<T> { Some(T), None }
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L735-L751`

### `item.enum.variant-shapes` — Enum variant shapes

A variant is one of: unit `Name`; tuple `Name(T, ...)`; variadic-tuple `Name(...T)`; struct-shape `Name { f: T, ... }` (fields may be `pub`); empty struct-shape `Name {}`; or a discriminant-bearing `Name = <disc>`. Variant lists allow leading doc-comments per variant and a trailing comma.

Examples:

```logos
Some(T)
Point { x: i32, y: i32 }
Empty {}
Args(...i32)
```

*Divergence:* Variadic-tuple variant `Name(...T)` has no Rust analog.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L753-L786`, `tools/peg_gen/grammars/logos.peg#L757-L775`

## `datatype`

### `item.datatype.def` — Writ datatype definition

A datatype item is `[pub[(vis)]] eidos NAME [<type-params>] { field_def_or_doc* }`. It declares a Writ-fabric datatype with named/repeat-group fields; the optional generic parameter list and visibility marker are accepted.

Examples:

```logos
pub eidos Point<T> { x: T, y: T }
```

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1096-L1100`

### `item.datatype.explicit-inst` — Explicit datatype instantiation declaration

`[pub[(vis)]] eidos TYPE_REF ;` (no body) is an explicit-instantiation declaration that binds metadata annotations (e.g. `#[type_code=N]`) to a concrete generic instantiation, e.g. `#[type_code=42] datatype Array<i32>;`.

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1102-L1109`

## `struct`

### `item.struct.explicit-inst` — Explicit struct instantiation declaration

`[pub[(vis)]] struct TYPE_REF ;` where TYPE_REF carries type arguments (e.g. `struct Foo<i64>;`) is an explicit-instantiation declaration binding annotations to a generic struct instantiation. The dedicated `instantiate Foo<T>;` form is preferred.

*Divergence:* A6: see B-item-92 — bare `struct Foo;` is the unit struct, generic form kept for the unbound-typevar diagnostic

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1133-L1138`

### `item.struct.named-def` — Named-field struct definition

`[pub[(vis)]] struct IDENT [<type-params>] [where-clause] { field_def_or_doc* method_def_or_doc* }` defines a struct with named fields, optional generics, an optional where-clause, and optional inline method definitions.

Examples:

```logos
pub struct S<T> where T: Clone { x: T, fn get(&self) -> &T { &self.x } }
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1149-L1150`, `tools/peg_gen/grammars/logos.peg#L1160-L1161`

### `item.struct.tuple-def` — Tuple struct definition

`[pub[(vis)]] struct IDENT [<type-params>] ( tuple_field (, tuple_field)* ) ;` defines a tuple struct whose fields are types only; field names are synthesized as "0","1",… so `foo.0` and pattern `Foo(a,b)` work uniformly with named-field structs. Each tuple_field may carry its own `pub`.

Examples:

```logos
pub struct Pair(pub i32, i32);
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1151-L1152`, `tools/peg_gen/grammars/logos.peg#L1174-L1180`

### `item.struct.unit-decl` — Unit struct declaration

`[pub] struct IDENT ;` declares a zero-field (unit) struct. A bare IDENT immediately followed by `;` is a unit struct; `struct Foo<...>;` (IDENT then `<`) is instead parsed as an explicit instantiation. This rule MUST be matched before struct_inst.

Examples:

```logos
pub struct Foo;
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1120-L1131`

### `item.struct.where-clause-named-only` — Where-clause only on IDENT-name struct alternatives

A struct/enum definition where-clause is accepted only on the IDENT-NAME alternatives, not on the antiquot (NAME_VAR / `#`-prefixed) alternatives, because WHERE and NAME_VAR share an AST slot.

*Note (uncertainty):* Slot-sharing is an implementation constraint surfaced as a grammar restriction.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1140-L1150`

### `pat.struct.field` — Struct-pattern field

A struct-pattern field is `..` (rest), `name: subpat`, `0: subpat` (tuple-struct field by index, resolved positionally), `ref name`, `ref mut name`, or a bare `name` shorthand binding the field to a same-named local.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1980-L1999`

### `pat.struct.shape` — Struct pattern

`Point { field_list }` / `Point {}` destructure a struct by named fields.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2138-L2142`

## `union`

### `item.union.layout-and-unsafe-access` — Union layout and unsafe field access

A `union NAME { f: T, ... }` is a struct-shaped type whose size is max-of-fields aligned to max field alignment; reading or writing a union field requires an `unsafe` block.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L321`

### `item.union.def` — Union definition

`[pub[(vis)]] union IDENT [<type-params>] [where-clause] { field_def_or_doc* }` defines a union with named fields and optional generics. It is collected internally as a struct flagged `is_union`; no tuple shape, no methods.

Examples:

```logos
union U { a: i32, b: f32 }
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1163-L1172`

## `field`

### `item.field.named` — Named field definition

A struct field is `[pub] IDENT : TYPE_REF [,]`. The contextual keywords `new` and `null` are also accepted as field names. A trailing comma is permitted.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1191-L1202`

### `item.field.repeat-group` — Repeat-group field (quote)

`#( field_def ),*` and `#( field_def )*` denote a repeat-group of field definitions (REPEAT_GROUP, OP=1 comma-separated / OP=0 plain), for use in quoted item bodies.

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1183-L1186`

### `item.field.variadic` — Variadic field

A field of form `IDENT ... : TYPE_REF` marks a variadic field (IS_VARIADIC).

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1203-L1204`

### `expr.field.tuple-index` — Tuple / field access

Postfix `.field` reads a named field and `.N` (integer) reads the Nth tuple/tuple-struct element.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2684-L2685`, `tools/peg_gen/grammars/logos.peg#L2678-L2679`

## `trait`

### `item.trait.explicit-inst` — Explicit genos/trait specialization declaration

`[pub[(vis)]] <trait-kw> TYPE_REF ;` (no body) binds annotations to a logical-family (genos) specialization of a concrete trait instantiation; implementing eidos inherit the metadata via impl.

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1111-L1118`

### `grammar.trait.relaxed-bound-sized-only` — Relaxed bound `?Trait` accepts only `?Sized`

A relaxed trait bound `?Trait` on a trait-bound (the RELAXED marker) is valid only for `?Sized`; no other relaxed bounds are accepted.

Examples:

```logos
fn f<T: ?Sized>(x: &T) {}
```

*Note (uncertainty):* Documented as 'Phase 1' restriction; enforcement occurs outside this slot-table unit.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L75`

### `grammar.trait.bound-arg-assoc-eq` — Associated-type equality in bound args

bound_arg_list mixes positional type/lifetime args and associated-type equality clauses; bound_arg ::= IDENT '=' type_ref (ASSOC_EQ_BIND) | type_or_lt_arg.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3126-L3133`

### `grammar.trait.bound-forms` — Trait bound forms

A trait_bound is an IDENT optionally with generic args 'Name<bound_arg_list>' or Fn-family parenthesized form 'Name(closure_type_args?) ('-> type)?', each optionally HRTB-prefixed.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3095-L3124`

### `grammar.trait.relaxed-bound-question` — Relaxed bound ?Trait

trait_bound may be '?' IDENT (RELAXED). Grammatically any '?Ident' is accepted; sema rejects anything other than '?Sized' (which opts a type parameter out of the implicit Sized bound).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3092-L3096`

## `method`

### `trait.method.signatures` — Trait method declarations

A trait item is a method declaration (`[unsafe] fn NAME [<params>] (params) [-> T] [where ...] (block | ';')`) — body-bearing alts give a default impl, `;`-terminated alts are required methods — or an associated type/const. Method names may be `new`/`null` keywords. A `where` clause may follow the signature (before block or `;`); on a default body it gates per-impl default synthesis (skip the default when the bound fails for the impl's concrete type).

Examples:

```logos
fn next(self) -> Option<Item>;
fn max(self) -> Item where Item: Ord { ... }
fn new() -> Self;
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L885-L963`, `tools/peg_gen/grammars/logos.peg#L933-L951`

## `assoc`

### `type.assoc.projection` — Associated-type projection

`T::Item` and `T::Item<A,B>` (GAT with type args) are associated-type references; the `::Name[<args>]` tail may chain one or more times. `<T as Trait>::Assoc` is the fully-qualified form, with the disambiguating trait recorded for resolution.

Examples:

```logos
<Vec<T> as IntoIterator>::IntoIter
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1475-L1488`

### `trait.assoc.type-and-const` — Associated type and const declarations in traits

Trait associated items: `type NAME [<params>] [= T] ;` (optional default and optional bound list `: B + B`) declares an associated type; `const NAME : T [= expr] ;` declares an associated const, optionally with a default value.

Examples:

```logos
type Item;
type Item: Ord = i32;
const N: usize = 0;
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L952-L963`

## `impl`

### `item.impl.items` — Impl item kinds

An impl item is a method definition, an associated-type impl `type NAME [<params>] = T ;`, or an associated-const impl `const NAME : T = expr ;`. Doc-comments may precede impl items.

Examples:

```logos
type Item = i32;
const N: usize = 4;
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1054-L1060`, `tools/peg_gen/grammars/logos.peg#L567`

### `item.impl.negative` — Negative impl

`impl [<params>] !Trait for <target> [where ...] {}` declares a negative impl (the body must be empty), asserting that the target does not implement Trait.

Examples:

```logos
impl !Send for Foo {}
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L992-L1004`

### `item.impl.targets` — Impl block forms and targets

`[unsafe] impl [<impl_params>] [Trait [<args>] for] <target> [where ...] { items }` defines an impl. Trait impls use `Trait for Target`; standalone (inherent) impls omit the trait. The target may be a simple type, pointer, reference, bare slice `[T]`, `dyn Trait`, tuple, or fn-pointer type. Each form admits an optional where-clause before the body.

Examples:

```logos
impl Foo { ... }
impl<T> Trait for Struct<T> { ... }
impl Debug for (A, B) { ... }
impl<T> MyTrait for [T] { ... }
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L972-L1051`, `tools/peg_gen/grammars/logos.peg#L1021-L1030`

## `extern`

### `item.extern.block` — Extern block

`[unsafe] extern ["ABI"] { extern_block_item* }` groups same-ABI externs. The optional ABI string applies to all items in the block (inherited at splice). The Rust-2024 `unsafe extern` marker is accepted with no extra semantics.

Examples:

```logos
unsafe extern "C" { fn puts(s: *const u8) -> i32; }
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1209-L1227`

### `item.extern.block-item` — Extern block item (fn / static)

Inside an extern block, items use bare `fn IDENT(params [, ...]) [-> T] ;` (no `extern` keyword; trailing `, ...` makes it variadic) or `static [mut] IDENT : T ;`. The produced extern fn carries no ABI of its own; an extern static with no value is marked external (no initializer).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1228-L1243`

### `item.extern.fn-def` — Standalone extern fn declaration

`extern ["ABI"] fn IDENT(params [, ...]) [-> T] ;` declares a single FFI function carrying its ABI string verbatim. A trailing `, ...` makes it variadic. Omitting the ABI string selects the default (Logos-internal) calling convention.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1209-L1216`, `tools/peg_gen/grammars/logos.peg#L1244-L1255`

## `extern-block`

### `item.extern-block.abi-default-to-children` — extern block applies its ABI as a default

`extern "ABI" { extern_fn* }` splices its function items into the module item stream; the block ABI applies as a default to children that do not specify their own ABI. An omitted block ABI is the default Logos-internal ABI.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L317`

## `fn`

### `stmt.fn.nested-lifts-to-toplevel` — Nested function lifted to a free function

A `fn name(params) [-> T] { body }` at statement position is lifted to a top-level free function (gensym name); the local name is bound as a fn-ptr `let`. A nested fn does not capture enclosing locals.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L303`

### `item.fn.antiquot-name` — Function with antiquoted name

`[pub] [unsafe] fn #(expr) [<type-params>] ( [params] ) [-> T] block` carries an expr-TOM name (NAME_VAR), valid only inside a quote body; these alts omit the where-clause because NAME_VAR and WHERE share a slot.

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1286-L1293`, `tools/peg_gen/grammars/logos.peg#L1312-L1319`

### `item.fn.def` — Function definition

A function item is `[pub[(vis)]] [unsafe] fn NAME [<type-params>] ( [param_list] ) [-> T] [where-clause] block`. NAME may be IDENT or the contextual keywords `new`/`null`. The where-clause and return type are optional.

Examples:

```logos
pub unsafe fn f<T>(x: T) -> T where T: Copy { x }
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1286-L1335`

### `item.fn.param-list-trailing-comma` — Parameter list trailing comma

A parameter list is `param (, param)* (,)?`, but a trailing comma is forbidden when immediately followed by `...` (the variadic marker), so `, ...` separators are unambiguous.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1337-L1342`

### `item.fn.param-pattern` — Pattern-binding parameters

A parameter may bind an irrefutable pattern: a tuple-destructure `(a, b, ...) : T`, a struct pattern `Name { f, .. } : T`, or a slice pattern `[h, t] : T`. Refutable patterns at the fn boundary are rejected in sema with the same diagnostic as for `let`.

Examples:

```logos
fn f(Point { x, y }: Point) {}
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1356-L1393`

### `item.fn.param-self-shorthand` — Self-receiver / ref-binding parameter shorthand

A parameter may be `&[mut] IDENT` (reference binding, type elided), `ref IDENT : T`, or `mut IDENT : T` (mutable local binding, mutability invisible to callers).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1344-L1355`

### `item.fn.param-variadic` — Variadic parameter

`IDENT : T ...` marks a variadic parameter (IS_VARIADIC); plain `IDENT : T` is the ordinary typed parameter.

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1379-L1382`

### `item.fn.nested` — Nested function statement

A `fn name(params) [-> T] { ... }` at statement position is a nested function: its body is lifted to a top-level free function and the local name binds a fn-ptr value. A nested fn captures nothing; reads of enclosing locals are rejected (use a closure instead).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1829-L1837`

## `static-fn`

### `item.static-fn.def` — Static (associated) function definition

`[pub] static [unsafe] fn NAME [<params>] (params) [-> T] { ... }` defines an associated/free function with no `self` receiver; its own optional type-parameter list follows the name, matching instance/free fn generics. The name may be the `new` keyword.

Examples:

```logos
static fn make<T>(x: T) -> Self { ... }
pub static fn new() -> Self { ... }
```

*Divergence:* `static fn` spelling for associated (no-self) functions; Rust uses an `fn` without a `self` parameter inside an impl.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1067-L1093`

## `template`

### `metaprog.template.decl` — Template declaration

`template <item>` wraps a struct/enum/datatype/trait/impl/fn declaration as inert data (an AST blob) rather than a real binding; the inner names are never registered, so referencing the template as a type yields an unknown-type diagnostic. Templates are consumed by metafunctions via apply/metacall.

Examples:

```logos
template struct Pair<A,B> { a: A, b: B }
```

*Divergence:* No Rust equivalent.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L604-L612`

## `instantiate`

### `mono.instantiate.decl` — Explicit instantiation root-pin

`[pub] instantiate <type_ref> ;` materializes the named generic instance as a monomorphization root: all its inherent and trait methods become roots, transitively pulling everything they call. `pub instantiate` additionally marks the instance as part of the package's public API surface.

Examples:

```logos
instantiate Vec<i32>;
pub instantiate Foo<T>;
```

*Divergence:* No Rust equivalent; analog of C++ `template class Foo<int>;`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L591-L595`

## `where`

### `item.where.clause` — Where clause

`where where_pred (, where_pred)*`. A predicate is `<subject> : trait_bound (+ trait_bound)*` where subject is an associated-type ref, a reference type (`&T`, incl. `for<'a> &'a T`), or a plain type-param; or it is a bare type_param.

Examples:

```logos
where T: Clone + Send, &T: Into<U>
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1257-L1271`

## `def`

### `trait.def.modifiers` — Trait definition modifiers and supertraits

`[pub] [auto|unsafe] trait NAME [<params>] [: super + super ...] { items }` defines a trait; the supertrait list after `:` is a `+`-separated list of trait bounds. `auto` and `unsafe` are mutually-exclusive leading modifiers. Generic params, supertraits, both, or neither may be present.

Examples:

```logos
trait Foo: Bar + Baz { }
auto trait Send { }
unsafe trait Sync { }
pub trait Iterator<T> { }
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L819-L876`, `tools/peg_gen/grammars/logos.peg#L828-L829`

## `never`

### `type.never.bang` — Never type `!`

`!` is a type (the never type), parsed as a type reference named `!`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1719-L1720`

## `tuple`

### `pat.tuple.elem-rest` — Tuple-pattern rest element

A tuple-pattern element may be `..` (rest, converted to `_` wildcard skips preserving fixed arity) or an or-pattern of sub-patterns.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2015-L2026`

### `pat.tuple.shape` — Tuple pattern

`(a, b, ...)` is a tuple pattern (≥2 elements) admitting a `..` rest at any single position; `(x,)` (trailing comma) is a 1-tuple pattern, distinguished from a parenthesised pattern `(x)`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2228-L2236`

### `type.tuple.multi` — Tuple type

A tuple type is `(T1, T2, ...)` with ≥2 comma-separated element types (optional trailing comma), or a 1-element tuple `(T,)` requiring the trailing comma.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1732-L1735`

### `type.tuple.unit` — Unit type `()`

`()` denotes the unit type, the empty tuple type.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1722-L1723`, `tools/peg_gen/grammars/logos.peg#L1816-L1817`

### `type.tuple.variadic-arity` — Variadic-arity tuple target `(A...)`

`(A...)` is a variadic-arity tuple type naming pack-typevar A; used as an impl target `impl<A...> Trait for (A...)`. Resolves to a Tuple type with one variadic element naming A.

*Divergence:* Logos addition: variadic tuple impls (no direct Rust equivalent).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1726-L1731`

## `ptr`

### `type.ptr.raw` — Raw pointer type

`*const T` is an immutable raw pointer and `*mut T` is a mutable raw pointer to T.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1746-L1749`

### `type.ptr.raw-slice` — Raw fat-pointer to slice

`*const [T]` and `*mut [T]` are raw fat pointers to a slice, sharing the `{*const T, usize}` ABI of `&[T]` but without borrow-check guarantees. These alternatives precede plain pointer/array forms so the bare `[T]` parses.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1737-L1745`

### `type.ptr.zoned` — Zoned raw pointer `*zoned [mut] T`

`*zoned T` / `*zoned mut T` is a zoned raw pointer (Ref-arm self-relative at rest; deref/assign runs the storage↔compute bridge). `zoned` is a contextual keyword recognized only in pointer position (a bare IDENT after `*`), validated as NAME=="zoned" by sema; it is not globally reserved.

*Divergence:* Logos addition (F3 ref-repr design): zoned pointers, no Rust equivalent.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1750-L1759`

## `array`

### `type.array.size-from-metacall` — Array size from metacall

`[T; metacall { ... }]` permits a compile-time metacall block as the array size expression.

*Divergence:* Logos: comptime sizing via explicit metacall (see explicit-metacall design).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1769-L1770`

### `type.array.size-from-pack` — Array size from variadic pack length

`[T; P...(P)]` sizes the array from a variadic pack length; lowered to symbolic array-size-var `__sizeof_pack:P` and resolved at monomorphization.

*Divergence:* Logos addition: pack-length array sizing.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1762-L1768`

### `type.array.sized` — Fixed-size array type `[T; N]`

`[T; N]` is a fixed-size array type where N is an integer literal or an identifier (const generic). Size-bearing forms are matched before the unsized fallback.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1761-L1772`

### `expr.array.literal-forms` — Array literal and fill forms

Array literals: element list `[e1, e2, …]` and fill form `[value; N]` where N is an integer literal, a named const, `sizeof...(P)` (variadic pack length), or a `metacall` block. The fill form is preferred over the list form to resolve ambiguity.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2863-L2873`, `tools/peg_gen/grammars/logos.peg#L2703-L2704`

## `slice`

### `type.slice.unsized-only-behind-pointer` — Unsized slice [T] cannot appear by value

The bare unsized slice type `[T]` (Kind::UnsizedSlice) may not appear by value; it is only legal behind `&`/`*const`/`*mut` (canonicalised to a sized Slice) or as a `T: ?Sized` substitution.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L304`

### `type.slice.ref` — Slice type

`&[T]` and `&mut [T]` are slice types (fat pointer: ptr + len); an explicit lifetime `&'a [T]` / `&'a mut [T]` is accepted (captured but not distinctly enforced).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1618-L1629`

### `pat.slice.elem-rest` — Slice-pattern rest binding

A slice-pattern element may be `name @ ..` (binds the trailing/middle sub-slice to a name), `..` (anonymous rest), or a regular sub-pattern.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2004-L2013`

### `pat.slice.shape` — Slice pattern

`[elems]` / `[]` match a slice/array by element patterns.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2143-L2147`

### `type.slice.unsized` — Unsized slice type `[T]`

Bare `[T]` (no size) is the unsized slice type. The size-bearing array forms are tried first, so `[T; N]` always wins over the unsized fallback.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1764-L1774`

## `ref`

### `type.ref.double-ref-nesting` — Double reference types desugar to nested references

`&&T` resolves to a nested reference `&(&T)`; `&&mut T` resolves to `&(&mut T)`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L286-L287`

### `type.ref.borrow` — Reference types

`&T`, `&mut T`, `&'a T`, `&'a mut T` are safe borrow-checked reference types. `&&T` / `&&mut T` (no whitespace, tokenized as AND) denote double-references; arbitrary-depth `& & … T` stacks are accepted at type position.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1631-L1655`

### `type.ref.ordered-choice` — Type-reference ordered choice

A type reference resolves by ordered choice: antiquot, typeof, cfg-slot-assoc, cfg-slot, writ-array, writ-map, pointer, array, slice, tagged, dyn, reference, impl-Trait, unit, never, closure, fn-pointer, tuple, paren, qualified-assoc, assoc-type-ref, then simple type. Associated-type forms precede simple_type so `T::Item` matches before `T`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1421`

### `pat.ref.binding-mode` — ref / ref mut / mut bindings

`ref x` binds the matched place by shared reference; `ref mut x` by mutable reference; `mut x` introduces a fresh mutable binding by value.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2053-L2063`

### `pat.ref.reference-pattern` — Reference patterns

`&pat` and `&mut pat` match a reference, peeling one scrutinee reference layer. `&&pat` / `&&mut pat` (lexed as `AND`) peels two layers, producing nested reference patterns.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2064-L2085`

### `type.ref.dotted-path` — Fully-qualified non-generic type path

A fully-qualified non-generic type in type position is written `pkg.path.Type` (dotted); the last path segment is the type. Matched before bare-IDENT alternatives so the whole dotted form is claimed. The generic dotted form `pkg.path.Type<A>` is not supported (use a `use` import + short name).

*Divergence:* Logos path model: `.` for package/module path, `::` for items.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1805-L1813`

### `type.ref.metavar` — Metavariable type reference

`#Ident` and `#(expr)` are type references whose name is supplied by a metaprogram variable/expression rather than a literal identifier.

*Divergence:* Logos metaprogramming addition.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1801-L1804`

## `dyn`

### `type.dyn.trait-object` — dyn trait-object type

`dyn Trait`, `&dyn Trait`, `&mut dyn Trait` (optionally lifetime-annotated and HRTB-quantified `for<'a>`) form trait objects (fat pointer: data + vtable). Type args use `<...>`; Fn-family `dyn Fn(args)[-> R]` puts args in PARAMS/return in RET_TYPE. Trailing `+ Ident` (auto-trait, e.g. Send/Sync) and `+ 'lt` (lifetime) bounds are accepted; auto-trait bounds are enforced at unsize coercion, lifetime bounds are recorded but not enforced.

Examples:

```logos
&dyn Display + Send
Box<dyn for<'a> Fn(&'a i32) -> i32>
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1496-L1616`

## `tagged`

### `type.tagged.thin-pointer` — tagged thin pointer type

`&tagged<T> Name` is a thin tag-dispatched pointer: a type_code tag is stored in memory before the object, and call sites read the tag, look up the dispatch table, and call indirectly.

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1490-L1494`

## `impl-trait`

### `type.impl-trait.param` — impl Trait type

`impl Trait`, `impl Trait<args>`, and `impl Fn(args) [-> R]` are accepted in type position; an impl-Trait parameter desugars to a synthetic generic parameter bounded by the same trait (Fn-family args→PARAMS, return→RET_TYPE, generic args→TYPE_PARAMS).

Examples:

```logos
fn f(x: impl Display) {}
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1395-L1414`

## `closure`

### `type.closure.type` — Closure type

`|T1, T2| -> R` is a closure type used in parameter annotations; the zero-arg form `|| -> R` is accepted (the `||` token is split).

*Divergence:* A6: Rust spells closures via Fn-family bounds; Logos has a dedicated `|..|->R` closure type syntax.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1657-L1664`

## `fn-ptr`

### `type.fn-ptr.type` — Function-pointer type

`fn(T1,T2) -> R` is a bare function-pointer type. Qualifiers/prefixes are accepted: `unsafe fn(...)` (IS_UNSAFE), `extern "ABI" fn(...)` (ABI threaded to the calling convention), and `for<'a> fn(...)` (HRTB binders captured for future region inference).

Examples:

```logos
extern "C" fn(i32) -> i32
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1666-L1715`

## `paren`

### `type.paren.unwrap-to-inner` — Parenthesized type is structurally its inner type

A parenthesized type `(T)` is unwrapped to its inner type `T`; `(T)` and `T` are structurally identical.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L290`

### `type.paren.unwrap` — Parenthesized type

`( T )` is a parenthesized type, distinct from `()` (unit), `(T,)` (1-tuple) and `(T1,T2)` (n-tuple); sema unwraps it to its inner type T.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1423-L1426`

## `cfg-slot`

### `type.cfg-slot.projection` — Type-level cfg-slot projection

`<type:CFG.path>` projects, at mono-time, the type stored at a path within a WritStatic-typed type-level binding. Path steps are `.IDENT` (string key), `.INTEGER` (int key) and `.[INTEGER]` (array index). At least one path step is required. `<type:CFG.SLOT>::Assoc` projects an associated type on the slot base.

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1428-L1449`

## `antiquot`

### `type.antiquot.quote-ty-only` — Type antiquotation

`$ident` in type position is a type antiquotation valid only inside `quote_ty! { ... }`; resolving it elsewhere is an error.

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1456-L1459`

## `typeof`

### `type.typeof.expr` — typeof type

`typeof(expr)` is the compile-time type of expr; the expression is not evaluated.

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1461-L1463`

## `generic`

### `type.generic.instantiation` — Generic type instantiation `T<...>`

`Name<arg, ...>` (optional trailing comma) instantiates a generic type. The type name may also be a metavariable: `#Ident<...>` or `#(expr)<...>`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1797-L1815`

### `type.generic.type-arg-kinds` — Generic type-argument kinds

A generic type argument may be a lifetime `'a` (stored as LIFETIME_PARAM and skipped during concrete-type resolution), a pack expansion `Ident...`, an antiquote `$Ident` or `$Ident...`, an integer literal (optionally negated), a writ literal, or a type.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1776-L1795`

### `grammar.generic.hrtb-binder` — HRTB for<...> binder parsed then dropped

hrtb_binder ::= 'for' '<' LIFETIME (',' LIFETIME)* ','? '>' may prefix any trait_bound. Lifetimes are not tracked structurally, so for<'a> Trait<...> is semantically equivalent to Trait<...> (binder parsed into a disposable head).

*Divergence:* Lifetimes not structurally tracked: HRTB binder is accepted but discarded (Rust enforces it).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3077-L3108`

### `grammar.generic.lifetime-param-outlives` — Lifetime parameter with outlives bound

lifetime_param ::= LIFETIME (':' LIFETIME ('+' LIFETIME)*)? ; a lifetime parameter optionally carries one or more outlives bounds.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3141-L3145`

### `grammar.generic.type-arg-list` — Type argument list

type_arg_list ::= type_or_lt_arg (',' type_or_lt_arg)* ','? ; generic instantiation argument list (e.g. Vec<i32>).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3183-L3185`

### `grammar.generic.type-param-default` — Type parameter default

A type parameter may carry a default via 'IDENT (: bounds)? = type_ref'; the default type applies when the argument is omitted at instantiation.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3174-L3177`

### `grammar.generic.type-param-forms` — Type parameter forms

type_param admits: lifetime_param; 'IDENT: lifetime_param (+ lifetime_param)*' (type-outlives); ptr/arr specialisation patterns; const params 'const IDENT: T', 'const IDENT...: T' (variadic), 'const #IDENT: T'; variadic type param 'IDENT... (: bounds)?'; metavar '#IDENT (: bounds)?'; 'IDENT: bounds (= default)?'; 'IDENT = default'; or bare 'IDENT'. A repeat-group '#(type_param), *' expands variadically.

*Divergence:* Logos additions: variadic type/const params ('...'), metavar params ('#'), repeat-group expansion (no Rust equivalent).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3147-L3181`

### `grammar.generic.type-param-list` — Type parameter list

type_param_list ::= '<' type_param (',' type_param)* ','? '>' ; also reused for specialisation patterns (<i32>, <*T>, <[T;4]>). Distinguishing type-var vs concrete is deferred to sema, not grammar.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3073-L3075`

## `binop`

### `expr.binop.precedence-cascade` — Binary operator precedence

Binary precedence, lowest→highest: logical (`&&`/`||`) < comparison (`==` `!=` `<=` `>=` `<` `>`) < bitor `|` < bitxor `^` < bitand `&` < shift (`<<` `>>`) < additive (`+` `-`) < multiplicative (`*` `/` `%`) < `as`-cast < unary. All binary levels are left-associative.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2585-L2636`, `tools/peg_gen/grammars/logos.peg#L2602-L2606`

## `cmp`

### `expr.cmp.no-chained-comparisons` — Chained comparisons rejected

A comparison chain with 2+ comparators in a row (`a < b < c`) is rejected at sema with the diagnostic "chained comparisons not supported; use `a < b && b < c`". It parses (CHAINED_CMP) but is not a valid program.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L289`

### `expr.cmp.non-chainable` — Comparison operators are non-chainable

Comparison operators are non-chainable: at most one comparison per level is well-formed. A chain of 2+ comparators (e.g. `a < b < c`) is parsed as a distinct CHAINED_CMP node so sema can reject it with a dedicated diagnostic rather than a generic syntax error.

*Divergence:* Rust-conformant outcome (chained comparison is an error); Logos detects it grammatically for a better diagnostic.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2589-L2600`, `tools/peg_gen/grammars/logos.peg#L2424-L2431`

## `unary`

### `expr.unary.operator-set` — Unary / prefix operators

Prefix unary operators (highest binding among operators): `*` deref, `&` borrow, `&mut` mutable borrow, `-` negate, `!` not. `&&v` (lexed as the AND token) means a double reference and lowers to nested address-of.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2648-L2656`

## `cast`

### `expr.cast.byte-string-to-array` — Byte-string literal is [u8; N]

A byte-string literal `b"..."` at expression position lowers to an array literal of type `[u8; N]` (escapes decoded).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L302`

### `expr.cast.as-chain` — as-cast chaining

`as`-casts (`v as T`) bind below unary operators and chain left-associatively, so `x as T1 as T2` folds as `(x as T1) as T2`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2638-L2646`, `tools/peg_gen/grammars/logos.peg#L2632`

## `postfix`

### `expr.postfix.chain` — Postfix operator chain

A primary expression may be followed by zero or more left-associative postfix suffixes: method call `.m(args)` (optionally `.m::<T>(args)` with explicit turbofish type args), expression-callee invocation `e(args)`, field read `.field`, tuple index `.N`, indexing `[i]`, and the try operator `?`. Chains parse left-to-right (`a.b.c`, `a.f().b`).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2658-L2694`

## `try`

### `expr.try.operator` — Try operator

Postfix `e?` is the try operator; it propagates the error/none case of a Result/Option-like value and yields the success payload.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2692-L2693`

## `index`

### `expr.index.read` — Index expression

`e[i]` is a postfix index-read; with a range index (`s[a..b]`, `s[a..]`, `s[..b]`, `s[..]`) it produces a slice.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2690-L2691`, `tools/peg_gen/grammars/logos.peg#L2394-L2396`

## `invoke`

### `expr.invoke.expression-callee` — Expression-as-callee invocation

`(expr)(args)` invokes the value produced by `expr` as a callee, routed through closure-call or fn-ptr-call.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L298`

## `turbofish`

### `expr.turbofish.generic-ref` — Turbofish generic reference and static call

`IDENT::<T,…>` is a generic reference (explicit type arguments to a function/item). `IDENT::<T,…>::METHOD` is a static call on the type-applied receiver.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2756-L2760`

## `macro`

### `expr.macro.fn-style-call` — Function-style macro invocation

Function-style macros invoke as `name!(…)`, `name![…]`, or `name!{…}`; the contents between balanced delimiters are captured as raw source text and re-interpreted at sema time per the callee's macro kind (#[fn_macro] re-parses as an expression list; #[token_macro] lexes as a TokenStream). In no-struct-lit (condition) position the brace form `name!{…}` is excluded.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2743-L2754`, `tools/peg_gen/grammars/logos.peg#L2550-L2559`

## `fn-macro`

### `metaprog.fn-macro.expr-and-item-forms` — Function-style macros resolve to #[fn_macro] fns

`name!(args)` / `name![args]` resolves CALLEE against `#[fn_macro]` functions with argument ASTs passed as expression blobs; `name!{...}` at item position routes through item-splice (callee returns an item list).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L293-L294`

### `metaprog.fn-macro.item-position` — Item-position fn-macro invocation

`IDENT ! { ... }` at item position invokes a function-like macro whose body is captured as raw text; the macro must use brace delimiters at item position (parens/brackets are reserved for expression position). The callee returns a list of items.

Examples:

```logos
my_macro! { struct A; }
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L573-L574`

## `metacall`

### `metaprog.metacall.explicit-comptime-call` — metacall executes a call at compile time

`metacall <call_expr>` executes the wrapped call at compile time; at module-item position the JIT'd callee returns an item blob whose emitted items are spliced into the module.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L266`, `tools/peg_gen/grammars/logos.peg#L277`

### `metaprog.metacall.forms` — metacall expression forms

`metacall` accepts a block (`metacall { … }`), a parenthesized expression (`metacall (e)`), or a call expression (`metacall f(…)`), and evaluates its argument at compile time.

*Divergence:* Logos addition: explicit compile-time evaluation operator (no implicit const-eval).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2731-L2736`

### `metaprog.metacall.item-position` — Item-position metacall

`metacall <call-expr> ;` at module top level invokes a metafunction that returns an item-blob; the produced items are spliced into the program at that position.

Examples:

```logos
metacall gen_items::<Foo>();
```

*Evidence:* `tools/peg_gen/grammars/logos.peg#L581-L582`

## `quote`

### `metaprog.quote.typed-ast-literals` — quote_* produce typed AST/Type literals

`quote_item! { item* }`, `quote_expr! { expr }`, and `quote_ty! { type }` are typed literals yielding item-list, expression, and Type AST values respectively. Antiquotation `$ident` (a Type-valued binding) and `$ident...` (an Array<Type> binding) are legal only inside `quote_ty!`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L268-L274`

## `struct-lit`

### `expr.struct-lit.field-init` — Struct field initializers and shorthand

A struct field initializer is `name: expr` or the shorthand `name` (FIELD_SHORTHAND, binding the in-scope variable of that name). Tuple-struct fields may be initialized by their numeric name `S { 0: a, 1: b }` since fields of `struct S(T0,T1)` are named "0"/"1".

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2843-L2861`, `tools/peg_gen/grammars/logos.peg#L2851-L2855`

### `expr.struct-lit.forms` — Struct literal forms

Struct literals: `T { f: e, … }`, generic `T::<A,…> { f: e, … }`, and functional-update `T { f: e, .. base }` / `T { .. base }` / `T { .. base, f: e }`. Explicit fields always override the base regardless of field order.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2818-L2838`, `tools/peg_gen/grammars/logos.peg#L2823-L2831`

## `enum-lit`

### `expr.enum-lit.forms` — Enum variant literal forms

Enum variants are written `E::V` (unit), `E::V(args)` (tuple payload), `E::V { f: e, … }` (struct-shape payload), with optional turbofish `E::V::<T,…>`. The qualified-as form `<T as Trait>::V` and dotted-package-prefix form `pkg.path.E::V` are also accepted. Struct-shape variant fields are resolved by name to positional indices.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2787-L2816`

## `comprehension`

### `expr.comprehension.list-and-map` — List and map comprehensions

List comprehension `[expr for x in iter (if pred)?]` and map comprehension `{kexpr: vexpr for x in iter (if pred)?}` produce a collection by iterating `iter`, binding `x`, optionally filtering by `pred`.

*Divergence:* Logos addition: Python-style comprehensions; not present in Rust.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2875-L2885`

## `control`

### `expr.control.break-continue-return-in-value-position` — break/continue/return usable in expression position

`break`, `continue`, and `return` may appear in expression position (Never-typed); the bare `return` form carries no value. They type-check as `!`/Never so surrounding expressions accept them.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L299-L301`

### `expr.control.never-position` — Diverging control-flow as expression

`return [e]`, `break [label] [e]`, and `continue [label]` may appear in expression position with type `!` (never), permitting forms like `let x = if c { v } else { return e };` and `_ => break`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2716-L2728`

## `block`

### `grammar.block.brace` — Block

A block is `{ stmt* }`: a brace-delimited sequence of zero or more statements.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1821-L1823`

### `expr.block.as-value` — Block / control constructs as expressions

`{ … }` blocks, `unsafe { … }`, `loop { … }`, `if … {} else {}`, and `match … {}` are all primary expressions producing a value (block/loop yield their tail/break value).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2711-L2715`

## `unsafe`

### `expr.unsafe.block` — Unsafe block

`unsafe { ... }` is an unsafe block whose body is an ordinary block.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1825-L1827`

## `if`

### `expr.if.let-chain` — if let-chain

An `if` may chain conditions with `&&` where the first segment is a `let` binding: `if let P = e && seg (&& seg)* { THEN } [else …]`. Each subsequent seg is either `let P = e` or a bare condition (level `cmp_expr_ns`). The chain requires the first segment to be a `let` and at least two `&&`-joined segments. Desugars to nested matching: all let-patterns must match and all conditions hold for THEN.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2342-L2381`

### `expr.if.no-struct-lit-cond` — if/while/for condition restricts struct literals

In `if`/`while`/`for` condition position the scrutinee uses the no-struct-lit expression grammar (`expr_ns`): a top-level `IDENT { … }` is NOT parsed as a struct literal, so the brace opens the control-flow block. A struct literal in condition position must be parenthesized. Restriction applies only to the top-level primary; inside parens/brackets/calls full `expr` resumes.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2411-L2417`, `tools/peg_gen/grammars/logos.peg#L2512-L2516`

### `expr.if.single-let-guard` — if-let with single guard condition

`if let P = e && cond { THEN } [else ELSE]` (single let plus trailing condition) desugars to `match e { P if cond => THEN, _ => ELSE }`; the let scrutinee is parsed at `cmp_expr_ns` so the `&&` belongs to the guard.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2357-L2364`

## `if-let-chain`

### `expr.if-let-chain.fall-to-else-on-failure` — if-let chain falls to else on any segment failure

`if let P1 = e1 && let P2 = e2 && cond { THEN } else { ELSE }` evaluates a flat sequence of refutable binds and boolean conditions left-to-right; any failed bind or false condition takes the ELSE branch.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L318-L320`

## `range`

### `pat.range.char` — Char patterns

`'a'` matches a char literal; `'a'..='z'` matches an inclusive char range.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2190-L2194`

### `pat.range.integer` — Integer range patterns

Integer range patterns include closed inclusive `lo..=hi`, closed exclusive `lo..hi`, and half-open forms `a..` (RangeFrom → [a, TYPE_MAX]), `..=b` (RangeToInclusive → [TYPE_MIN, b]), `..b` (RangeToExclusive → [TYPE_MIN, b-1]). Each endpoint may be negated (`-N`). Open bounds clamp to the scrutinee type's min/max.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2148-L2185`

### `expr.range.family` — Range expressions

Range value-expressions: `lo..hi` (half-open), `lo..=hi` (inclusive), `lo..` (from), `..hi` (to), `..=hi` (to-inclusive), `..` (full). An omitted side leaves the corresponding bound unspecified. Sema lowers each to a stdlib Range struct implementing `Iterator`. Range sits at the top of the value-expression precedence cascade (below it: logical operators).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2392-L2409`

## `writ`

### `type.writ.lit-and-array-map` — Writ literal / typed array / typed map types

`@{...}` at type position is a WritStatic value literal type (LIT_WSTATIC). `<Elem>[]` is a Writ typed-array type and `<K[,V]>{}` is a Writ typed-map type (used in `as` casts).

*Divergence:* A6

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1451-L1473`

### `pat.writ.container` — Writ map/array patterns

`@{ key: pat, ... }` / `@{}` match writ maps; `@[ elem, ... ]` / `@[]` match writ arrays. Array elements admit a trailing `..` to match length ≥ n; map keys are string literals.

*Divergence:* Logos addition: Writ container patterns.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2028-L2041`, `tools/peg_gen/grammars/logos.peg#L2113-L2120`

### `pat.writ.scalar` — Writ scalar patterns

`@null`, `@true`/`@false`, `@N`/`@-N`, and `@"str"` are writ scalar patterns matching writ null, bool, integer, and string values respectively.

*Divergence:* Logos addition: Writ data-substrate patterns.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2092-L2106`

### `pat.writ.typed-container` — Writ typed map/array patterns

`@<T>{..}`, `@<T,R>{..}`, and `@<T>[..]` are typed writ map and array patterns annotating the matched container's element type(s).

*Divergence:* Logos addition: Writ typed-container patterns.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2107-L2112`

### `expr.writ.sdn-literal` — Writ SDN literals

Writ structured-data literals use the `@` sigil: `@{k:v,…}` map, `@[v,…]` array, `@"s"` string, `@42`/`@-1` int, `@<float>` float, `@true`/`@false` bool, `@null`. Typed forms `@<Elem>[…]` (dense array) and `@<K,V>{…}` / `@<K>{…}` (typed map). Comprehension forms `@[expr for x in iter (if p)?]` and `@{k:v for …}`. Only the outermost literal needs the `@` sigil; inner values are plain.

*Divergence:* Logos addition: Writ self-describing data-notation literals.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2887-L2923`

### `grammar.writ.array-literal` — Writ array literal

> ⚠️ **Conflict:** this id is also defined in another grammar (logos, writ); the two are distinct rules. This instance is from `grammar/logos/rule-writ_map.json`.

writ_array ::= '[' (writ_val (',' writ_val)* ','?)? ']' ; a bracket-delimited, comma-separated, optionally trailing-comma list of Writ values.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2927-L2928`

### `grammar.writ.capture-placeholders` — Writ runtime capture placeholders

Inside a Writ literal, '${' expr '}' captures an arbitrary expression (WRIT_CAP_EXPR) and '$' IDENT captures a named binding (WRIT_CAP_IDENT) as a runtime value.

*Divergence:* No Rust analogue; Writ interpolation.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2949-L2950`

### `grammar.writ.entry-key-kinds` — Writ entry keys

writ_entry ::= (STRING | '-' INTEGER | INTEGER) ':' writ_val ; a map key is a quoted string, a negative integer, or a non-negative integer. A '-' INTEGER key carries LO_NEG.

*Divergence:* No Rust analogue; Writ data-literal grammar.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2931-L2936`

### `grammar.writ.map-literal` — Writ map literal

> ⚠️ **Conflict:** this id is also defined in another grammar (logos, writ); the two are distinct rules. This instance is from `grammar/logos/rule-writ_map.json`.

writ_map ::= '{' (writ_entry (',' writ_entry)* ','?)? '}' ; a brace-delimited, comma-separated, optionally trailing-comma list of key:value entries.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2924-L2925`

### `grammar.writ.nested-at-optional` — Optional @ on nested Writ aggregates

A nested writ_map / writ_array inside a writ_val may optionally be prefixed by '@'; '@'-prefixed and bare forms are equivalent.

*Divergence:* No Rust analogue; Writ literal nesting.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2951-L2955`

### `grammar.writ.scalar-values` — Writ scalar values

writ_val scalars: RAW_STRING/STRING -> WRIT_STR; FLOAT -> WRIT_FLOAT; '-' INTEGER -> WRIT_NEG_INT; INTEGER -> WRIT_INT; 'true'/'false' -> WRIT_BOOL; 'null' -> WRIT_NULL.

*Divergence:* No Rust analogue; Writ scalar literals.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2956-L2963`

### `grammar.writ.type-literal` — Writ embedded type value

writ_val may be '<' 'type' ':' simple_type '>' embedding a Logos Type as a first-class Writ value (WRIT_TYPE_LIT); any simple_type (e.g. generic instantiations Vec<u8>, Result<T,E>) is accepted.

*Divergence:* No Rust analogue; type-as-value embedding.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2941-L2948`

### `grammar.writ.type-slot-path` — Writ CFG type-slot

writ_val may be '<' 'type' ':' IDENT path_step+ '>' producing a CFG_SLOT_TYPE (slot extraction keeping an IDENT-only head followed by path steps).

*Divergence:* No Rust analogue; Writ embedded-type slot.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2945-L2946`

## `pack`

### `expr.pack.sizeof-and-expand` — Variadic pack size and expansion

`P...(N)` yields the length of variadic pack `P` (sizeof-pack), and `P...` in expression position expands the pack `P`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2737-L2738`, `tools/peg_gen/grammars/logos.peg#L2774`

## `sizeof-pack`

### `intrinsic.sizeof-pack.length-of-type-pack` — sizeof...(T) yields pack length

`sizeof...(T)` is a value-position expression yielding the length of the type pack `T` as a `u64`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L271`

## `offset-of`

### `intrinsic.offset-of.compile-time-byte-offset` — offset_of! yields compile-time field offset

`offset_of!(Type, field)` evaluates at compile time to the byte offset of `field` within `Type`'s ABI layout, as an i64 constant.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L323`

### `intrinsic.offset-of.form` — offset_of! intrinsic

`offset_of!(Type, field)` yields the byte offset of `field` within `Type`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2729-L2730`

## `heap`

### `divergence.heap.no-class-new-delete` — No C++-style class/new/delete

The language has no `class` declaration, `new` expression, or `delete` statement. Heap allocation is expressed via `Box` (and other library owning types), not a built-in `new`/`delete` pair.

*Divergence:* Logos addition/removal vs C++; Rust-conformant (Rust also has no class/new/delete).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L165-L166`

## `for`

### `stmt.for.each` — For-each loop

`for x in iter { }` iterates over an iterable. The loop variable may be a simple identifier (fast path) or a full destructuring pattern `for (a,b) in v { }`, in which case the pattern is bound against each element.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1878-L1886`

### `stmt.for.range` — For-range loop

`for i in lo..hi { }` iterates the exclusive integer range; `for i in lo..=hi { }` iterates the inclusive range.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1868-L1877`

## `while`

### `stmt.while.forms` — while and while-let

`while cond { }` is a conditional loop; `while let PAT = e [&& guard] { }` is a while-let loop; `while LET-CHAIN { }` is a while-let chain (≥2 segments starting with let), ordered first so it is not shadowed.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2277-L2287`

## `loop`

### `stmt.loop.infinite` — loop block

`loop { ... }` is an unconditional loop.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1888-L1894`

### `stmt.loop.labeled` — Labeled loop

`'label: for/while/loop { ... }` attaches a lifetime-syntax label to a loop, targetable by `break 'label` / `continue 'label`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1891-L1902`

## `let`

### `pat.let.refutability-checked` — let with complex pattern checked for refutability

`let <pattern> = expr;` for a pattern beyond a simple ident/tuple is an irrefutable destructure: sema checks the pattern is irrefutable and lowers it via `match`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L285`

### `stmt.let.forms` — let bindings

`let` supports: tuple destructure `let (a,b) [: T] = e;`, `let ref x [: T] = e;` (sugar for `let x = &e;`), `let mut x [: T] [= e];` (mutable, type-only declaration without init allowed when typed), `let x [: T] [= e];`, and `let PAT = e;` (irrefutable full pattern, refutability checked by sema).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2246-L2275`

## `let-else`

### `stmt.let-else.form` — let-else statement

`let PAT = expr else { block };` binds a refutable pattern; on match failure the else block runs (which must diverge).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2242-L2244`

## `return`

### `expr.return.implicit-tail` — Tail expression is implicit return

A trailing expression with no terminating `;` at statement position synthesizes an implicit `return expr` for a non-void function.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L291`

### `stmt.return.form` — return statement

`return [expr];` returns from the enclosing function with an optional value; may be terminated by `;` or `,`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2289-L2290`

## `break`

### `stmt.break.forms` — break

`break;`, `break expr;`, `break 'label;`, and `break 'label expr;` are all valid; a value and/or a target label are optional. A bare `break` may also be terminated by `,`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1904-L1911`

## `continue`

### `stmt.continue.forms` — continue

`continue;` and `continue 'label;` are valid; the target label is optional. A bare `continue` may also be terminated by `,`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1913-L1916`

## `assign`

### `stmt.assign.destructuring-into-places` — Destructuring assignment into existing places

Destructuring assignment `(a,b)=e` / `[a,b]=e` / `S{a,b}=e` writes into EXISTING places (not new bindings), desugared to `let tmp = rhs;` followed by per-place assignments.

*Divergence:* RFC 2909 (Rust-conformant).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L311`

### `stmt.assign.compound-place` — Compound assignment over any place

`PLACE op= expr` applies a compound assignment over an arbitrary place: a bare variable takes the simple-var path; any other place desugars to `place = place op rhs` (or an `*Assign` trait-method call). Bare-deref `*p op= v` is handled separately (it is not an atom).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2317-L2323`

### `stmt.assign.destructure` — Destructuring assignment into existing places

Tuple `(a, b) = e;`, array `[a, b] = e;`, and struct `S { f, .. } = e;` destructuring assignment writes into existing places (RFC 2909). Parsed before expr-statements so a parenthesised/bracketed LHS followed by `=` is recognized.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2306-L2315`

### `stmt.assign.place` — General place assignment

`PLACE = expr;` where PLACE is an arbitrary postfix-chain lvalue (chained index `a[i][j]`, deref+tuple-index `(*p).0`, deeper mixes); sema computes the address and emits a deref-write. Tried after the specialized single/two-level write forms and after bare-variable assignment.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2295-L2304`

### `stmt.assign.simple` — Simple variable assignment

`name = expr;` assigns to a simple variable place.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2292-L2293`

### `expr.assign.compound-op-set` — Compound assignment operators

The compound-assignment operators are `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`. A compound-assign statement is `place OP value ;` where `place` is an atom (postfix-chained lvalue) and `value` is a full `expr`.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2324-L2327`

### `expr.assign.deref-write` — Dereference write statement

`* p = v ;` writes value `v` through dereferenced place `p` (a `unary_expr`). `* p OP v ;` performs compound assignment through a bare dereference and is defined to lower to `*p = *p OP v`.

*Divergence:* Logos addition: distinct DEREF_WRITE/DEREF_COMPOUND statement forms; semantics match Rust place-expression assignment.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2335-L2340`

## `match`

### `stmt.match.arm` — Match arm with optional guard

A match arm is `PAT [if GUARD] => BODY`, where BODY is a block, an expression, or a statement, with an optional trailing comma. The optional `if GUARD` is a guard expression gating the arm.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1930-L1941`

### `stmt.match.scrutinee-form` — Match statement

`match SCRUT { ARM* }` matches a scrutinee against arms. A bare-identifier scrutinee is parsed specially (as a var-ref) so `match e { ... }` does not mis-parse `e {` as a struct literal; complex scrutinee expressions fall through to the general expr form.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1918-L1928`

## `or`

### `pat.or.alternatives` — Or-pattern

A pattern is one or more `pat_single` alternatives separated by `|`, with an optional leading `|`. A variant-payload arg may itself be an or-pattern `Some(A | B)`; a single alternative passes through transparently.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1962-L1978`

## `at`

### `pat.at.binding` — At-binding pattern

`name @ subpat` binds `name` to the value matched by `subpat`. `ref name @ subpat` binds by reference.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2043-L2052`

## `wild`

### `pat.wild.ident` — Identifier / wildcard pattern

A bare identifier is an irrefutable binding pattern (the matched value is bound to the name; `_` is the anonymous wildcard).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2237-L2238`

## `lit`

### `expr.lit.char-is-unicode-scalar` — Char literal is a Unicode scalar

A char literal `'X'` denotes a single Unicode scalar value, decoded to a `u32` scalar.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L292`, `tools/peg_gen/grammars/logos.peg#L296`

### `pat.lit.bool` — Bool patterns

`true` and `false` match the boolean values.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2208-L2211`

### `pat.lit.bytes-rejected` — Byte-string pattern rejected

A byte-string-literal pattern parses but is rejected by sema (pending &[u8] equality-matching codegen).

*Note (uncertainty):* Status is provisional ("until codegen lands"); reflects current compiler behavior.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2200-L2203`

### `pat.lit.float-rejected` — Float-literal pattern rejected

A float-literal pattern parses but is rejected by sema with a diagnostic: float equality matching in patterns is deliberately not supported (IEEE equality semantics undefined).

*Divergence:* Rust deprecated float patterns; Logos rejects them outright.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2195-L2199`

### `pat.lit.integer` — Integer literal patterns

`N` matches an integer literal; `-N` matches a negative integer literal.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2186-L2189`

### `pat.lit.string` — String-literal pattern

A string-literal pattern `"foo"` matches by string equality (lowered to a refutable `str_eq(scrut, "foo")` guard over a wildcard binding).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2204-L2207`

### `pat.lit.unit` — Unit pattern `()`

`()` is the unit pattern, matching the unit value.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2087-L2091`

## `str`

### `pat.str.lowers-to-eq-guard` — String-literal pattern lowers to equality guard

A string-literal pattern `match s { "foo" => ... }` is matched by lowering to a `str_eq` guard.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L312`

## `group`

### `pat.group.paren` — Parenthesised / grouped pattern

`(P)` is exactly P and `(P | Q | ...)` is a grouped or-pattern (inlined into a single or-pattern at that position). `(..)` matches any tuple binding nothing (irrefutable wildcard).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2212-L2227`

## `expr`

### `grammar.expr.array-literal` — Array literal

arr_lit ::= '[' (expr (',' expr)* ','?)? ']' ; a bracket-delimited comma-separated list of expressions with optional trailing comma.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2966-L2967`

### `grammar.expr.call-arg-list` — Call argument list

call_arg_list ::= expr (',' expr)* ','? ; a call site's value-argument list with optional trailing comma.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3187-L3189`

### `grammar.expr.call-free` — Free function call

'IDENT(args)', 'new(args)', and 'null(args)' are plain free-function CALLs by name.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3238-L3243`

### `grammar.expr.call-metavar` — Metavariable call

'#IDENT(args)' and '#(expr)(args)' invoke a callee named by a metavariable (NAME_VAR) or by an evaluated expression, used in metaprogramming-expanded call sites.

*Divergence:* No Rust analogue; metaprogramming callee splice.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3230-L3237`

### `grammar.expr.call-package-qualified` — Package-qualified free-function call

A call 'IDENT path_dot_ident+ '::' IDENT ('::' '<' type_arg_list '>')? '(' call_arg_list? ')'' resolves a free fn by its dotted package path (RECEIVER = first segment, QUAL_PARTS = rest); this disambiguates same-named free fns across packages (e.g. logos.lang.mem::replace vs logos.lang.ptr::replace).

*Divergence:* Logos path model: '.'-separated package path + '::'-item (vs Rust all-'::').

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3191-L3203`

### `grammar.expr.call-static` — Static / associated call

'Type::IDENT(args)' and 'Type::new(args)' are STATIC_CALLs invoking an associated function (including the 'new' constructor) of the receiver type.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3226-L3229`

### `grammar.expr.call-turbofish-method` — Turbofish on the method

'Type::method::<T>(args)' applies type args <T> to the associated method's own generics (distinct from Type::<T>::method); must be matched before plain 'Type::method(args)'.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3220-L3225`

### `grammar.expr.call-turbofish-type` — Turbofish on the type

'Type::<T>::method(args)' is a STATIC_CALL applying type args <T> to the type before selecting an associated fn/method/new; 'Type::<T>(args)' / 'new::<T>(args)' / 'null::<T>(args)' are GENERIC_CALL with the type args on the callee.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3204-L3213`

### `grammar.expr.call-ufcs-qualified` — UFCS qualified-path call

'<Type as Trait>::method(args)' dispatches on the concrete Type; the trait qualifier is consumed and dropped because the type-dispatch already resolves the method.

*Divergence:* Trait qualifier in <T as Tr>::m is dropped (Rust uses it for disambiguation).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3214-L3219`

### `grammar.expr.closure-expr` — Closure expression forms

closure_expr is '|' closure_param_list? '|' or '||' (OR token), optionally preceded by 'move' and optionally followed by '-> type', with a body that is either a block or (tried after block forms) a single expression. '|x| expr' / '|| expr' are brace-less expression-body closures; the body expr stops at the enclosing ',' / ')'.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3004-L3039`

### `grammar.expr.closure-param-untyped` — Closure parameter type may be omitted

closure_param allows the type annotation to be omitted: '|x|' is accepted as well as '|x: T|'. Forms: '&mut IDENT', '&IDENT', 'ref IDENT: T', 'mut IDENT: T', 'mut IDENT', '(pat_binding_list): T', 'IDENT: T', 'IDENT'. The omitted type is inferred from the surrounding fn(T)->R formal at the call site.

*Divergence:* Conformant with Rust closure type-inference.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2979-L3000`

### `grammar.expr.paren-expr` — Parenthesised expression

paren_expr ::= '(' expr ')' ; a single parenthesised expression (not a tuple).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3042-L3043`

### `grammar.expr.tuple-literal` — Tuple literal arity

tuple_lit ::= '(' expr ',' expr (',' expr)* ')' | '(' expr ',' ')' ; a tuple literal requires either a single element with a trailing comma, or two or more comma-separated elements. '(expr)' alone is not a tuple.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2970-L2973`

## `float`

### `pat.float.rejected-at-sema` — Float-literal patterns rejected

A float-literal pattern parses but is rejected at sema (not a valid match pattern).

*Divergence:* Rust also forbids float patterns (deprecated/removed).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L283`

## `metaprog`

### `grammar.metaprog.quote-expr` — quote_expr! macro

quote_expr_expr ::= 'quote_expr' '!' '{' expr '}' ; body is a single expression producing a typed AST (expr-blob) literal.

*Divergence:* No Rust analogue.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3060-L3061`

### `grammar.metaprog.quote-item` — quote_item! macro

quote_item_expr ::= 'quote_item' '!' '{' item* '}' ; body is zero or more item declarations producing a typed AST (item-blob) literal.

*Divergence:* No Rust analogue (Rust uses macro_rules!/proc-macro quote).

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3051-L3052`

### `grammar.metaprog.quote-ty` — quote_ty! macro

quote_ty_expr ::= 'quote_ty' '!' '{' type_ref '}' ; body is a single type expression producing a first-class Type literal (same Type{kind,name,size} shape as type_of::<T>()).

*Divergence:* No Rust analogue.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L3068-L3069`

## `tuple-bind`

### `pat.tuple-bind.let` — Let-binding tuple pattern

A let-binding tuple pattern admits `()` (unit), `..` rest (expanded to the right number of `_` skips), nested tuples `(a, (b, c))`, and identifier bindings. Rest fills remaining positions so names land on the correct tuple slots.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L1943-L1960`

## `variant`

### `pat.variant.fieldless` — Fieldless variant pattern

`E::V` matches a fieldless (unit) enum variant.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2130-L2131`

### `pat.variant.struct-shape` — Enum struct-variant pattern

`E::V { x, y: pat, .. }` / `E::V {}` match a struct-shaped enum variant; field names resolve to variant payload indices.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2123-L2129`

### `pat.variant.tuple` — Enum tuple-variant pattern

`E::V(args)` matches an enum tuple-variant payload; payload args are full nested patterns, possibly including `..` rest and or-patterns. A bare `Foo(a, b)` (no `::`) matches a tuple-struct when the name resolves as a tuple-struct rather than an enum.

*Evidence:* `tools/peg_gen/grammars/logos.peg#L2121-L2122`, `tools/peg_gen/grammars/logos.peg#L2132-L2137`

# Writ data-notation grammar (standalone `writ.peg`)

## `writ`

### `grammar.writ.export-entrypoints` — Writ grammar public entry rules

The Writ data-format grammar exposes exactly the rules { value, map, array, typed_value, typed_collection } as importable entry points; importing grammars may start parsing at any of these and at no other Writ rule.

*Note (uncertainty):* PEG-tooling directive (grammar-composition surface), not a Logos-source language rule; included as the observable export contract of the Writ literal sub-grammar.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L17-L17`

### `grammar.writ.datatype-form` — DATATYPE node

A DATATYPE node represents a type name with optional generic parameters of the form Typename<Param, ...>.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L40`

### `grammar.writ.node-kinds` — Writ AST node kinds

A Writ value parses to exactly one node kind from the closed set { MAP, ARRAY, STRING, INTEGER, FLOAT, BOOLEAN, NULL_VAL, TYPED_VALUE, DATATYPE, MAP_ENTRY, PARAM_VAL }. Each node carries a CODE field equal to its kind code.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L31-L43`

### `grammar.writ.param-placeholder` — PARAM_VAL positional placeholder

> ⚠️ **Conflict:** this id is also defined in another grammar (writ); the two are distinct rules. This instance is from `grammar/writ/nodes.json`.

A PARAM_VAL node is a positional parameter placeholder written $N; it is assigned the reserved type_hash 127 (tag 0xFF).

*Evidence:* `tools/peg_gen/grammars/writ.peg#L42`

### `grammar.writ.typed-value-form` — TYPED_VALUE node

A TYPED_VALUE node represents a typename-applied-to-value construction of the form Typename(value), e.g. Date("2026-01-01").

*Evidence:* `tools/peg_gen/grammars/writ.peg#L39`

### `lex.writ.bool-null-keywords` — Writ boolean and null literal keywords

The keywords TRUE='true', FALSE='false', NULL='null' are reserved literal tokens denoting boolean true/false and the null value.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L62-L64`

### `lex.writ.float-literal` — Writ float literal

A Writ FLOAT is an optional '-', optional integer part, a mandatory '.' with a fractional part, optional exponent ([eE][+-]?digits), and an optional 'f'|'d' type suffix. Regex: /[-]?[0-9]*\.[0-9]+([eE][+-]?[0-9]+)?[fd]?/. The fractional part is required (a '.' must be followed by >=1 digit).

*Divergence:* Requires a fractional digit after '.'; bare-integer floats and leading-dot are governed by this regex (no trailing-dot form); 'f'/'d' suffixes.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L67`

### `lex.writ.ident` — Writ identifier

A Writ IDENT matches /[a-zA-Z_][a-zA-Z0-9_]*/ and is used both as map keys and as type names.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L70`

### `lex.writ.integer-literal` — Writ integer literal with radix and suffix

A Writ INTEGER is an optional leading '-' followed by a hex (0x/0X), binary (0b/0B), octal (0o/0O), or decimal magnitude, with an optional suffix: '_(u|s)(8|16|32|64)' (sized) or C-style 'ull'|'ul'|'ll'|'u'. Regex: /[-]?(0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|[0-9]+)(_(u|s)(8|16|32|64)|ull|ul|ll|u)?/.

*Divergence:* Data-language lexer (Writ), not Logos source; C-style suffixes ull/ul/ll/u and '_s32'-style signed suffix differ from Rust integer-literal suffixes.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L66`

### `lex.writ.punctuation` — Writ punctuation tokens

Writ lexes the fixed punctuators: LBRACE='{', RBRACE='}', LBRACKET='[', RBRACKET=']', LPAREN='(', RPAREN=')', LANGLE='<', RANGLE='>', COLON=':', COMMA=',', EQUALS='=', DOLLAR='$'.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L48-L59`

### `lex.writ.skip-whitespace-comments` — Writ skipped whitespace and comments

Between tokens Writ skips: whitespace /[ \t\n\r]+/, line comments /\/\/[^\n]*/, and non-greedy block comments /\/\*.*?\*\//. These produce no tokens.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L73-L75`

### `lex.writ.string-literal` — Writ string literal

A Writ STRING is a double-quote-delimited sequence /"([^"\\]|\\.)*"/: any char except '"' or '\', or a backslash followed by any single char (escape).

*Evidence:* `tools/peg_gen/grammars/writ.peg#L65`

### `grammar.writ.array-literal` — Array literal

> ⚠️ **Conflict:** this id is also defined in another grammar (logos, writ); the two are distinct rules. This instance is from `grammar/writ/rule-value.json`.

array <- '[' (value (',' value)*)? ','? ']' producing {CODE:ARRAY, ITEMS:[...]}. The element list may be empty and a single trailing comma is permitted.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L101-L102`

### `grammar.writ.datatype` — Datatype reference with generic args

datatype <- IDENT ('<' type_arg (',' type_arg)* '>')? producing {CODE:DATATYPE, NAME:IDENT, PARAMS:[...]}. The generic argument list, when '<...>' is present, must be non-empty.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L121-L125`

### `grammar.writ.map-entry-key` — Map entry key/value

map_entry <- (STRING / IDENT) ':' value producing {CODE:MAP_ENTRY, KEY, VALUE}. A key may be either a quoted STRING or a bare IDENT; the value is any Writ value.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L97-L98`

### `grammar.writ.map-literal` — Map literal

> ⚠️ **Conflict:** this id is also defined in another grammar (logos, writ); the two are distinct rules. This instance is from `grammar/writ/rule-value.json`.

map <- '{' (map_entry (',' map_entry)*)? ','? '}' producing {CODE:MAP, ITEMS:[...]}. The entry list may be empty and a single trailing comma is permitted after the last entry.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L94-L95`

### `grammar.writ.param-placeholder` — Positional parameter placeholder

> ⚠️ **Conflict:** this id is also defined in another grammar (writ); the two are distinct rules. This instance is from `grammar/writ/rule-value.json`.

param_val <- '$' INTEGER producing {CODE:PARAM_VAL, VALUE:INTEGER}. '$N' is a positional template parameter placeholder where N is a non-negative decimal integer (runtime type_hash=127, tag=0xFF).

*Evidence:* `tools/peg_gen/grammars/writ.peg#L130-L133`

### `grammar.writ.scalar-literals` — Scalar literal node codes

A bare STRING yields {CODE:STRING}, FLOAT yields {CODE:FLOAT}, INTEGER yields {CODE:INTEGER}, TRUE/FALSE yield {CODE:BOOLEAN, VALUE:true|false}, and NULL yields {CODE:NULL_VAL}. Boolean keywords carry the literal value; NULL carries no value.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L86-L91`

### `grammar.writ.type-arg` — Generic type argument

type_arg <- datatype / INTEGER. A generic argument is either a nested datatype or a non-type integer literal (C++-style const generic argument).

*Evidence:* `tools/peg_gen/grammars/writ.peg#L127-L128`

### `grammar.writ.typed-collection` — Typed array/map syntax hints

typed_collection <- typed_array / typed_map; typed_array <- '<' datatype '>' array; typed_map <- '<' datatype '>' map | '<' datatype ',' datatype '>' map. Type parameters preceding the collection are parse-time hints only: the parser still yields plain ARRAY/MAP nodes. A typed map may declare just the key type (value type defaults to AnyVal) or both key and value types.

*Evidence:* `tools/peg_gen/grammars/writ.peg#L114-L119`

### `grammar.writ.typed-value-ctor` — Typed value constructor

typed_value <- datatype '(' value ')' producing {CODE:TYPED_VALUE, NAME:datatype, VALUE:value}. Exactly one inner value is wrapped by the named type constructor (e.g. Date("2026-01-01")).

*Evidence:* `tools/peg_gen/grammars/writ.peg#L104-L106`

### `grammar.writ.value-alternatives` — Writ value grammar

value <- param_val / typed_value / typed_collection / map / array / STRING / FLOAT / INTEGER / TRUE / FALSE / NULL. The ordered alternation is the embeddable entry point for a Writ value; PEG ordering means earlier alternatives win (param_val, typed_value, and typed_collection are attempted before bare map/array/scalars).

*Evidence:* `tools/peg_gen/grammars/writ.peg#L81-L91`

# Hest RPC IDL grammar (`hrpc.peg`)

## `hrpc`

### `lex.hrpc.comments` — HRPC comments

HRPC supports line comments /\/\/[^\n]*/ (to end of line) and block comments /\/\*.*?\*\// (non-greedy, may span lines); both are skipped as trivia.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L93-L94`

### `lex.hrpc.identifier` — HRPC identifier

An IDENT matches /[a-zA-Z_][a-zA-Z0-9_]*/: leading ASCII letter or underscore, followed by ASCII letters, digits, or underscores.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L89`

### `lex.hrpc.integer-literal` — HRPC integer literal

An INTEGER literal is one or more decimal digits /[0-9]+/; no sign, base prefix, or separators.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L86`

### `lex.hrpc.keywords` — HRPC IDL keywords

The HRPC interface-definition language reserves the keyword tokens: `package`, `import`, `option`, `message`, `enum`, `service`, `rpc`, `returns`, `stream`, `repeated`, `optional`, `required`, `map`, `oneof`.

*Note (uncertainty):* HRPC is a separate IDL surface (RPC schema), not the Logos source language.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L56-L70`

### `lex.hrpc.punctuation` — HRPC punctuation tokens

HRPC punctuation tokens: `{` `}` `(` `)` `<` `>` `;` `=` `,` `.` (LBRACE RBRACE LPAREN RPAREN LANGLE RANGLE SEMICOLON EQUALS COMMA DOT).

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L73-L82`

### `lex.hrpc.string-literal` — HRPC string literal

A STRING literal is a double-quoted sequence matching /"([^"\\]|\\.)*"/: any chars except quote/backslash, or a backslash escaping any single char, delimited by `"`.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L85`

### `lex.hrpc.whitespace-skip` — HRPC whitespace is insignificant

Whitespace /[ \t\n\r]+/ is skipped between tokens and is not significant.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L92`

### `grammar.hrpc.enum-decl` — Enum definition

enum_def := 'enum' IDENT '{' enum_value_def* '}'; enum_value_def := IDENT '=' INTEGER ';'. Each enum value binds a name to an explicit integer.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L140-L144`

### `grammar.hrpc.field-decl` — Message field definition

field_def := field_label? type_ref IDENT '=' INTEGER ';' where field_label := 'optional' | 'required' | 'repeated'. Every field carries an explicit numeric tag; the label is optional.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L127-L130`

### `grammar.hrpc.file-structure` — HRPC file top-level structure

file := package_def? import_def* top_def* — an HRPC source file is an optional single package declaration, followed by zero+ imports, followed by zero+ top-level definitions, in that order.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L100-L101`

### `grammar.hrpc.import-decl` — Import declaration

import_def := 'import' STRING ';' — import path is a string literal.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L108-L109`

### `grammar.hrpc.message-decl` — Message definition

message_def := 'message' IDENT '{' message_body* '}' where message_body := field_def | oneof_def | enum_def | message_def | option_def. Messages nest (message/enum may appear inside a message body).

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L121-L124`

### `grammar.hrpc.oneof-decl` — Oneof definition

oneof_def := 'oneof' IDENT '{' oneof_field* '}'; oneof_field := type_ref IDENT '=' INTEGER ';'. Oneof members are unlabeled tagged fields and may not nest other oneofs/messages.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L133-L137`

### `grammar.hrpc.option-decl` — Option declaration

option_def := 'option' IDENT '=' option_value ';' where option_value := STRING | INTEGER | IDENT. Options may appear at top level and inside message bodies.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L112-L115`, `tools/peg_gen/grammars/hrpc.peg#L118`, `tools/peg_gen/grammars/hrpc.peg#L124`

### `grammar.hrpc.package-decl` — Package declaration

package_def := 'package' qualified_name ';' — at most one package declaration per file (production is optional, non-repeating).

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L104-L105`, `tools/peg_gen/grammars/hrpc.peg#L100`

### `grammar.hrpc.qualified-name` — Dotted qualified name

qualified_name := IDENT ('.' IDENT)* — a non-empty dot-separated sequence of identifier components.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L166-L171`

### `grammar.hrpc.rpc-method` — RPC method declaration

rpc_def := 'rpc' IDENT '(' rpc_type ')' 'returns' '(' rpc_type ')' ';' — each method has exactly one input and one output rpc_type.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L150-L151`

### `grammar.hrpc.rpc-stream` — Streaming RPC type

rpc_type := 'stream' type_ref | type_ref — an rpc input or output may be marked streaming with the 'stream' keyword prefix.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L154-L156`

### `grammar.hrpc.service-decl` — Service definition

service_def := 'service' IDENT '{' rpc_def* '}' — a service body contains zero+ rpc method declarations only.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L147-L148`

### `grammar.hrpc.top-def` — Top-level definition kinds

top_def := message_def | enum_def | service_def | option_def — the four item kinds permitted at file scope.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L118`

### `grammar.hrpc.type-ref` — Type reference

type_ref := 'map' '<' type_ref ',' type_ref '>' | qualified_name — a type is either a map with key and value type arguments or a qualified name; maps may nest.

*Evidence:* `tools/peg_gen/grammars/hrpc.peg#L160-L162`
