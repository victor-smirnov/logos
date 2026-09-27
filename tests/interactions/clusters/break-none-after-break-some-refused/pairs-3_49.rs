fn f(n: i64) -> Option<i64> { let r = loop { if n > 3 { break Some(n); } break None; }; r }
fn g(n: i64) -> i64 { let r = 'o: loop { for i in 0..n { if i == 2 { break 'o i * 10; } } break -1; }; r }
fn main() { println!("{:?} {:?} {} {}", f(5), f(1), g(5), g(1)); }
