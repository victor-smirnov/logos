trait Num { fn as_i64(&self) -> i64; }
impl Num for i32 { fn as_i64(&self) -> i64 { *self as i64 } }
fn main() { let m: &dyn Num = &5; std::process::exit(m.as_i64() as i32) }
