#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Inv<'x> { m: &'x mut &'x i64 }
fn f<'a, 'b>(x: *const Inv<'a>, y: *const Inv<'b>) -> bool {
    return x == y;
}
fn main() {}
