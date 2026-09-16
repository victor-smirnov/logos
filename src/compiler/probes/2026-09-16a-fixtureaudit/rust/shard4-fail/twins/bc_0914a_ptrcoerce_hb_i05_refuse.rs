#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a, 'b, 'c>(x: &'c mut &'a i64) -> &'c &'b i64 {
    return x;
}
fn main() {}
