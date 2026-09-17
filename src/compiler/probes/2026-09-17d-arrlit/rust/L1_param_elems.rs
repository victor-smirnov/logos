fn pick<'a>(x: [&'a i64; 1]) -> &'a i64 { x[0] }
fn thru<'a>(p: &'a i64) -> &'a i64 { pick([p]) }
fn main() { let n: i64 = 7; let r = thru(&n); std::process::exit((*r as i32) - 7); }
