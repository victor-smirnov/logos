#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn pick<'a>(a: &'a i64, b: &'a i64) -> &'a i64 { if *a > *b { return a; } return b; }
fn lmain() -> i32 {
    let f: for<'z> fn(&'z i64, &'z i64) -> &'static i64 = pick;
    let x: i64 = 32i64;
    let y: i64 = 3i64;
    return *f(&x, &y) as i32;
}
fn main() {}
