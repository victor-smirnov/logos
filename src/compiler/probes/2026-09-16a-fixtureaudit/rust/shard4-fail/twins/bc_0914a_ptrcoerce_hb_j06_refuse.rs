#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut v: Vec<Vec<i64>> = Vec::new();
    let mut w: Vec<i64> = Vec::new();
    w.push(1i64); w.push(2i64);
    v.push(w);
    let e: &Vec<i64> = &v[0];
    let mut z: Vec<i64> = Vec::new();
    v[0] = z;
    let _n: usize = e.len();
    return 0i32;
}
fn main() {}
