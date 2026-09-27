#[derive(PartialEq, PartialOrd)]
struct Q { n: i64 }
fn main() { let a = Q { n: 1 }; let b = Q { n: 2 }; std::process::exit(if a < b { 0 } else { 1 }); }
