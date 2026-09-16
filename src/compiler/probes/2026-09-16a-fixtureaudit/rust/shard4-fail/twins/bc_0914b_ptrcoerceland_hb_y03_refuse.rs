#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a>(x: &[&'a i64; 2]) -> *const &'static i64 {
    return x;
}
fn main() {}
