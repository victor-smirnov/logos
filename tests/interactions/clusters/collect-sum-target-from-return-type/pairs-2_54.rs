fn t(v: &Vec<i64>) -> i64 { v.iter().map(|x| *x * 2).sum() }
fn u(v: &Vec<i64>) -> i64 { return v.iter().map(|x| *x * 2).sum(); }
fn main() { let v: Vec<i64> = vec![1, 2]; println!("{} {}", t(&v), u(&v)); }
