#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn mk(step: i64) -> impl Fn(i64) -> i64 {
    let mut acc: i64 = 0i64;
    return move |d: i64| -> i64 { acc = acc + d + step; return acc; };
}
fn lmain() -> i32 {
    let f = mk(1i64);
    return f(2i64) as i32;
}
fn main() {}
