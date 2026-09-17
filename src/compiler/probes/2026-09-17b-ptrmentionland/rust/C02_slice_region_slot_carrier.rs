// twin of hand/C02 — LEGAL
fn sum<'a>(s: &'a [i64]) -> i64 { s[0] + s[1] }
fn main() { let a: [i64; 2] = [4, 6]; std::process::exit((sum(&a) - 10) as i32); }
