static V: i64 = 5;
fn pick<'a>(x: (&'a i64, i64), y: (&'a i64, i64)) -> &'a i64 { if x.1 > y.1 { x.0 } else { y.0 } }
fn escape() -> &'static i64 { let n: i64 = 9; pick((&V,1), (&n,2)) }
fn main() { println!("{}", escape()); }
