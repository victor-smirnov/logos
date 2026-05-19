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

### Session 2 — Primitive migration

- [ ] `i8` `i16` `i64` `isize` — Display + Debug
- [ ] `u8` `u16` `u32` `u64` `usize` — Display + Debug
- [ ] `bool` — Display + Debug
- [ ] `str` — Display + Debug
- [ ] `*const u8` (C string) — Display
- [ ] `f32` `f64` — Display + Debug (route through existing
      `logos_fmt_f64_g`/etc. extern, then `pad`)
- [ ] `()` unit — Display + Debug
- [ ] tuples `(A,)`, `(A,B)`, `(A,B,C)`, `(A,B,C,D)` — Debug; Logos
      already has Display=Debug for tuples in std.fmt

### Session 3 — Stdlib structs + numeric format traits

- [ ] `Option<T>` Debug (`Some(7)` / `None`)
- [ ] `Result<T, E>` Debug
- [ ] `Vec<T>` Debug (`[1, 2, 3]`)
- [ ] `String` Display = pass-through to as_str(); Debug = quoted
- [ ] `Ordering` Display + Debug
- [ ] `LowerHex`/`UpperHex`/`Octal`/`Binary` — either migrate to take
      `&mut Formatter` OR fold into Display via spec field check.
- [ ] `LowerExp`/`UpperExp` — same choice.

### Session 4 — Metacall migration + legacy delete

- [ ] Rewrite `format_args_str` to parse the spec string into a
      Formatter, then call `arg.fmt_display(&mut f)` per placeholder.
- [ ] Rewrite `format!`/`print!`/`println!`/`eprint!`/`eprintln!`/
      `panic!`/`assert!`/`assert_eq!`/`assert_ne!` metacalls to use
      the new path (most just funnel through `format_args_str` —
      change is local).
- [ ] Delete `fmt_display` / `fmt_debug` / `fmt_lower_hex` / etc.
      helper fns.
- [ ] Delete `fmt_pad` / `pad_into`.
- [ ] Delete legacy `mem.string::Display` trait + every `impl Display
      for X { fn fmt(self, &mut String) }` impl.
- [ ] Delete legacy `std.fmt::Debug` trait + every `impl Debug for X
      { fn dbg(self, &mut String) }` impl.
- [ ] Rename `FmtDisplay` → `Display`, `FmtDebug` → `Debug`, and
      method names `fmt_display`/`fmt_debug` → `fmt`. Trait-registry
      collision risk lifts because legacy is gone.

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
