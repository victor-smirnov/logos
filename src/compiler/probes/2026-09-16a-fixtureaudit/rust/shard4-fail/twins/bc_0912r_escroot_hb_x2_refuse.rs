#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn id<'q>(x: &'q i64) -> &'q i64 { return x; }
fn foo<'a>(out: &mut &'a i64) {
    let v: i64 = 1i64;
    *out = id(&v);
}
fn main() {}
