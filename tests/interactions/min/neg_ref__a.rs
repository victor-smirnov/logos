use std::ops::Neg;
struct P { x: i64 }
impl Neg for &P { type Output = i64; fn neg(self) -> i64 { 0 - self.x } }
fn main() { let p = P { x: 4 }; let n = -&p; println!("{}", n); }
