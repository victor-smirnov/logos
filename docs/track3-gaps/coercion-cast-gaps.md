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
| C6-cc-02 | Top-level `const` as array length | ✅ Closed (2026-05-11) — via MP-mc-01: `arr_type` admits `[T; metacall { <expr> }]`, sema ctfe-evaluates to literal length. | `[i64; metacall { N }]` is the Logos idiom. | `constant-expression-cast-9942` (un-trimmed) | `let _: [i64; metacall { 23i64 }]` works |
| C6-cc-03 | Top-level `const` as enum discriminant | ✅ Closed (2026-05-11) — via MP-mc-01: `variant_def` admits `V = metacall { <expr> }`. | `enum E { V = metacall { N } }` works. | `cast-enum-const` (un-trimmed) | `enum Stuff { Bar = metacall { 5i64 } }` works |
| C6-cc-04 | `&<literal>` bound to a `let` | ✅ Closed (2026-05-11) — sema's lower_let synthesises a hidden `let __lit_temp_N = <lit>;` before the user's let and rewrites the value to `&__lit_temp_N`. Both lets are wrapped in an SBlock; outer-scope `define()` keeps the user binding visible. | `let r: &T = &42;` previously rejected — temporary-lifetime check. | `basic-ptr-coercions`, `pointer-reassignment-after-deref-78192` (B2) | `let r: &isize = &42isize;` ⇒ "borrows an unnamed temporary" |
| C6-cc-05 | Implicit `&mut T → &T` at binding site | ✅ Closed (`2bedcbb`, Sprint 2) | `let _x: &isize = &mut_isize_ref` rejected. Workaround: explicit reborrow `&*x`. | `basic-ptr-coercions` | `let mx: &mut isize = …; let _x: &isize = mx;` |
| C6-cc-06 | Implicit `&T → *const T` / `*mut T → *const T` | ✅ Closed (`2bedcbb`, Sprint 2) | Same as B3-bg-05 — flagged here for the coercion/cast family too. | `basic-ptr-coercions` | `let _p: *const T = &v;` |
| ~~C6-cc-07~~ | ~~no native `char`~~ | ✅ Closed (entry was wrong) | Logos does have native `char` (4-byte Unicode scalar; `'X'` literals work — see `tests/logos/pass/char_lit_unicode.logos`). `cast/cast.rs` should be importable in a future batch. | `cast/cast.rs` | n/a |
| C6-cc-08 | Fat-pointer → thin-pointer cast | Deferred to slice/fat-ptr work — independent slice/unsize design. | `*const [i32] as *const [i32; 2]`; depends on slice/fat-ptr support. | `cast/fat-ptr-cast-rpass.rs` (not imported) | n/a |
| C6-cc-09 | `Trait object` casts / unsize coercions (`&T → &dyn Trait`) | Partial (2026-05-11) — first slice closed: at fn-call sites a `&T` / `&mut T` argument coerces to `&dyn Trait` / `&mut dyn Trait` when the parameter expects a trait object. Sema's `types_compatible` accepts the ref-over-struct source; mlir-gen unwraps the ref pointee when looking up the impl, and `coerce_to_dyn` materialises the fat-pointer pair from the existing ref value. New `&mut dyn` grammar alt added. Remaining: explicit `as &dyn Trait` cast expressions, `Box<T> as Box<dyn Trait>` (call-site coercion already works for non-cast positions), `+ 'a` lifetime bound on dyn. | `&T → &dyn Trait` (implicit at fn-arg), `&mut T → &mut dyn Trait`. | `coercion/coerce-mut-trait-object-8248`, `coercion/coerce-ref-trait-object` (probe). | `fn foo(_: &mut dyn A); foo(&mut b);` compiles + runs + dispatches via vtable |
| C6-cc-10 | `{:x}`/`{:X}`/`{:o}`/`{:b}` formatter for usize/isize/u16/u8/i16/i8 | ✅ Closed (2026-05-11) — extended LowerHex/UpperHex/Octal/Binary impls in `stdlib/std/fmt/fmt.logos` from {u64,u32,i64,i32} to also cover {usize,isize,u16,u8,i16,i8}. | Logos format engine had radix formatter impls only for u64/u32/i64/i32, so `println!("{:x}", addr_usize)` rejected. | `cast-region-to-uint` (un-trimmed) | `println!("{:x}", v_usize)` |
