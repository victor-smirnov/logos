// TWIN: Eq -> PartialEq (supertrait and impl)
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
trait Same: PartialEq { fn same(&self, o: &Self) -> bool { self == o } }
impl Same for D {}
fn run() -> i32 {
    let a = D { v: 4 }; let b = D { v: 4 };
    if !a.same(&b) { return 1; }
    0
}
fn main() { std::process::exit(run()); }
