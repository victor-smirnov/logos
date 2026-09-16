struct Inv<'a> { p: &'a mut &'a i64 }
fn t<'a, 'b>(_x: &'a i64, y: Inv<'b>) -> i64 where 'a: 'b { **y.p }
fn f<'p, 'q>(x: &'p i64, y: Inv<'q>) -> i64 { t(x, y) }
fn main() { let n = 1i64; let mut h: &i64 = &n; let w = Inv { p: &mut h }; std::process::exit(f(&n, w) as i32); }
