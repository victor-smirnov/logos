// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let a = D { v: 6 }; let b = D { v: 6 };
    let ra: &D = &a; let rb: &D = &b;
    let pa: &&D = &ra; let pb: &&D = &rb;
    let x: &D = *pa; let y: &D = *pb;
    if !(x == y) { return 1; }
    0
}
fn main() { std::process::exit(run()); }
