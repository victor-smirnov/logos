fn main() { let v: Vec<i64> = vec![3, -9, 4]; let w: Vec<i64> = v.iter().copied().map(|x| x * 2).collect(); println!("{:?}", w); }
