#![allow(dead_code, unused_variables, unused_mut)]
// TWIN v2: v1 wrote `**v`, one deref too many under 2024 ergonomics, and died
// on E0614 before the binding-modifier verdict was reached.
fn take(r: &Option<i64>) -> i64 {
    let mut out: i64 = 0i64;
    match &r {
        Some(ref v) => { out = *v; }
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
