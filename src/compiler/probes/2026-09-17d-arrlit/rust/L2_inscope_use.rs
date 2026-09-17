fn pick<'a>(x: [&'a i64; 1]) -> &'a i64 { x[0] }
fn main() { let n: i64 = 4; let r = pick([&n]); std::process::exit((*r as i32) - 4); }
