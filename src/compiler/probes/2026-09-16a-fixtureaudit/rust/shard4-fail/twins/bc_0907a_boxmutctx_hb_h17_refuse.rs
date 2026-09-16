#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let bb: Box<Box<i64>> = Box::new(Box::new(1i64));
    **bb = 6i64;
    return 0i32;
}
fn main() {}
