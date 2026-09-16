#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct S { k: i64 }
fn id<'q>(x: &'q i64) -> &'q i64 { return x; }
fn foo<'a>(out: &mut &'a i64) {
    let s: S = S { k: 5i64 };
    *out = id(&s.k);
}
fn main() {}
