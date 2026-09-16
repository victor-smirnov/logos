#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a>(x: *mut &'a i64, y: *mut &'static i64) -> bool { return x == y; }
fn main() {}
