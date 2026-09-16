#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct TS(i64, i64);
fn lmain() -> i32 {
    let t: TS = TS(5i64, 6i64);
    let mut out: i64 = 0i64;
    match &t {
        TS { 0: ref a, 1: b } => { out = *a + *b; }
    }
    if out != 11i64 { return 1i32; }
    return 0i32;
}
fn main() {}
