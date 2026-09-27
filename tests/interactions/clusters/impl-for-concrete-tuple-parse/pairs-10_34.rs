trait T { fn f(&self) -> i64; }
impl T for (i64, i64) { fn f(&self) -> i64 { self.0 + self.1 } }
fn main() { println!("{}", (1i64, 2i64).f()); }
