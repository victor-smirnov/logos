#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'r>(g: fn(&'r i64) -> i64) -> fn(&i64) -> i64 {
    return g;
}
fn main() {}
