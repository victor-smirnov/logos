trait Num { fn as_i64(&self) -> i64; }
impl Num for u8 { fn as_i64(&self) -> i64 { *self as i64 } }
fn main() { let m: &dyn Num = &5; println!("{}", m.as_i64()); }
