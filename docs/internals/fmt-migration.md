# core::fmt → Formatter migration

Multi-session plan for migrating Logos's stdlib formatting from the legacy
`fn fmt(self: Self, buf: &mut String)` shape (kept in `mem.string::Display`
and `std.fmt::Debug`) over to the Rust-faithful
`fn fmt(&self, f: &mut Formatter) -> Result<(), Error>` shape (declared
in `lang.mem.fmt`).

## Why

- Rust ports lose their `Display`/`Debug` semantics today — width / fill /
  alignment / precision / sign / alt-form / zero-pad spec params don't
  reach the per-impl rendering code. Spec parsing happens once inside
  `format_args_str` and the body has to undo or replicate it through the
  `fmt_pad` driver.
- Closure-style Display/Debug impls used in Rust code (e.g. `write!(f,
  "[{:>5}]", x)` chains) have no analogue without a real `Formatter`
  with spec fields.
- `std::fmt::Write` consumers (anything that wants to plug into a
  formatter as a sink) need the trait.

## End state

```logos
package logos.mem.fmt;

pub trait Display {
    fn fmt(&self, f: &mut Formatter) -> Result<(), Error>;
}
pub trait Debug {
    fn fmt(&self, f: &mut Formatter) -> Result<(), Error>;
}
```

All primitive and stdlib impls migrated. `format!`/`println!`/etc.
metacalls parse the spec string into Formatter fields, then dispatch
through `Display::fmt` / `Debug::fmt`. `fmt_pad` /
`fmt_display`/`fmt_debug` helpers gone; `LowerHex`/`UpperHex`/`Octal`/
`Binary`/`LowerExp`/`UpperExp` either migrated or absorbed into a
`{:x}` spec routed through `Formatter::pad`.

## Stages

### Session 1 (THIS commit) — Foundation

- [x] `Formatter::pad(content, prefix)` — port of `fmt_pad` reading
      spec fields off `self`.
- [x] `Formatter::write_int_dec_{i64,u64}` / `write_int_radix_u64` /
      `write_bool` — primitive writers all routing through `pad`.
- [x] `FmtDisplay` / `FmtDebug` traits declared in `lang.mem.fmt`.
      Method names `fmt_display` / `fmt_debug` (not `fmt`) — Logos's
      method-mangling doesn't include the trait name, so `fmt` would
      clash with the legacy traits' `fmt` for the same impl type. They
      rename back to `fmt` in Session 4 alongside the legacy delete.
- [x] `fmt_display_to_string<T: FmtDisplay>(&T) -> String` +
      `fmt_debug_to_string<T: FmtDebug>` convenience renderers.
- [x] `impl FmtDisplay for i32` + `impl FmtDebug for i32` as
      proof-of-shape.
- [x] Smoke: `fmt_formatter_display.logos` exercises bare render,
      width+right, width+zero, width+left, hex radix, bool.

### Session 2 — Primitive migration ✅ (2026-05-18)

- [x] `i8` `i16` `i64` `isize` — Display + Debug
- [x] `u8` `u16` `u32` `u64` `usize` — Display + Debug
- [x] `bool` — Display + Debug
- [x] `str` — Display (pad), Debug (`"<content>"` — minimal quote, full
      `\n`/`\t`/`\u{…}` escape ladder is a follow-up alongside utf8 work)
- [x] `*const u8` (C string) — Display
- [x] `f32` `f64` — Display + Debug (extern bridge into
      `logos_fmt_f{32,64}_g` then `pad`)
- [x] `()` unit — Display + Debug. Grammar accepts `()` in
      impl-target position (logos.peg `simple_type` gained
      `LPAREN RPAREN` → `TUPLE_TYPE` 2026-05-19). Both
      `sema_collect.cpp` and `sema_decl.cpp` TUPLE_TYPE branches
      detect `resolved.kind() == Kind::Void` after `resolve_type`
      and register impl-target under `"void"` (instead of the
      arity-keyed `$tuple$0`), keeping collect-side method
      mangling and decl-side LImplBlock target_type in sync so
      mono lookup links use-sites to the emitted body.
- [x] tuples `(A,)`, `(A,B)`, `(A,B,C)`, `(A,B,C,D)` — Debug
      (`&self` recv, recurse into each field's `fmt_debug`)

### Session 3 — Stdlib structs + numeric format traits (2026-05-18)

- [x] `Option<T>` Debug — works. Blanket-bound recursion fix
      landed 2026-05-19 via `mono_concrete_satisfies_bound` —
      see baghunt_mono_blanket_bound_recursion (CLOSED).
- [x] `Result<T, E>` Debug — works after refining
      `is_self_referential` (mono_clone.cpp ~line 4500): a fully
      concrete-arg use of the enum (e.g. `Result<(), Error>` as
      a return type) no longer counts as self-recursive, only
      args that contain TypeVars from the fn's own params do.
- [x] `Vec<T>` Debug — works (fix above).
- [x] `String` Display = pass-through `as_str()` through `pad`; Debug
      = `"<content>"` quoted (same minimal-escape caveat as str).
- [x] `Ordering` Display + Debug. NB: `match *self`
      (deref-before-match) sidesteps an icmp-on-ptr cascade that
      fires when matching directly on `&Self` for an enum scrutinee.
- [x] `FmtLowerHex`/`FmtUpperHex`/`FmtOctal`/`FmtBinary` — new
      Rust-shape traits + impls for every integer primitive
      (i8..i64+isize, u8..u64+usize). Method names suffixed
      `_fmt_lower_hex` etc. for the same legacy-mangling reason as
      `fmt_display`/`fmt_debug`; rename back to plain method names
      in Session 4.
- [x] `FmtLowerExp`/`FmtUpperExp` — same, for f32/f64. Extern bridge
      `logos_fmt_f{32,64}_{e,E}` to libc snprintf.

**Generic-blanket deferral.** `impl<T: FmtDebug> FmtDebug for Vec<T>`
(and Option/Result) triggers mono to instantiate
`Vec$G1$X__fmt_debug` for EVERY concrete `X` that exists in the
program, even when `X` has no `FmtDebug` impl. Bodies then reference
`v.fmt_debug(f)` against an unimplemented X → "func.call does not
reference a valid function". Same baghunt class as identity
`impl<T> Borrow<T> for T`. Workaround is per-type impls (defeats the
generic point). Real fix: mono needs to honour bounds at
blanket-instantiation time and skip Xs that don't satisfy the bound.
Filed as a follow-up; container Debug impls re-add once that lands.

### Session 4 — Metacall migration + legacy delete + rename ✅ (2026-05-19)

- [x] Sema-resident lowering (sema_expr.cpp:13400+) emits Formatter-
      shape block: builds `Formatter::new(&mut __buf)`, writes spec
      fields per placeholder (`__f.width = …; __f.align_code = …;`),
      dispatches through free-fn dispatcher (`fmt_display(&arg, &mut
      __f)` / `fmt_debug` / `fmt_lower_hex` / `fmt_upper_hex` /
      `fmt_octal` / `fmt_binary` / `fmt_lower_exp` / `fmt_upper_exp`).
      Dispatchers take `&T` (not by-value) so the format!-supplied
      arg isn't moved/dropped through the synthesized block.
- [x] `format_args_str` joins the format-family intercept list, so
      `assert_eq!`/`assert_ne!`/`panic!` expansions (now emitting
      `format!(…)` instead of direct `format_args_str(…)`) lower
      through sema-resident interception. Runtime variadic-bound
      `T...: Display + Debug` never has to bind to non-Display types
      (Result<i32,str>, tuples with only Debug, etc.).
- [x] Helper fns `fmt_display`/`fmt_debug`/`fmt_lower_hex`/etc.
      kept (now in std.fmt, new signature) since the slice/str
      dot-method short-circuit still applies; bodies just delegate
      to `arg.fmt_display(f)`. `fmt_pad`/`pad_into` gone — spec
      lives on Formatter and is applied by `Formatter::pad`.
- [x] Legacy `mem.string::Display` trait + per-primitive impls
      deleted. `to_string<T: Display>` free fn deleted. ToString
      trait stays in mem.string with primitive impls that don't go
      through Display; blanket `impl<T: Display> ToString for T`
      moves to mem.fmt.
- [x] Legacy `std.fmt::Debug` trait + per-primitive `dbg` impls
      deleted alongside the 5 test files + logos_showcase example
      that used to consume it (debug_primitives, derive_debug_e2e/
      enum/generic, impl_for_tuple, logos_showcase). All migrated
      to FmtDebug shape; derive metaprog hooks emit `impl Debug for
      X { fn fmt_debug(&self, f: &mut Formatter) -> Result<(), Error>
      { ... } }`.
- [x] Renamed `FmtDisplay` → `Display`, `FmtDebug` → `Debug`,
      `FmtLowerHex` → `LowerHex`, etc. Methods stay distinct
      (`fmt_display`/`fmt_debug`/`_fmt_lower_hex`/…) — Logos's flat
      method registry per type can't currently host two traits'
      `fmt(&self, &mut Formatter)` on the same primitive without
      collision. Trait-aware method mangling lifts that and lets
      methods rename to plain `fmt` in a follow-up.

## Risk register

- Cross-package trait-name registry collision (B-mv-02) — only fires
  if two traits with the same name live in different packages. Step 4
  resolves this by deleting legacy first, then renaming.
- Mlir-gen `&mut Self` receiver — already in use for `*Assign`
  operator family and `DerefMut`, so the call shape is well-tested.
- Tuple impls take Self by value, while the new trait takes `&self`.
  Migration code path: change tuple receiver to `&self`.
- Each migration step is independently testable — Stage 2 impls coexist
  with Stage 4 metacalls (which still consume legacy traits) until
  Stage 4 swaps. No big-bang day.
