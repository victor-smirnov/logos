// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn boom(n: &mut i64) -> bool { *n = *n + 100; true }
fn run() -> i32 {
    let a = D { v: 4 }; let b = D { v: 4 };
    let ra: &D = &a; let rb: &D = &b;
    let mut n: i64 = 0;
    let flag: bool = true;
    if !(flag && ra == rb) { return 1; }
    if !(ra == rb || boom(&mut n)) { return 2; }
    if n != 0 { return 3; }
    0
}
fn main() { std::process::exit(run()); }
