fn mul(k: i64) -> Box<dyn Fn(i64) -> i64> { Box::new(move |x: i64| x * k) }
fn main() { let b = mul(3); println!("{}", b(10)); let c = mul(7); println!("{} {}", c(2), b(2)); }
