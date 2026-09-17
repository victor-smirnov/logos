static V: i64 = 5;
fn pick<'a>(x: [&'a i64; 1], y: [&'a i64; 1]) -> &'a i64 { if *x[0] > *y[0] { x[0] } else { y[0] } }
fn escape() -> &'static i64 { let n: i64 = 9; pick([&V], [&n]) }
fn main() { println!("{}", escape()); }
