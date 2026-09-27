use std::ops::Mul;
#[derive(Debug, Clone, Copy)]
struct Q { n: i64, d: i64 }
fn q(n: i64, d: i64) -> Q { Q { n, d } }
impl Mul for Q { type Output = Q; fn mul(self, o: Q) -> Q { q(self.n * o.n, self.d * o.d) } }
fn main() { fn half(v: Q) -> Q { v * q(1, 2) } let h = half(q(4, 1)); println!("{:?}", h); }
