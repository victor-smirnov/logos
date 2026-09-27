use std::ops::Mul;
#[derive(Clone, Copy)]
struct V { x: i64 }
impl Mul for V { type Output = i64; fn mul(self, o: V) -> i64 { self.x * o.x } }
fn dot_self<T: Mul<Output = R> + Copy, R>(t: T) -> R { return t * t; }
fn main() { println!("{}", dot_self(V { x: 3 })); }
