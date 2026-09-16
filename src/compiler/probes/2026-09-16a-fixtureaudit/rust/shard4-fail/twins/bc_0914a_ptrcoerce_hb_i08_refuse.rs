#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a, 'b>(x: & [&'a i64; 2]) -> *const &'b i64 {
    return x;
}
fn main() {}
