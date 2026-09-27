trait Widen { fn widen(&self) -> i64; }
impl Widen for u8 { fn widen(&self) -> i64 { *self as i64 } }
fn narrow(n: i64) -> impl Widen { n as u8 }
fn main() { let x: u8 = narrow(3); println!("{}", x); }
