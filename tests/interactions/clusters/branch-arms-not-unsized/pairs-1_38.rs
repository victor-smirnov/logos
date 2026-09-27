fn pick(k: i64) -> Box<dyn Fn(i64) -> i64> { let b: Box<dyn Fn(i64) -> i64> = match k { 0 => Box::new(|x: i64| x + 1), n => Box::new(move |x: i64| x * n) }; return b; }
fn main() { let a = pick(0); let b = pick(3); println!("{} {}", a(10), b(10)); }
