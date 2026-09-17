static V: i64 = 5;
fn pick2<'a>(x: [&'a i64; 2]) -> &'a i64 { x[1] }
fn escape() -> &'static i64 { let n: i64 = 8; pick2([&V, &n]) }
fn main() { std::process::exit(*escape() as i32); }
