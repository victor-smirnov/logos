fn main() { let v: Vec<i64> = vec![4,5,6,7]; let b: &[i64] = &v[1..3]; println!("{}", b.len()); }
