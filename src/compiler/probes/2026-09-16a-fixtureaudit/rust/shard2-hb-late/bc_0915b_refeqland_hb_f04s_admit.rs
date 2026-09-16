// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let mut a = D { v: 1 }; let b = D { v: 9 };
    let rb: &D = &b;
    let ra: &mut D = &mut a;
    ra.v = 9;
    let r1: bool = ra == rb;
    if !r1 { return 1; }
    0
}
fn main() { std::process::exit(run()); }
