trait Tr { type A; fn get(&self) -> Self::A; }
struct S;
impl Tr for S { type A = i64; fn get(&self) -> i64 { 4 } }
fn twice<R: Tr<A = i64>>(r: &R) -> i64 { r.get() + r.get() }
fn main() { let s = S; println!("{}", twice(&s)); }
