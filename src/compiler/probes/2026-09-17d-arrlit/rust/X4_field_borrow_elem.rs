struct S { f: i64 }
fn pick<'a>(x: [&'a i64; 1]) -> &'a i64 { x[0] }
fn escape() -> &'static i64 { let s = S { f: 3 }; pick([&s.f]) }
fn main() { std::process::exit(*escape() as i32); }
