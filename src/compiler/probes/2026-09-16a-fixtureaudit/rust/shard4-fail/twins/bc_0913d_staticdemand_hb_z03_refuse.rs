#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Keep { fn keep<'a>(self: &Self, t: &'a i64) -> i64 where 'a: 'static; }
struct K { b: i64 }
impl Keep for K {
    fn keep<'a>(self: &Self, t: &'a i64) -> i64 where 'a: 'static { return *t + self.b; }
}
fn f<T: Keep>(d: &T, u: &i64) -> i64 {
    return d.keep(u);
}
fn main() { let n = 1i64; let k = K { b: 1i64 }; f(&k, &n); }
