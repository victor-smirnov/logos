fn pick<'a>(x: [[&'a i64; 1]; 1]) -> &'a i64 { x[0][0] }
fn escape() -> &'static i64 { let n: i64 = 6; pick([[&n]]) }
fn main() { std::process::exit(*escape() as i32); }
