# 2026-09-09e ergoref — the counter-example set, and what each one is FOR

Programs live in the probe's `hand/` directory. Every one is multi-line and returns a
distinct exit code, so a wrong answer is an EXIT CODE and not a string a reader can
mis-parse. Verdicts measured on build 6e10b5ffbceba8b4 (arms installed, none armed = base).

## THE TEN THE 2024 RULE MAKES ILLEGAL — all TEN compile and exit 0 unarmed

  cx04  variant payload, `&`            match &o { Option::Some(ref v) }        [row program]
  cx05  variant payload, `&mut`         match &mut o { Option::Some(ref mut v) }
  cx06  struct field SHORTHAND          match &p { P { ref x, y } }
  cx07  struct field SUB-PATTERN        match &p { P { x: ref v, y: _ } }
  cx08  tuple element                   match &t { (ref a, b) }
  cx09  tuple element, `&mut`           match &mut t { (ref mut a, b) }
  cx10  slice/array element, `&mut`     match &mut arr { [ref mut p, q] }
  cx11  if-let                          if let Option::Some(ref v) = &o
  cx19  or-pattern, both alternatives   match &e { E::A(ref v) => .. E::B(ref v) => .. }
  cx20  STRUCT-typed payload            match &m { HM::Inner(ref l, _) }

The row's header names five doors. The measurement is TEN shapes across FOUR mechanisms,
and the struct-field door turns out to be TWO doors, not one — the shorthand `{ ref x }`
and the sub-pattern `{ x: ref v }` are refused by DIFFERENT arms.

## THE EIGHT THAT ARE LEGAL AND MUST STAY LEGAL (rule 5: vary the SHAPE)

  cx01  `match &o { &Option::Some(ref v) }`  — the `&`-pattern RESETS the mode to move
  cx02  by-value scrutinee, written `ref`
  cx03  `match *o { Option::Some(ref v) }`   — the corpus repair's own shape
  cx13  `match &e { Outer::W(Option::Some(a)) }` — NO modifier written ANYWHERE. The inner
        binder is the COMPILER's own nested-variant synthesis, and `binding_is_ref` is set
        for it too (`synth_wants_ref`). ⚠ THE CRUDE ARM REFUSES THIS PROGRAM, blaming the
        synthesized name `__refut_W_0_0`.
  cx14  `match &p { (Option::Some(a), b) }`  — the same trap at the tuple door
  cx15b `match &o { ref w => }`              — TOP-LEVEL binder: Rust 2024 leaves the
        top-level default binding mode `move`, so the modifier is LEGAL here
  cx15c `match n { ref w => *w }`            — top-level `ref` over a non-reference
  cx16  `match &o { mut w => }`              — the control for the LANDED `mut` half

## AND ONE PROGRAM THAT IS NOT ABOUT THIS ROW AT ALL

  cx15d `match &n { ref w => **w }` with `n: i64` — LEGAL Rust (`w: &&i64`), compiles clean
        on every arm INCLUDING none, and SIGSEGVs at run (rc 139). Inherited: the compiler
        is untouched by this round's corpus commit and it crashes there too. Reported, not
        fixed. See the round report.
