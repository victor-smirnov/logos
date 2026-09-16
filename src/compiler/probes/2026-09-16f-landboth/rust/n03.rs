fn t<'a, 'b>(_x: &'a i64, y: &mut (&'b i64, i64)) -> i64 where 'a: 'b { *y.0 + y.1 }
fn f<'p, 'q>(x: &'p i64, y: &mut (&'q i64, i64)) -> i64 { t(x, y) }
fn main() { let n = 2i64; let m = 3i64; let mut tp: (&i64, i64) = (&m, 4i64); std::process::exit(f(&n, &mut tp) as i32); }
