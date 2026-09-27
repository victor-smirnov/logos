use std::ops::Neg;
#[derive(Clone, Copy)]
struct V(i32);
impl Neg for V { type Output = V; fn neg(self) -> V { V(-self.0) } }
fn ng<A: Neg<Output = V> + Copy>(a: A) -> V { -a }
fn main() { println!("{}", ng(V(3)).0); }
