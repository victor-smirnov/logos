#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(2i64);
    let e: &i64 = &v[1];
    (&mut v).push(*e + 1i64);
    return v.len() as i32;
}
fn main() {}
