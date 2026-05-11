# Coercion / cast gaps surfaced by Track 3 imports

Logos is conservative on implicit coercions: most of the
`&mut T → &T`, `& → *const`, `*mut → *const` chains require an
explicit `as` cast at the binding site. This is partly a deliberate
divergence (raw pointers should be explicit) and partly a gap.

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| C6-cc-01 | Auto-deref of `&u8` at shift-op site | ✅ Closed (`bb4d746`, Sprint 1.6) | `(&42u8 >> 4)` rejected — `>>` doesn't auto-deref the LHS. | `cast-does-fallback` | `(&42u8 >> 4) as usize` |
| C6-cc-02 | Top-level `const` as array length | Deferred — metacall gap. The intended Logos replacement for const-eval is `metacall { … }` splice, but metacall isn't yet admitted at array-length position (`[T; metacall {N}]` ⇒ "syntax error near ';'"). Until grammar admits metacall there, the rustc test can't be ported. New gap row needed: MP-mc-01 (metacall at array-length / enum-disc position). | `[i64; S]` where `S: usize` const not accepted at type position. | `constant-expression-cast-9942` | `const S: usize = 23; let _: [i64; S];` |
| C6-cc-03 | Top-level `const` as enum discriminant | Deferred — metacall gap. `enum E { V = metacall { N } }` ⇒ "syntax error near 'enum'". Same MP-mc-01 root. | `enum E { V = QUUX }` with `QUUX: isize` const not parsed; literal works. | `cast-enum-const` | as above |
| C6-cc-04 | `&<literal>` bound to a `let` | ✅ Closed (2026-05-11) — sema's lower_let synthesises a hidden `let __lit_temp_N = <lit>;` before the user's let and rewrites the value to `&__lit_temp_N`. Both lets are wrapped in an SBlock; outer-scope `define()` keeps the user binding visible. | `let r: &T = &42;` previously rejected — temporary-lifetime check. | `basic-ptr-coercions`, `pointer-reassignment-after-deref-78192` (B2) | `let r: &isize = &42isize;` ⇒ "borrows an unnamed temporary" |
| C6-cc-05 | Implicit `&mut T → &T` at binding site | ✅ Closed (`2bedcbb`, Sprint 2) | `let _x: &isize = &mut_isize_ref` rejected. Workaround: explicit reborrow `&*x`. | `basic-ptr-coercions` | `let mx: &mut isize = …; let _x: &isize = mx;` |
| C6-cc-06 | Implicit `&T → *const T` / `*mut T → *const T` | ✅ Closed (`2bedcbb`, Sprint 2) | Same as B3-bg-05 — flagged here for the coercion/cast family too. | `basic-ptr-coercions` | `let _p: *const T = &v;` |
| ~~C6-cc-07~~ | ~~no native `char`~~ | ✅ Closed (entry was wrong) | Logos does have native `char` (4-byte Unicode scalar; `'X'` literals work — see `tests/logos/pass/char_lit_unicode.logos`). `cast/cast.rs` should be importable in a future batch. | `cast/cast.rs` | n/a |
| C6-cc-08 | Fat-pointer → thin-pointer cast | Deferred to slice/fat-ptr work — independent slice/unsize design. | `*const [i32] as *const [i32; 2]`; depends on slice/fat-ptr support. | `cast/fat-ptr-cast-rpass.rs` (not imported) | n/a |
| C6-cc-09 | `Trait object` casts / unsize coercions | Deferred to Sprint 5.8 (closure-as-trait-object dyn lowering arc) — same dyn-trait infra as C5-cl-04 / T9-tr-04. | `&T as &dyn Trait`, `Box<T> as Box<dyn Trait>`, etc. | `cast/cast-rfc0401.rs`, `owned-struct-to-trait-cast-6318.rs` | n/a |
| C6-cc-10 | `{:x}`/`{:X}`/`{:o}`/`{:b}` formatter for usize/isize/u16/u8/i16/i8 | ✅ Closed (2026-05-11) — extended LowerHex/UpperHex/Octal/Binary impls in `stdlib/std/fmt/fmt.logos` from {u64,u32,i64,i32} to also cover {usize,isize,u16,u8,i16,i8}. | Logos format engine had radix formatter impls only for u64/u32/i64/i32, so `println!("{:x}", addr_usize)` rejected. | `cast-region-to-uint` (un-trimmed) | `println!("{:x}", v_usize)` |
