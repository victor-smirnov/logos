struct Q { v: i64 }
fn main() { let a = Q { v: 1 }; let b = Q { v: 2 }; if a < b { std::process::exit(1); } }
