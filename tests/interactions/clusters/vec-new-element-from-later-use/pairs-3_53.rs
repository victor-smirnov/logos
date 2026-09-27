fn largest<T: PartialOrd + Copy>(xs: &[T]) -> T { let mut m = xs[0]; for &x in xs { if x > m { m = x; } } m }
fn main() { println!("{}", largest(&[1.5f64, -2.0])); }
