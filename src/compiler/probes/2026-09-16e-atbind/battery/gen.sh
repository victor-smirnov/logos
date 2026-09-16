#!/usr/bin/env bash
# gen.sh — write the `@`-binder hand battery (Logos) and its rustc twins.
# VARY THE SHAPE, NOT THE COUNT: struct / tuple / enum-payload / wildcard / nested /
# reference scrutinee / let spelling / loop / guard / Copy control / no-return control.
# The plain spelling is written FIRST (a01), per the armelem round's own correction.
#
# Every program's oracle is a DESTRUCTOR COUNT through a *mut i64 sequence counter
# (`*c = *c * 10 + v`), read AFTER the scrutinee's scope ends, so a leak reads SHORT
# and a double drop reads LONG — rc alone cannot tell them apart.
set -u
D="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$D/logos" "$D/rust"

# ---------- Logos ----------
w() { cat > "$D/logos/$1.logos"; }

w a01 <<'EOF'
// a01 — THE PLAIN SPELLING: `y @ W { .. }` over a by-value struct, arm returns.
package a01;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let w: W = W { a: D { v: 2i64, c: p }, b: 3i64 };
    match w {
        y @ W { .. } => { return y.b; }
    }
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a02 <<'EOF'
// a02 — EXPRESSION POSITION: the same `@` binder as the value of a `let`.
package a02;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let w: W = W { a: D { v: 2i64, c: p }, b: 3i64 };
    let k: i64 = match w { y @ W { .. } => y.b };
    return k;
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a03 <<'EOF'
// a03 — TUPLE scrutinee: `y @ (_, _)`.
package a03;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn g(p: *mut i64) -> i64 {
    let t: (D, i64) = (D { v: 2i64, c: p }, 3i64);
    match t {
        y @ (_, _) => { return y.1; }
    }
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a04 <<'EOF'
// a04 — ENUM PAYLOAD by value, the second target row's shape, with the OUTER local too.
package a04;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn g(p: *mut i64) -> i64 {
    let x: D = D { v: 1i64, c: p };
    let o: Option<D> = Option::Some(D { v: 2i64, c: p });
    match o {
        y @ Option::Some(_) => { if y.is_some() { return 4i64; } return 3i64; }
        Option::None => {}
    }
    return x.v;
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a05 <<'EOF'
// a05 — `y @ _` over a Drop struct: the WILDCARD sub-pattern.
package a05;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn g(p: *mut i64) -> i64 {
    let d: D = D { v: 2i64, c: p };
    match d {
        y @ _ => { return y.v; }
    }
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a06 <<'EOF'
// a06 — NO `return` in the arm: the arm falls through to the end of the function.
package a06;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let mut k: i64 = 0i64;
    let w: W = W { a: D { v: 2i64, c: p }, b: 3i64 };
    match w {
        y @ W { .. } => { k = y.b; }
    }
    return k;
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a07 <<'EOF'
// a07 — COPY CONTROL: `y @ 3i64` over a scalar. No destructor is involved at all;
// this one must be correct on every binary, and separates "the `@` door is broken"
// from "the `@` door is broken FOR A DROP TYPE".
package a07;
extern fn printf(fmt: *const u8, ...) -> i32;
fn g() -> i64 {
    let v: i64 = 3i64;
    match v {
        y @ 3i64 => { return y + 1i64; }
        _ => { return 0i64; }
    }
}
fn main() -> i32 {
    unsafe { printf("k=%ld\n".as_ptr(), g()); }
    return 0i32;
}
EOF

w a08 <<'EOF'
// a08 — OUTER AND INNER both bound: `y @ Option::Some(z)`.
package a08;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn g(p: *mut i64) -> i64 {
    let o: Option<D> = Option::Some(D { v: 2i64, c: p });
    match o {
        y @ Option::Some(z) => { return z.v; }
        Option::None => { return 0i64; }
    }
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a09 <<'EOF'
// a09 — NESTED: `y @ W { a: q, b: _ }` binds the whole AND a field.
package a09;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let w: W = W { a: D { v: 2i64, c: p }, b: 3i64 };
    match w {
        y @ W { a: q, b: _ } => { return q.v; }
    }
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a10 <<'EOF'
// a10 — REFERENCE SCRUTINEE: `match &w`, the binder is a reference, nothing moves.
package a10;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let mut k: i64 = 0i64;
    {
        let w: W = W { a: D { v: 2i64, c: p }, b: 3i64 };
        match &w {
            y @ W { .. } => { k = y.b; }
        }
    }
    return k;
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a11 <<'EOF'
// a11 — THE `let` SPELLING of the same door: `let y @ W { .. } = w;`
package a11;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let mut k: i64 = 0i64;
    {
        let w: W = W { a: D { v: 2i64, c: p }, b: 3i64 };
        let y @ W { .. } = w;
        k = y.b;
    }
    return k;
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a12 <<'EOF'
// a12 — IN A LOOP: the binder is created and destroyed once per iteration.
package a12;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let mut i: i64 = 0i64;
    let mut k: i64 = 0i64;
    while i < 2i64 {
        let w: W = W { a: D { v: 2i64, c: p }, b: 3i64 };
        match w {
            y @ W { .. } => { k = y.b; }
        }
        i = i + 1i64;
    }
    return k;
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a13 <<'EOF'
// a13 — WITH A GUARD: the guard reads the binder before the arm body runs.
package a13;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let w: W = W { a: D { v: 2i64, c: p }, b: 3i64 };
    match w {
        y @ W { .. } if y.b > 1i64 => { return y.b; }
        _ => { return 0i64; }
    }
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a14 <<'EOF'
// a14 — TWO ARMS, the `@` arm NOT taken: the other arm runs and the scrutinee
// must still be destroyed exactly once.
package a14;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn g(p: *mut i64) -> i64 {
    let o: Option<D> = Option::None;
    match o {
        y @ Option::Some(_) => { return 1i64; }
        Option::None => { return 2i64; }
    }
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

w a15 <<'EOF'
// a15 — PLAIN NAMED BINDER CONTROL, no `@` at all. Must be correct on every binary;
// it is what separates "the `@` door" from "a match arm binding a whole struct".
package a15;
extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let w: W = W { a: D { v: 2i64, c: p }, b: 3i64 };
    match w {
        y => { return y.b; }
    }
}
fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, rd(p)); }
    return 0i32;
}
EOF

echo "logos programs: $(ls "$D/logos" | wc -l)"
