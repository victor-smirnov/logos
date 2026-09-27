fn evens<'a>(v: &'a Vec<i64>) -> impl Iterator<Item = &'a i64> + 'a { v.iter().filter(|x| **x % 2 == 0) }
fn main() { let v: Vec<i64> = vec![1, 2, 3, 4]; let mut t: i64 = 0; for x in evens(&v) { t += *x; } println!("{}", t); }
