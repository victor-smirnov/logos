#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a>(q: *mut &'a i64) -> i64 {
    let p: *const &'static i64 = q;
    return 0i64;
}
fn main() {}
