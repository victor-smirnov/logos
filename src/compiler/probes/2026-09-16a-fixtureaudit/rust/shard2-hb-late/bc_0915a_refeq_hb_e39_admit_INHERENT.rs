// CONTROL TWIN: the Logos program TRANSLATED LITERALLY — inherent `partial_cmp`, no PartialOrd impl.
use std::cmp::Ordering;
struct D { v: i64 }
impl D {
    fn partial_cmp(&self, other: &D) -> Option<Ordering> {
        if self.v < other.v { return Some(Ordering::Less); }
        if self.v > other.v { return Some(Ordering::Greater); }
        Some(Ordering::Equal)
    }
}
fn run() -> i32 {
    let a = D { v: 6 }; let b = D { v: 6 };
    let ra: &D = &a; let rb: &D = &b;
    if !(ra <= rb) { return 1; }
    if !(rb <= ra) { return 2; }
    if ra < rb { return 3; }
    0
}
fn main() { std::process::exit(run()); }
