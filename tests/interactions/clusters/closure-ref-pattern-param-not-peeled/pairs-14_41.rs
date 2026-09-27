fn main() { let v = vec![1i64, 2, 5]; let u: Vec<i64> = v.iter().map(|&x| { let y: i64 = x; y * 2 }).collect(); println!("{:?}", u); }
