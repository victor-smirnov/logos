#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn take(r: &Option<i64>) -> i64 {
    let mut out: i64 = 0i64;
    match &r {
        Some(ref v) => { out = **v; }
        None => {}
    }
    return out;
}
fn lmain() -> i32 {
    let o: Option<i64> = Some(4i64);
    if take(&o) != 4i64 { return 1i32; }
    return 0i32;
}
fn main() {}
