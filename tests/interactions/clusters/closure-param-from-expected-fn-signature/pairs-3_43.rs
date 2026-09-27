fn adder(n: i64) -> impl Fn(i64) -> i64 { move |x| x + n }
fn main() { let f = adder(3); println!("{}", f(4)); }
