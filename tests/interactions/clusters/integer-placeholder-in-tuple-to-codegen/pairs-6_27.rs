fn main() { let mut k = 0u8; let found = loop { for j in 0..10u8 { if j * k > 20 { break; } } k += 1; if k > 5 { break (k, 1); } }; println!("{:?}", found); }
