fn f() -> i64 { 3 }
fn g(xs: &[i64]) -> i64 { let mut s = 0; for x in xs { s += *x; } s }
fn main() { let mut s = 0; s += f(); println!("{} {}", s, g(&[1, 2])); }
