
trait Metric { fn m(&self) -> i64; }
impl Metric for i64 { fn m(&self) -> i64 { return *self; } }
fn f(n: i64) -> impl Metric { if n == 0 { return 5i64; } return f(n - 1); }
fn main() { println!("{}", f(3).m()); }
