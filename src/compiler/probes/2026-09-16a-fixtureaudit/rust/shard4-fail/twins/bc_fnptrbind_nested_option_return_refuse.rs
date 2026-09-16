#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn t<'r>(g: fn(&'r i64) -> i64) -> Option<fn(&i64) -> i64> {
    return Some(g);
}
fn main() {}
