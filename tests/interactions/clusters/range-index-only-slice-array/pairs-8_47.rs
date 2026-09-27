


fn n(v: &[i32]) -> i64 { v.len() as i64 }
fn main() { let v: Vec<i32> = vec![7, 8]; let a = n(&v[..]); println!("{}", a); println!("{}", n(&v[1..])); }
