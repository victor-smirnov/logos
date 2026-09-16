#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: Logos indexes a Vec by u64; Rust by usize.
fn lmain() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(2i64);
    let e: &i64 = &v[0];
    v[1] = 4i64;
    let _k: i64 = *e;
    return 0i32;
}
fn main() {}
