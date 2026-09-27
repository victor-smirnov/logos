fn tot2<const N: usize>(c: &[i64; N]) -> i64 { let mut s = 0i64; for x in c.iter() { s += *x; } s }
fn tot3<const N: usize>(c: &[i64; N]) -> i64 { let mut s = 0i64; for i in 0..N { s += c[i]; } s }
fn tot4(c: &[i64; 3]) -> i64 { let mut s = 0i64; for x in c.iter() { s += *x; } s }
fn main() { let a = [1i64, 2, 3]; println!("{} {} {}", tot2(&a), tot3(&a), tot4(&a)); }
