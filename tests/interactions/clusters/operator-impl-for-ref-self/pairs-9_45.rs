use std::ops::Neg;
struct P { x: i64, s: i64 }
impl Neg for &P { type Output = i64; fn neg(self) -> i64 { 0 - self.x } }
fn main() { let p = P { x: 4, s: 0 }; let r = &p; let n = -r; let m = -&p; println!("{} {}", n, m); }
