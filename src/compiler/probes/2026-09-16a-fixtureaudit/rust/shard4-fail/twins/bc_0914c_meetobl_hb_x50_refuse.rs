#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
fn f<'a, 'b>(v: &mut Vec<P<'a>>, x: &'a i64, y: &'b i64) {
    v.push(P { x: x, y: y });
}
fn main() {}
