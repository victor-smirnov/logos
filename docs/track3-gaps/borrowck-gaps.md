# Borrowck / lang-front gaps surfaced by Track 3 imports

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| B3-bg-01 | `let` declare-without-init for an immutable binding | Logos grammar rejects `let v: T;` (no initialiser, no `mut`). Rust accepts this when the binding is definitely-assigned downstream. | `borrowck-scope-of-deref-issue-4666` (fun1 dropped), various | `let v: usize;` ⇒ "syntax error near 'usize'" |
| B3-bg-02 | `let mut` declare-without-init | Mostly the same — but possibly grammar accepts and sema rejects. Confirm. | `lazy-init` | `let mut x: isize; if … { x = 12; } else { x = 10; }` ⇒ "syntax error near 'isize'" |
| B3-bg-03 | `&&mut T` codegen | Type parses; mlir-gen emits something that segfaults at runtime. | `borrowck-borrow-of-mut-base-ptr-safe` (trimmed) | `let t2: &&mut isize = &t0;` runtime SIGSEGV |
| B3-bg-04 | Addr-of a captured local inside closure body | `&x` inside a closure where `x` is captured by value crashes mlir-gen with `& undefined 'x'`. By-value captures and reads work; capture-by-reference / addr-of-captured isn't wired. | `borrowck-closures-two-imm` (b/c dropped) | `let c = || -> i32 { return get(&x); };` ⇒ "mlir_gen: & undefined 'x'" |
| B3-bg-05 | Implicit `&T → *const T` at assignment | Rust silently coerces; Logos requires explicit `as *const T` cast at the assignment site. May be the right answer (raw pointers should be explicit), but documented for the record. | `pointer-reassignment-after-deref-78192` | `c: *const u32 = …; let d: &u32 = …; c = d;` ⇒ type-mismatch |
| B3-bg-06 | Vec → slice coercion at fn-arg position | Rust passes `Vec<T>` into `fn foo(v: &[T])` via Deref coercion. Logos has `Vec<T>` but no auto-Deref to slice. | `borrowck-mut-vec-as-imm-slice` | `fn want_slice(v: &[isize])` called with `&v: &Vec<isize>` rejected |
| B3-bg-07 | `for i in &v` where `i: &T` and the body does `*i` | The IntoIterator-for-`&Vec` path yields refs whose lifetime is tied to the borrow of `v`; Logos's current iterator borrowing model trips on `*i` reads inside the loop in some cases. Not fully reduced — flagged for triage. | `borrowck-mut-vec-as-imm-slice` (loop unrolled) | tbd |

## Closing rules

Same as `parser-gaps.md`: fix → land focused regression in `tests/logos/pass/borrowck_<name>.logos` → un-trim the imported test.
