fn apply(f: impl Fn(i64) -> i64, x: i64) -> i64 { f(x) }
fn main() { let b: Box<dyn Fn(i64) -> i64> = Box::new(|x: i64| x * 2); println!("{}", apply(b, 21)); }
