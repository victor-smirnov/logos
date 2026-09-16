#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn t<'r>(g: fn(&'r i64) -> i64) -> i32 {
    let p: (fn(&i64) -> i64, i32) = (g, 1i32);
    return 0i32;
}
fn main() {}
