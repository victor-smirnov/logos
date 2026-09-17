struct W<'s> { r: &'s i64 }
fn pick<'a>(x: &W<'a>) -> &'a i64 { x.r }
fn escape() -> &'static i64 { let n: i64 = 9; let w = W { r: &n }; pick(&w) }
fn main() { println!("{}", escape()); }
