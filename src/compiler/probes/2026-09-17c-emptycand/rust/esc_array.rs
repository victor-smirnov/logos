fn pick<'a>(x: [&'a i64; 1]) -> &'a i64 { x[0] }
fn escape() -> &'static i64 { let n: i64 = 9; pick([&n]) }
fn main() { println!("{}", escape()); }
