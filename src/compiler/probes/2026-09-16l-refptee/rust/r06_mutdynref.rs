trait Tr { fn m(&self) -> i64; }
struct S { v: i64 }
impl Tr for S { fn m(&self) -> i64 { self.v } }
fn inner(a: & &mut dyn Tr) -> i64 { a.m() }
fn main() { let mut s = S { v: 9 }; let rd: &mut dyn Tr = &mut s; std::process::exit((inner(&rd) - 9) as i32); }
