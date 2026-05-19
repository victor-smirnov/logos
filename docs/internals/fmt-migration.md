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
- [ ] `()` unit — DEFERRED. Logos parser doesn't accept `()` in
      impl-target position (`impl X for () {...}` errors). Tuples of
      arity ≥ 1 work fine. Metacall path for `format!("{}", ())` can
      special-case at Session 4 or wait for a grammar fix.
- [x] tuples `(A,)`, `(A,B)`, `(A,B,C)`, `(A,B,C,D)` — Debug
      (`&self` recv, recurse into each field's `fmt_debug`)

### Session 3 — Stdlib structs + numeric format traits (2026-05-18)

- [x] `Option<T>` Debug — works. Blanket-bound recursion fix
      landed 2026-05-19 via `mono_concrete_satisfies_bound` —
      see baghunt_mono_blanket_bound_recursion (CLOSED).
- [ ] `Result<T, E>` Debug — DEFERRED. Distinct mono bug:
      enum-method-template instantiation skips multi-param
      generic-enum impls. `Result<i32,i32>::fmt_debug` never
      emits even when needed by Option<Result<...>> body.
      Vec/Option work because inner-call resolves to concrete
      leaf.
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
