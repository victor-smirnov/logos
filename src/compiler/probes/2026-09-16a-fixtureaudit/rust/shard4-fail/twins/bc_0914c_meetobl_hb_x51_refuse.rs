#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
fn f<'a, 'b>(slot: &mut Option<P<'a>>, x: &'a i64, y: &'b i64) {
    *slot = Some(P { x: x, y: y });
}
fn main() {}
