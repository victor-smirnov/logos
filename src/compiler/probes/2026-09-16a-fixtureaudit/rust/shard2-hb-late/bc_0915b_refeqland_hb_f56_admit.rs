// TWIN: Eq -> PartialEq
struct W<T> { x: T }
impl PartialEq for W<i64> { fn eq(&self, other: &W<i64>) -> bool { self.x == other.x } }
fn run() -> i32 {
    let a: W<i64> = W::<i64> { x: 5 }; let b: W<i64> = W::<i64> { x: 5 };
    let ra: &W<i64> = &a; let rb: &W<i64> = &b;
    if ra != rb { return 1; }
    0
}
fn main() { std::process::exit(run()); }
