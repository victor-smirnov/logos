#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
struct W<'a> { r: &'a i64 }
impl<'a> W<'a> {
    fn keep(self: &Self) -> i64 where 'a: 'static { return *self.r; }
}
fn f(u: &i64) -> i64 {
    let w = W { r: u };
    return w.keep();
}
fn main() { let n = 1i64; f(&n); }
