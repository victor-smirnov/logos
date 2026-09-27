enum E { A(i32) }
trait Tr { fn get(&self) -> i32; fn one(&self) -> i32 { 1 } fn two(&self) -> i32 { self.get() } }
impl Tr for E { fn get(&self) -> i32 { 5 } }
fn main() { let e = E::A(7); println!("{} {}", e.one(), e.two()); }
