#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P { x: i64, y: i64 }
fn lmain() -> i32 {
    let mut p: P = P { x: 3i64, y: 4i64 };
    match &mut p {
        P { x: ref mut v, .. } => { *v = *v + 1i64; }
    }
    if p.x != 4i64 { return 1i32; }
    return 0i32;
}
fn main() {}
