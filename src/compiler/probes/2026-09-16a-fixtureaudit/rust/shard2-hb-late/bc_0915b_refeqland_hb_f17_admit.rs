// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let mut a = D { v: 1 }; let mut b = D { v: 1 };
    let mut ra: &mut D = &mut a; let mut rb: &mut D = &mut b;
    let x: &mut &mut D = &mut ra; let y: &mut &mut D = &mut rb;
    if x != y { return 1; }
    0
}
fn main() { std::process::exit(run()); }
