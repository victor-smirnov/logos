fn f(x: u8) -> i64 { match x { 0..=100 => 1, 101..=254 => 2 } }
fn main() { println!("{}", f(255)); }
