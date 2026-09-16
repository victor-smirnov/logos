trait Tr { fn m(&self) -> i64; }
struct S { v: i64 }
impl Tr for S { fn m(&self) -> i64 { self.v } }
fn use1(x: &dyn Tr) -> i64 { x.m() }
fn main() { let s = S { v: 2 }; std::process::exit((use1(&s) - 2) as i32); }
