#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct W<'x> { r: &'x i64 }
fn f<'a, 'b>(x: *mut W<'a>, y: *mut W<'b>) -> bool {
    return x == y;
}
fn main() {}
