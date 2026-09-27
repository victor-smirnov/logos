struct D;
fn f(d: &D) -> i32 { 3 }
fn main() { println!("{}", f(&D)); }
