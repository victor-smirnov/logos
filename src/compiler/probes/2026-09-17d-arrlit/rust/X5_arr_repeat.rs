fn pick2<'a>(x: [&'a i64; 2]) -> &'a i64 { x[0] }
fn escape() -> &'static i64 { let n: i64 = 2; pick2([&n; 2]) }
fn main() { std::process::exit(*escape() as i32); }
