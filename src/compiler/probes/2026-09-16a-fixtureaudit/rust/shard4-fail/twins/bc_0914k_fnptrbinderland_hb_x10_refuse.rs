#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
static K: i64 = 1i64;
fn t<'a>(q: &'a i64) -> i32 {
    let f: fn(&i64) -> &'static i64 = |x: &'a i64| -> &'static i64 { return &K; };
    return 0i32;
}
fn main() {}
