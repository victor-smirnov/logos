fn f() -> i64 { loop { break 3; } }
fn main() { println!("{}", f()); }
