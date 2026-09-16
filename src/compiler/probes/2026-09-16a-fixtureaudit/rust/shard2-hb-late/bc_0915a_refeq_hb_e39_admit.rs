// TWIN of bc_0915a_refeq_hb_e39_admit.logos
// TWIN: Logos supplies `<=`/`<` from an INHERENT `fn partial_cmp`; Rust requires the PartialOrd
// trait (and its PartialEq supertrait). Rewritten as trait impls; nothing else changed.
use std::cmp::Ordering;
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
impl PartialOrd for D {
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
