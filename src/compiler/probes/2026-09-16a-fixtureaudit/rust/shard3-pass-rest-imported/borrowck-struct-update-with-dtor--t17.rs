struct T { a: i64, b: String }
impl Drop for T { fn drop(&mut self) {} }
fn f(s0: T) -> i32 { let _s2 = T { a: 2i64, ..s0 }; return 0i32; }
fn main() { let _ = f; }
