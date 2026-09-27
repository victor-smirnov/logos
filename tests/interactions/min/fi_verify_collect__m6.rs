fn main() { let a: Vec<i64> = vec![1, 2]; let v = a.into_iter().collect::<Vec<_>>(); println!("{}", v.len()); let w: Vec<_> = vec![1i64].iter().map(|x| x * 2).collect(); println!("{}", w.len()); }
