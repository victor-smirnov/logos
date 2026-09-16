// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
struct H<T> { x: T }
impl<T: PartialEq> H<T> { fn same(&self, a: &T, b: &T) -> bool { a == b } }
fn run() -> i32 {
    let h: H<D> = H::<D> { x: D { v: 0 } };
    let a = D { v: 4 }; let b = D { v: 4 };
    if !h.same(&a, &b) { return 1; }
    0
}
fn main() { std::process::exit(run()); }
