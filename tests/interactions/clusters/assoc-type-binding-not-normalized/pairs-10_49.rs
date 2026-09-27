trait Sc { type Out; fn apply(&self) -> Self::Out; }
struct D { v: i32 }
impl Sc for D { type Out = i64; fn apply(&self) -> i64 { self.v as i64 * 3 } }
fn doubled<S: Sc<Out = i64>>(s: &S) -> i64 { s.apply() * 2 }
fn main() { println!("{}", doubled(&D { v: -5 })); }
