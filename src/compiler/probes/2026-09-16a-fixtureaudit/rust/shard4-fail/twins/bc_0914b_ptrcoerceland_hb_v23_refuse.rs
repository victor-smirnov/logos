#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(2i64);
    let c: bool = v.len() > 1;
    let e: &i64 = &v[0];
    let mut i: i64 = 0i64;
    while i < 2i64 {
        v[1] = *e;
        i = i + 1i64;
    }
    return 0i32;
}
fn main() {}
