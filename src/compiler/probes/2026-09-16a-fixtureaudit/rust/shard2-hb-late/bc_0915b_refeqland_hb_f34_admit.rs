// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn differ<'a, 'b>(x: &'a D, y: &'b D) -> bool { x != y }
fn run() -> i32 {
    let a = D { v: 2 }; let b = D { v: 3 }; let c = D { v: 2 };
    if !differ(&a, &b) { return 1; }
    if differ(&a, &c) { return 2; }
    0
}
fn main() { std::process::exit(run()); }
