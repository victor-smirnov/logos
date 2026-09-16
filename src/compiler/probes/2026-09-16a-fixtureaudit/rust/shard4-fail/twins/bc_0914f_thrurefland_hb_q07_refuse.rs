#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a, 'b>(x: *mut fn(&'a i64), y: *mut fn(&'b i64)) -> bool {
    return x == y;
}
fn main() {}
