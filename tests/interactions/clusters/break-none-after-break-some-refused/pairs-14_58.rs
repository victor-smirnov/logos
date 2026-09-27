fn main() { let v = vec![3i64, 8]; let r = loop { for x in v.iter() { if *x > 5 { break; } } if v.len() > 5 { break Some(1i64); } break None; }; println!("{:?}", r); }
