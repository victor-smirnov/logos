# Function-style Macros

Logos's `name!(...)` invocation surface is built on top of the
[metaprogramming layer](metaprog.md) — every macro is an ordinary
Logos function with a marker annotation. There is no separate macro
language; macros run in the same compile-time JIT that powers
`metacall` and `quote_*!`. This page describes the three macro kinds,
how the compiler dispatches them, and the format-family it provides
out of the box.

## At a glance

```logos
use logos.std.fmt;
use logos.mem.collections.vec;
use logos.std.compiler.metaprog;

// Function-style macro: AST args, ExprBlob result.
#[fn_macro]
pub fn vec(elems: Vec<ExprBlob>) -> ExprBlob {
    return quote_expr! { vec_from_arr([#(#elems),*]) };
}

// Token-stream macro: raw source bytes, ExprBlob result.
#[token_macro]
pub fn raw(s: str) -> ExprBlob {
    return quote_expr! { 99i32 };
}

// Item-position macro: brace form at module top level, ItemList result.
#[fn_macro]
pub fn emit_pair(_: Vec<ExprBlob>) -> ItemList {
    let mut out: ItemList = item_list_new();
    let id: Ident = Ident { ptr: "Foo".as_ptr(), len: 3u64 };
    let b: QuoteItemBlob = quote_item! { struct #id { x: i32 } };
    unsafe { (&mut out.blobs as *mut Vec<QuoteItemBlob>).push(b); }
    return out;
}

fn main() -> i32 {
    let v: Vec<i32> = vec!(1i32, 2i32, 3i32);
    let r: i32 = raw!{ this is { just } [some] (tokens) };
    println!("v.len = {}", v.length());
    return r;
}

emit_pair!{}  // generates `struct Foo { x: i32 }` in this module
```

## The three macro kinds

| Marker | Callee signature | Invocation surface | Resolved at |
|---|---|---|---|
| `#[fn_macro]` | `(Vec<ExprBlob>) -> ExprBlob` | `name!(...)` / `name![...]` | expression position |
| `#[fn_macro]` | `(ExprBlob) -> ExprBlob` | `name!(...)` / `name![...]` (exactly one arg) | expression position |
| `#[fn_macro]` | `(Vec<ExprBlob>) -> ItemList` or `() -> ItemList` | `name!{...}` | module item position |
| `#[fn_macro]` | `(Vec<ExprBlob>) -> QuoteItemBlob` etc. | `name!{...}` | module item position |
| `#[token_macro]` | `(str) -> ExprBlob` | `name!(...)` / `name![...]` / `name!{...}` | expression position |

The marker decides how the compiler treats the call site:

* **`#[fn_macro]` at expression position** — the parser captures the
  argument list as raw bytes, sema re-parses it as a comma-separated
  expression list, then each parsed expression is serialised as an
  `ExprBlob` and passed to the callee. The callee's `ExprBlob`
  return value is spliced back into the AST at the invocation site
  via the metacall JIT.

* **`#[fn_macro]` at item position** — same raw-capture pipeline, but
  the callee must return `ItemList` (or `QuoteItemBlob`); the resulting
  items splice into the enclosing module's item list, exactly as a
  `metacall foo();` item-form would.

* **`#[token_macro]`** — the parser still captures the contents as
  raw bytes, but sema does **not** re-parse them as Logos expressions.
  Instead the bytes are handed to the callee as a single `str` arg.
  Useful for DSLs whose body is not valid Logos.

## Positional rules

The Logos grammar exposes three delimiter forms:

* `name!(args)` — parentheses; expression position only.
* `name![args]` — brackets; expression position only.
* `name!{...}` — braces;
  * at module top level: item-position macro (`FN_MACRO_CALL_ITEM`);
  * elsewhere: expression-position macro (`FN_MACRO_CALL`).

Inside the delimiters the parser performs balanced-delimiter capture
— `name!{ nested (paren) ok }` reaches the matching close brace
correctly. The captured text is stashed in the AST node's `RAW_TEXT`
field; sema interprets it per callee marker.

## `ExprBlob` and how splicing works

Each `ExprBlob` carries a pointer to an AST-shaped Hermes blob in
rodata. When the metacall JIT thunk invokes the macro callee, the
returned `ExprBlob` becomes an `HERMES_BLOB` AST node that replaces
the original `FN_MACRO_CALL`. The next sema pass decodes the blob and
lowers it as a normal expression — so the macro author can compose
`ExprBlob` values freely via `quote_expr! { ... }` without worrying
about how the bytes wire back into sema.

The most common patterns:

```logos
// Splice a single child expression.
let body: ExprBlob = quote_expr! { #(args.get(0i64)) };

// Splice over a Vec<ExprBlob> cursor with `,` separator.
return quote_expr! { vec_from_arr([#(#elems),*]) };

// Splice an Ident-typed name into a position that takes a bare ident.
let id: Ident = /* … */;
return quote_item! { struct #id { x: i32 } };
```

## The `format!` family

Five built-in macros ship in `std.fmt`:

| Macro | Returns | Side effect |
|---|---|---|
| `format!` | `String` | none |
| `print!` | `()` | writes to stdout, no newline |
| `println!` | `()` | writes to stdout + `\n` |
| `eprint!` | `()` | writes to stderr, no newline |
| `eprintln!` | `()` | writes to stderr + `\n` |

All five are `#[fn_macro]` callees in `std.fmt` that the sema layer
recognises by name. Instead of dispatching through a JIT thunk, sema
parses the format string at compile time into a structured segment
list and synthesises a block expression with one explicit trait call
per placeholder:

```logos
format!("a={}, b={:?}", x, y)
// lowers to
{
    let mut __buf: String = String::new();
    __buf.push_str("a=");
    fmt_display(x, &mut __buf);
    __buf.push_str(", b=");
    fmt_debug(y, &mut __buf);
    __buf
}
```

Eight format-spec trait kinds are wired (each backed by its own
`std.fmt` trait + free-fn dispatcher):

| Spec | Trait | Free-fn dispatcher |
|---|---|---|
| `{}` | `Display` | `fmt_display` |
| `{:?}` | `Debug` | `fmt_debug` |
| `{:x}` | `LowerHex` | `fmt_lower_hex` |
| `{:X}` | `UpperHex` | `fmt_upper_hex` |
| `{:o}` | `Octal` | `fmt_octal` |
| `{:b}` | `Binary` | `fmt_binary` |
| `{:e}` | `LowerExp` | `fmt_lower_exp` |
| `{:E}` | `UpperExp` | `fmt_upper_exp` |

All Rust-compatible spec modifiers are supported:

* `{:N}` — minimum width (right-align by default).
* `{:<N}` / `{:>N}` / `{:^N}` — left / right / center align.
* `{:CN}` — fill char `C` (e.g. `{:*>5}` → `"***42"`).
* `{:.N}` — precision (truncates strings; rounds floats via libc).
* `{:+}` — force `+` sign on positive numbers.
* `{:#}` — alt form (`0x` / `0X` / `0o` / `0b` prefix).
* `{:0N}` — zero-pad shortcut (`{:05}` ≡ `{:0>5}` with `0` after sign/prefix).

The sema parser validates the format string at compile time —
arity / brace-balance / unknown spec chars all surface as proper
diagnostics with byte offsets into the format-string literal.

## Hygiene

Macro-generated identifiers live in fresh block scopes, so a
generated `let mut __buf = …;` cannot collide with a user-side
`__buf` in the surrounding scope. References to identifiers from the
call-site (e.g. `x` in `format!("{}", x)`) resolve in the user's
scope — call-site hygiene, like Rust's `macro_rules!` non-hygienic
references. Macros that need a guaranteed-unique local name use
`gensym("prefix")` from `std.compiler.metaprog.ast`.

## Diagnostic surface

The compiler distinguishes these diagnostic categories on macro
invocations:

* `fn_macro: unknown callee 'name!'` — no fn with that name.
* `fn_macro: 'name' is not marked #[fn_macro]` — callee exists but
  lacks the marker; ordinary fn-call syntax is what the user wants.
* `fn_macro: ... must have signature ...` — callee marker doesn't
  match its parameter / return shape.
* `format!: format string has K placeholders but M argument(s) provided`
  — arity mismatch in a format-family macro.
* `format!: unmatched `{` at format-string offset N` /
  `unmatched `}` at format-string offset N` — brace-balance error
  with byte offset into the literal.
* `format!: unknown type char 'c'` — unrecognised format-spec type.

## Implementation notes

* All `name!()` invocations go through the metacall JIT pipeline,
  with one exception: the `format!`-family macros are
  sema-resident — sema synthesises the equivalent block expression
  directly and skips the JIT thunk. Cost: one `LogosParser` re-parse
  per call site (the synthesised block); benefit: per-placeholder
  trait dispatch falls out at compile time, no T-bound bloat.

* The slice 3 raw-capture grammar means the same `name!(...)` /
  `name![...]` / `name!{...}` form works for both `#[fn_macro]` and
  `#[token_macro]` callees — sema's branching keys off the marker.

* The metacall arg-blob table (`logos_macro_arg` host shim) is shared
  between expression-position and item-position macros — sema
  serialises each parsed argument as an `ExprBlob` and the JIT
  thunk reconstitutes the `Vec<ExprBlob>` argument from per-site
  blob pointers.

## See also

* [Metaprogramming](metaprog.md) — the underlying `metacall` /
  `quote_*!` / `template` machinery.
* [Attributes](attributes.md) — the `#[fn_macro]` and `#[token_macro]`
  annotations.
* [`std.fmt`](../../../stdlib/std/fmt/fmt.logos) — the format-family
  implementation: traits, dispatchers, runtime helpers.
