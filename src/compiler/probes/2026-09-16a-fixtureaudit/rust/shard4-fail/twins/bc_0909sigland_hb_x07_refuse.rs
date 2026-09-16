#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: `extern fn printf` replaced by an inert `pr`.
fn pr(v: i64) { let _ = v; }
struct R { v: i64 }
trait Ask { fn ask(self: &Self) -> i64; }
impl Ask for R {
    fn ask(self: &R) -> bool { return self.v > 0i64; }
}
fn lmain() -> i32 {
    let r: R = R { v: 1i64 };
    pr(r.ask());
    return 0;
}
fn main() {}
