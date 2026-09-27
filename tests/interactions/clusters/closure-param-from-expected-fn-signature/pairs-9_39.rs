fn scaled(k: i64) -> impl Fn(i64) -> i64 { move |x| x * k }
fn main() { let f = scaled(3); println!("{}", f(4)); }
