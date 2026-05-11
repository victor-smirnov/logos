# Borrowck / lang-front gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| B3-bg-01 | `let` declare-without-init for an immutable binding | ✅ Closed (2026-05-11) — grammar admits `KW_LET IDENT COLON type_ref SEMI`; sema's lower_let registers the binding with the annotated type and leaves rhs null; mlir-gen allocates the slot without storing. Full definite-assignment analysis deferred — current code trusts user code / catches dangling reads as MLIR uninit-load. | Grammar rejected `let v: T;`. | `lazy-init` (un-trimmed) | `let v: usize;` works |
| B3-bg-02 | `let mut` declare-without-init | ✅ Closed (2026-05-11) — same fix as B3-bg-01 (mirrored alt for KW_MUT). | n/a | `lazy-init` (un-trimmed) | `let mut x: isize; if … { x = 12; } else { x = 10; }` works |
| B3-bg-03 | `&&mut T` codegen | ✅ Closed (`0dcbc44` + `2bedcbb`, Sprint 6/2) | Type parses; mlir-gen emits something that segfaults at runtime. | `borrowck-borrow-of-mut-base-ptr-safe` (trimmed) | `let t2: &&mut isize = &t0;` runtime SIGSEGV |
| B3-bg-04 | Addr-of a captured local inside closure body | Deferred — needs closure capture-mode inference + capture-spill in mlir-gen. By-value captures + reads work; `&captured` doesn't yet wire up the address-of the stored capture slot. ~1 week. | `&x` inside a closure where `x` is captured by value crashes mlir-gen with `& undefined 'x'`. | `borrowck-closures-two-imm` (b/c dropped) | `let c = \|\| -> i32 { return get(&x); };` ⇒ "mlir_gen: & undefined 'x'" |
| B3-bg-05 | Implicit `&T → *const T` at assignment | ✅ Closed (`2bedcbb`, Sprint 2) | Rust silently coerces; Logos requires explicit `as *const T` cast at the assignment site. | `pointer-reassignment-after-deref-78192` | `c: *const u32 = …; let d: &u32 = …; c = d;` ⇒ type-mismatch |
| B3-bg-06 | Vec → slice coercion at fn-arg position | Deferred — needs `Deref`-coerce machinery (Rust's `Vec<T>: Deref<Target=[T]>`). Could be done as targeted compiler-side coercion (cheap) or full `Deref` trait surface (medium). | Rust passes `Vec<T>` into `fn foo(v: &[T])` via Deref coercion; Logos has no auto-Deref to slice. | `borrowck-mut-vec-as-imm-slice` | `fn want_slice(v: &[isize])` called with `&v: &Vec<isize>` rejected |
| B3-bg-07 | `for i in &v` where `i: &T` and the body does `*i` | Deferred — needs reduction first. Likely interaction with `for x in &Vec` iteration model (yields T or &T?) — see `auto-loop.logos` trim note. | Iterator borrowing model trips on `*i` reads inside the loop in some cases. | `borrowck-mut-vec-as-imm-slice` (loop unrolled) | tbd |

## Closing rules

Same as `parser-gaps.md`: fix → land focused regression in `tests/logos/pass/borrowck_<name>.logos` → un-trim the imported test.
