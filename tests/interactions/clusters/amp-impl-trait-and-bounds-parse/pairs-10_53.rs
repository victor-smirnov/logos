fn ap(f: &impl Fn(i64) -> i64, x: i64) -> i64 { f(x) }
fn main() { println!("{}", ap(&|x| x + 1, 4)); }
