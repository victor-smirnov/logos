trait T { fn f(&self) -> i64; }
impl<A: Copy> T for (A, A, A) { fn f(&self) -> i64 { 3 } }
fn rep<S: T>(s: &S) -> i64 { s.f() * 2 }
fn main() { println!("{}", rep(&(1i32, 2i32, 3i32))); }
