fn t<'a, 'b>(_x: &'a i64, y: &'b i64) -> Option<&'b i64> where 'a: 'b { Some(y) }
fn f<'p, 'q>(x: &'p i64, y: &'q i64) -> Option<&'q i64> { t(x, y) }
fn main() { let n = 1i64; let m = 2i64; std::process::exit(*f(&n, &m).unwrap() as i32); }
