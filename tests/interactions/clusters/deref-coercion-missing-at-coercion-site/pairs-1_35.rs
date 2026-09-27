fn f(x: &i64) -> i64 { *x + 1 }
fn g(s: &str) -> usize { s.len() }
fn main() { let a = 5i64; let r = &a; let rr = &r; let s = "abc"; let rs = &s; println!("{} {}", f(rr), g(rs)); }
