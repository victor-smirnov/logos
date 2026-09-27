fn half(v: i64) -> Option<i64> { if v % 2 == 0 { Some(v / 2) } else { None } }
fn main() { let xs = [4i64, 3, 8]; let r: Vec<Option<i64>> = xs.iter().map(|x| { let h = half(*x)?; Some(h + 1) }).collect(); println!("{:?}", r); }
