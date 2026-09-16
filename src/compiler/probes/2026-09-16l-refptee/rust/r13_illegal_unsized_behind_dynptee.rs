trait Tr { fn m(&self) -> i64; }
struct S { v: i64 }
impl Tr for S { fn m(&self) -> i64 { self.v } }
fn take(a: &&dyn Tr) -> i64 { let y: dyn Tr = **a; y.m() }
fn main() { let s = S { v: 1 }; let rd: &dyn Tr = &s; std::process::exit((take(&rd) - 1) as i32); }
