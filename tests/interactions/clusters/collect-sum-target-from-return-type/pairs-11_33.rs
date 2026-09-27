fn t1(xs: &[i64]) -> i64 { return xs.iter().map(|x| x * 2).sum(); }
fn t2(xs: &[i64]) -> i64 { xs.iter().map(|x| x * 2).sum() }
fn main() { println!("{} {}", t1(&[1, 2]), t2(&[3])); }
