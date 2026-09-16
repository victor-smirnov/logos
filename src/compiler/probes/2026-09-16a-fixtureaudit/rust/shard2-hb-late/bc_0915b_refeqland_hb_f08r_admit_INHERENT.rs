// CONTROL TWIN: literal translation — inherent `fn lt`, no PartialOrd impl.
struct D { v: i64 }
impl D { fn lt(&self, o: &D) -> bool { self.v < o.v } }
fn less_ref(a: &D, b: &D) -> bool { a < b }
fn run() -> i32 {
    let x = D { v: 3 }; let y = D { v: 8 };
    if !less_ref(&x, &y) { return 1; }
    if less_ref(&y, &x) { return 2; }
    0
}
fn main() { std::process::exit(run()); }
