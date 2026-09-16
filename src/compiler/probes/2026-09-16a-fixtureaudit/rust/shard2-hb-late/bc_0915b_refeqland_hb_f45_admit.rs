// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn same<T>(a: &T, b: &T) -> bool where T: PartialEq { a == b }
fn run() -> i32 {
    let a = D { v: 4 }; let b = D { v: 4 };
    if !same::<D>(&a, &b) { return 1; }
    0
}
fn main() { std::process::exit(run()); }
