fn main() { let v = vec![1i64, 2, 3]; let s: &[i64] = &v[..]; let t = &v[1..]; println!("{} {}", s.len(), t[0]); }
