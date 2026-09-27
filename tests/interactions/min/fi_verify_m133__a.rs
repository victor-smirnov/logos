fn id() -> impl Fn(i64) -> i64 { |x| x }
fn main() { let f = id(); println!("{}", f(4)); }
