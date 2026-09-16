#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
struct K { }
impl K {
    fn keep<'a>(t: &'a i64) -> &'static i64 where 'a: 'static { return t; }
}
fn error(u: &i64) {
    let r = K::keep(u);
}
fn main() { let n = 1i64; error(&n); }
