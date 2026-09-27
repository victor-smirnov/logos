


fn main() { let a: Vec<i64> = vec![1, 2]; let v = a.iter().map(|x| x * 2).collect::<Vec<_>>(); println!("{:?}", v); }
