fn level(v: i64) -> i64 { let mut i: i64 = 0; loop { if i * 10 > v { break i; } i += 1; } }
fn main() { println!("{}", level(25)); }
