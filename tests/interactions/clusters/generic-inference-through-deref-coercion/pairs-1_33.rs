fn largest<T: PartialOrd + Copy>(xs: &[T]) -> T { let mut m = xs[0]; for &x in xs.iter() { if x > m { m = x; } } return m; }
fn main() { let v: Vec<i64> = vec![5, 9, 2]; println!("{}", largest(&v)); }
