fn f(k: u8) -> u8 { k }
fn main() { let mut s = 0u32; for k in 0..3u8 { s += f(k) as u32; } println!("{}", s); }
