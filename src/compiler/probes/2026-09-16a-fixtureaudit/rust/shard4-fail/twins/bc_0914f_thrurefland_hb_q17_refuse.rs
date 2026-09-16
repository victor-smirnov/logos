#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a, 'b: 'a>(x: *const &mut &'a i64, y: *const &mut &'b i64) -> bool {
    return x == y;
}
fn main() {}
