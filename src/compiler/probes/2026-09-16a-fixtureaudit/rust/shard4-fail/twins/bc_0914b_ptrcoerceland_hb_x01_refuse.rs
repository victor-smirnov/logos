#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64);
    let e: &i64 = &v[0];
    (&mut v).push(4i64);
    let k: i64 = *e;
    return k as i32;
}
fn main() {}
