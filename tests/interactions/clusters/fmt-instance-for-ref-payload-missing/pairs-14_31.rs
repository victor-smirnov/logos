fn main() { let v = vec![1i64, 2]; let mut e: Vec<&i64> = Vec::new(); e.push(&v[1]); println!("{:?}", e); }
