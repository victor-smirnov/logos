struct Co<'a> { r: &'a i64 }
fn t<'a, 'b>(x: &'a i64, y: Co<'b>) -> i64 where 'a: 'b { *x + *y.r }
fn f<'p, 'q>(x: &'p i64, y: Co<'q>) -> i64 { t(x, y) }
fn main() { let n = 1i64; let m = 2i64; let w = Co { r: &m }; std::process::exit(f(&n, w) as i32); }
