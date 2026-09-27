
fn g(v: &[i64], w: i64) -> Option<i64> { let r = 'a: loop { for x in v.iter() { if *x == w { break 'a Some(*x); } } break None; }; return r; }
fn main() { let a = [1i64, 2]; println!("{:?} {:?}", g(&a, 2), g(&a, 5)); }
