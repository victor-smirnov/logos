# Coercion / cast gaps surfaced by Track 3 imports

Logos is conservative on implicit coercions: most of the
`&mut T → &T`, `& → *const`, `*mut → *const` chains require an
explicit `as` cast at the binding site. This is partly a deliberate
divergence (raw pointers should be explicit) and partly a gap.

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| C6-cc-01 | Auto-deref of `&u8` at shift-op site | `(&42u8 >> 4)` rejected — `>>` doesn't auto-deref the LHS. | `cast-does-fallback` | `(&42u8 >> 4) as usize` |
| C6-cc-02 | Top-level `const` as array length | `[i64; S]` where `S: usize` const not accepted at type position. Generic `const N: i64` works (existing slice-3.6 fix); free-standing `const` doesn't. | `constant-expression-cast-9942` | `const S: usize = 23; let _: [i64; S];` |
| C6-cc-03 | Top-level `const` as enum discriminant | `enum E { V = QUUX }` with `QUUX: isize` const not parsed; literal works. | `cast-enum-const` | as above |
| C6-cc-04 | `&<literal>` bound to a `let` | `let r: &T = &42;` rejected — temporary-lifetime check requires binding the literal to a named let first. | `basic-ptr-coercions`, `pointer-reassignment-after-deref-78192` (B2) | `let r: &isize = &42isize;` ⇒ "borrows an unnamed temporary" |
| C6-cc-05 | Implicit `&mut T → &T` at binding site | `let _x: &isize = &mut_isize_ref` rejected. Workaround: explicit reborrow `&*x`. | `basic-ptr-coercions` | `let mx: &mut isize = …; let _x: &isize = mx;` |
| C6-cc-06 | Implicit `&T → *const T` / `*mut T → *const T` | Same as B3-bg-05 — flagged here for the coercion/cast family too. | `basic-ptr-coercions` | `let _p: *const T = &v;` |
| C6-cc-07 | `char` literal `'Q'` + `'Q' as isize` | Logos has no native `char`; `cast.rs` (the original) uses character → integer casts heavily. Not imported. | `cast/cast.rs` | n/a |
| C6-cc-08 | Fat-pointer → thin-pointer cast | `*const [i32] as *const [i32; 2]`; depends on slice/fat-ptr support. | `cast/fat-ptr-cast-rpass.rs` (not imported) | n/a |
| C6-cc-09 | `Trait object` casts / unsize coercions | `&T as &dyn Trait`, `Box<T> as Box<dyn Trait>`, etc. Multiple tests blocked. | `cast/cast-rfc0401.rs`, `owned-struct-to-trait-cast-6318.rs`, etc. | n/a |
