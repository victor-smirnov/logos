fn cnt<T>(xs: &[T]) -> usize { xs.len() }
fn main() { let v: Vec<i64> = vec![4, 5, 6]; println!("{}", cnt(&v)); println!("{}", cnt(&v[..])); }
