struct Tmp(i64);
impl Drop for Tmp { fn drop(&mut self) { println!("drop {}", self.0); } }
impl Tmp { fn v(&self) -> i64 { self.0 } }
fn main() { println!("A {} {}", Tmp(1).v(), Tmp(2).v()); }
