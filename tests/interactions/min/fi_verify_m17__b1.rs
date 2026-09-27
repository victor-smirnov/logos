fn mk(f: i64) -> impl Fn(i64) -> i64 { move |x| x * f }
fn main() { let g = mk(2); println!("{}", g(5)); }
