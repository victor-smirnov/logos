fn f(a: impl Copy + Into<i64>) -> i64 { let b = a; a.into() + b.into() }
fn main() { println!("{}", f(3i32)); }
