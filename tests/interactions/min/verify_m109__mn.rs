struct G { d: i64 }
impl Drop for G { fn drop(&mut self) { println!("drop {}", self.d); } }
fn f(n: i64) -> i64 { let _g = G { d: n }; n + G { d: 10 }.d }
fn main() { println!("{}", f(1)); }
