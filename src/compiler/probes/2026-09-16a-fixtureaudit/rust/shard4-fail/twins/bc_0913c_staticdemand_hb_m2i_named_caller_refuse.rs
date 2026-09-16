#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
fn static_id<'a>(t: &'a i64) -> &'static i64 where 'a: 'static { return t; }
fn error<'q>(u: &'q i64) -> i64 {
    let r = static_id(u);
    return *r;
}
fn main() { let n = 1i64; error(&n); }
