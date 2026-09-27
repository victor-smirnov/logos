use std::ops::{Mul, Not};
#[derive(Clone, Copy, PartialEq, Debug)]
enum Sg { Pos, Neg, Zero }
#[derive(Debug)]
enum E { A(i64), B { x: i64, y: i64 }, C }
impl Mul for Sg { type Output = Sg; fn mul(self, o: Sg) -> Sg { if self == Sg::Zero || o == Sg::Zero { Sg::Zero } else if self == o { Sg::Pos } else { Sg::Neg } } }
impl Not for Sg { type Output = Sg; fn not(self) -> Sg { match self { Sg::Pos => Sg::Neg, Sg::Neg => Sg::Pos, Sg::Zero => Sg::Zero } } }
fn main() {
    println!("{}", Sg::Neg * Sg::Neg == Sg::Pos);
    println!("{}", Sg::Pos * Sg::Neg == Sg::Neg);
    println!("{}", !Sg::Pos == Sg::Neg);
    println!("{:?} {:?} {:?}", E::A(1), E::B { x: 2, y: 3 }, E::C);
}
