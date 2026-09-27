fn h(n: &Option<i64>) -> i64 { match n { Some(b) => *b, None => 0 } }
fn main() { println!("{}", h(&None)); }
