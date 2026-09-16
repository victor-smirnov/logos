#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn id<'q>(x: &'q i64) -> &'q i64 { return x; }
fn foo<'a>(out: &mut &'a i64) {
    let arr: [i64; 2] = [1i64, 2i64];
    *out = id(&arr[1]);
}
fn main() {}
