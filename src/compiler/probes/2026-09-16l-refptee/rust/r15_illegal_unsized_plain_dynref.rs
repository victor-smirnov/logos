trait Tr { fn m(&self) -> i64; }
struct S { v: i64 }
impl Tr for S { fn m(&self) -> i64 { self.v } }
fn take(x: &dyn Tr) -> i64 { let y: dyn Tr = *x; y.m() }
fn main() { let s = S { v: 1 }; std::process::exit((take(&s) - 1) as i32); }
