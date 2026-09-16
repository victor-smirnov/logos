#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a, 'b>(x: &mut &'a i64) {
    let y: *mut &'b i64 = x;
}
fn main() {}
