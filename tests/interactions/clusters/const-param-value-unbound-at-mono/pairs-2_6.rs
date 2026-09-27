fn cs<const K: usize>(v: &[i64]) -> i64 { let mut i: usize = 0; let mut c: i64 = 0; while i < v.len() { c += 1; i += K; if c > 10 { break; } } return c; }
fn main() { let a = [1i64, 2, 3, 4, 5]; println!("{}", cs::<2>(&a)); }
