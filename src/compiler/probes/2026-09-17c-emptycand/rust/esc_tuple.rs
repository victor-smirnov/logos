fn pick<'a>(x: (&'a i64, i64)) -> &'a i64 { x.0 }
fn escape() -> &'static i64 { let n: i64 = 9; pick((&n, 1)) }
fn main() { println!("{}", escape()); }
