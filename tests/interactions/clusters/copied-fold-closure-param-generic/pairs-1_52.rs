use std::ops::Mul;
#[derive(Clone, Copy)]
struct M(i64);
impl Mul for M { type Output = M; fn mul(self, o: M) -> M { return M(self.0 * o.0); } }
fn main() { let ms: Vec<M> = vec![M(2), M(3), M(4)]; let p = ms.iter().copied().fold(M(1), |a, b| a * b); let q = ms.iter().fold(M(1), |a, b| a * *b); println!("{} {}", p.0, q.0); }
