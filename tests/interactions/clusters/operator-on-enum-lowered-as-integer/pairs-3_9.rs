use std::ops::Neg;
#[derive(Clone, Copy, PartialEq)]
enum Sg { Pos, Neg, Zero }
impl Neg for Sg { type Output = Sg; fn neg(self) -> Sg { match self { Sg::Pos => Sg::Neg, Sg::Neg => Sg::Pos, Sg::Zero => Sg::Zero } } }
fn show(s: Sg) -> i64 { match s { Sg::Pos => 1, Sg::Neg => -1, Sg::Zero => 0 } }
fn main() {
    let a = Sg::Neg;
    let b = -a;
    println!("{} {}", show(b), show(-Sg::Pos));
    let arr = [Sg::Neg, Sg::Zero];
    println!("{}", show(-arr[0]));
}
