#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a>(x: *mut &'static i64, y: *mut &'a i64) -> bool {
    return y == x;
}
fn main() {}
