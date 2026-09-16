#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let arr: [i64; 2] = [7i64, 8i64];
    let mut out: i64 = 0i64;
    match &arr {
        [ref p, q] => { out = *p + *q; }
    }
    if out != 15i64 { return 1i32; }
    return 0i32;
}
fn main() {}
