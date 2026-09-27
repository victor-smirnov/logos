trait Tr { type A; fn get(&self) -> Self::A; }
struct S;
impl Tr for S { type A = i64; fn get(&self) -> i64 { 4 } }
fn f<R: Tr<A = i64>>(r: &R) -> i64 { r.get() * 2 }
fn main() { let s = S; std::process::exit(if f(&s) == 8 { 0 } else { 1 }); }
