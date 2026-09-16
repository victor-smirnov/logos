// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn same<T: PartialEq>(a: &T, b: &T) -> bool {
    let x: &T = a; let y: &T = b;
    x == y
}
fn run() -> i32 {
    let a = D { v: 6 }; let b = D { v: 6 };
    if !same::<D>(&a, &b) { return 1; }
    0
}
fn main() { std::process::exit(run()); }
