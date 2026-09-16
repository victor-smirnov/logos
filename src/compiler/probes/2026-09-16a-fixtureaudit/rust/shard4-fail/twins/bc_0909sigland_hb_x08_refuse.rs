#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct R { v: i64 }
trait Ask { fn ask(self: &Self) -> i64; }
impl Ask for R {
    fn ask(self: &R) -> bool { return self.v > 0i64; }
}
fn lmain() -> i32 {
    let r: R = R { v: 1i64 };
    return 0;
}
fn main() {}
