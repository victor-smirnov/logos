struct Id(i64);
fn ap(f: fn(Id) -> i64) -> i64 { f(Id(3)) }
fn main() { std::process::exit(ap(|Id(k)| k) as i32) }
