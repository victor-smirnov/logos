fn f(x: u8) -> i32 { match x { 0..=127 => 1, 128..=255 => 2 } }
fn g(x: Option<u8>) -> i32 { match x { Some(0..=127) => 1, Some(128..=255) => 2, None => 3 } }
fn main() { println!("{} {} {}", f(200), g(Some(5)), g(None)); }
