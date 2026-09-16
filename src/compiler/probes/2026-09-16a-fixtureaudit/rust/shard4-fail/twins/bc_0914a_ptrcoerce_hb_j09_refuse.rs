#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(2i64);
    for x in v.iter() {
        v[0] = *x;
    }
    return 0i32;
}
fn main() {}
