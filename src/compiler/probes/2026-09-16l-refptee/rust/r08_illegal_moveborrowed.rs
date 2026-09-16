trait Tr { fn m(&self) -> i64; }
struct S { v: i64 }
impl Tr for S { fn m(&self) -> i64 { self.v } }
fn inner(a: & &dyn Tr) -> i64 { a.m() }
fn main() { let s = S { v: 1 }; let rd: &dyn Tr = &s; let hold: & &dyn Tr = &rd; drop(s); let _ = inner(hold); }
