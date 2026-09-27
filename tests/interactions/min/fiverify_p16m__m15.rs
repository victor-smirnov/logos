fn main() { let v: Vec<Option<i64>> = vec![Some(1), Some(2), Some(3)]; let mut n: i64 = 0; for p in &v { if let Some(x) = p { n += *x; } } println!("{}", n); }
