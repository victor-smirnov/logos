fn make(k: i64) -> impl Fn(i64) -> i64 { move |x| x + k }
fn main() { let f = make(2); println!("{}", f(5)); }
