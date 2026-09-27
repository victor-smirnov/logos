use std::ops::Add;
enum V { A, B, C }
impl Add for V { type Output = V; fn add(self, _o: V) -> V { V::A } }
fn main() { let v = V::B + V::B; std::process::exit(match v { V::A => 10, V::B => 11, V::C => 12 }) }
