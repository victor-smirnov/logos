fn make_adder(n: i64) -> impl Fn(i64) -> i64 { move |x| x + n }
fn main() { let f = make_adder(5); println!("{}", f(1)); }
