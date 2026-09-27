fn main() { let b: i64 = 3; let x: i64 = loop { match b { 1 => break b, _ => break "c" } }; std::process::exit(x as i32) }
