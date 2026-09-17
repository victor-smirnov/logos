static V: i64 = 5;
fn pick<'a>(x: [&'a i64; 1]) -> &'a i64 { x[0] }
fn get() -> &'static i64 { pick([&V]) }
fn main() { std::process::exit((*get() as i32) - 5); }
