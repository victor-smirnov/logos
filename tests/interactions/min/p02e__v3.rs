trait Widen { fn widen(self) -> i64; }
impl Widen for i64 { fn widen(self) -> i64 { self } }
fn one<T: Widen + Copy>(x: &T) -> i64 { x.widen() }
fn main() { let b = 7i64; std::process::exit(one(&b) as i32); }
