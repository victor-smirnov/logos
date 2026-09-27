fn adder(k: i64) -> impl Fn(i64) -> i64 { move |x| x + k }
fn main() { let f = adder(5); println!("{}", f(10)); }
