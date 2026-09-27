fn find(xs: &[i64], t: i64) -> Option<i64> { let mut i = 0i64; let r = loop { if i >= xs.len() as i64 { break None; } if xs[i as usize] == t { break Some(i); } i += 1; }; return r; }
fn main() { println!("{:?} {:?}", find(&[4, 5, 6], 6), find(&[4], 9)); }
