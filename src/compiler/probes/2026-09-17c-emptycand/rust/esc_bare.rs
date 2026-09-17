fn id<'a>(x: &'a i64) -> &'a i64 { x }
fn escape() -> &'static i64 { let n: i64 = 9; id(&n) }
fn main() { println!("{}", escape()); }
