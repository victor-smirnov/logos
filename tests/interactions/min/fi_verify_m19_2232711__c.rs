use std::ops::Add;
struct M<T>(T);
impl<T> Add for M<T> { type Output = M<T>; fn add(self, _o: Self) -> Self { self } }
fn main() { let c = M(1i64) + M(2i64); println!("{}", c.0); }
