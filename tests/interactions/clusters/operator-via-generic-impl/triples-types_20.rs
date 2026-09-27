use std::ops::Add;
struct M<T>(T);
impl<T: Add<Output = T>> Add for M<T> { type Output = M<T>; fn add(self, o: Self) -> Self { M(self.0 + o.0) } }
fn main() { let c = M(1i64) + M(2i64); println!("{}", c.0); }
