#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Cur { p: *const &'static i64 }
fn set<'a>(c: &mut Cur, r: & &'a i64) {
    c.p = r;
}
fn main() {}
