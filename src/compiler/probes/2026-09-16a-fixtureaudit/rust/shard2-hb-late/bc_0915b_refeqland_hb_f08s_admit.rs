// TWIN: inherent `fn lt` -> PartialOrd impl (see f08r)
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, o: &D) -> bool { self.v == o.v } }
impl PartialOrd for D { fn partial_cmp(&self, o: &D) -> Option<std::cmp::Ordering> { self.v.partial_cmp(&o.v) } }
fn less_ref(a: &D, b: &D) -> bool { a < b }
fn run() -> i32 {
    let x = D { v: 3 }; let y = D { v: 8 };
    if !less_ref(&x, &y) { return 1; }
    if less_ref(&y, &x) { return 2; }
    if !(x < y) { return 3; }
    0
}
fn main() { std::process::exit(run()); }
