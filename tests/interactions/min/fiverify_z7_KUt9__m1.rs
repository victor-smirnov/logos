fn g(x: Option<u8>) -> i32 { match x { Some(0..=127) => 1, Some(128..=255) => 2, None => 3 } }
fn main() { std::process::exit(g(Some(200))); }
