#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
pub struct Vec<T> { only: i64, _m: std::marker::PhantomData<T> }
fn total(s: &[i64]) -> i64 {
    let mut t: i64 = 0i64;
    for x in s { t = t + *x; }
    return t;
}
fn lmain() -> i32 {
    let v: Vec<i64> = Vec { only: 11i64, _m: std::marker::PhantomData };
    let n: i64 = total(&v);
    if n != 0i64 { return 1i32; }
    return 0i32;
}
fn main() {}
