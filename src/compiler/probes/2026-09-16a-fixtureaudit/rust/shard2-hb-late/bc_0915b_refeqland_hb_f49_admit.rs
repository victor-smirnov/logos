// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
struct Pair<'a> { l: &'a D, r: &'a D }
fn run() -> i32 {
    let a = D { v: 4 }; let b = D { v: 4 };
    let p = Pair { l: &a, r: &b };
    if p.l != p.r { return 1; }
    0
}
fn main() { std::process::exit(run()); }
