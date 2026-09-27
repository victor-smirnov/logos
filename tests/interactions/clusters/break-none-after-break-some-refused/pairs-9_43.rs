fn f(n: i64) -> Option<i64> { let r = loop { if n > 2 { break Some(n); } break None; }; r }
fn main() { println!("{:?} {:?}", f(3), f(1)); }
