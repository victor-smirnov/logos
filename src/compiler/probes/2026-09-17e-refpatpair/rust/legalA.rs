struct P { x: i32 }
fn deref2(p: &&P) -> i32 { let &&P { x } = p; x }
fn main() { let p = P { x: 5 }; let r: &P = &p; let pp: &&P = &r; std::process::exit(deref2(pp) - 5); }
