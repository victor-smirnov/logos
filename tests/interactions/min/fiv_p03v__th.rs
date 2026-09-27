enum E { A(i32) }
trait Tr { fn one(&self) -> i32 { 1 } fn two(&self) -> i32 { 2 } }
impl Tr for E { }
fn main() { let e = E::A(7); println!("{}", e.two()); }
