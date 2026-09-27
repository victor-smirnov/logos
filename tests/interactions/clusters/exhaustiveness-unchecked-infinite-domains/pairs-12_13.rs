fn main() { let a: u8 = 5; let v = match a { 0..=100 => 1, 101..=254 => 2 }; println!("{}", v); }
