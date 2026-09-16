fn t<'a, 'b>(x: &'a i64, y: Option<&'b i64>) -> i64 where 'a: 'b { match y { Some(r) => *x + *r, None => *x } }
fn f<'p, 'q>(x: &'p i64, y: Option<&'q i64>) -> i64 { t(x, y) }
fn main() { let n = 1i64; let m = 3i64; std::process::exit(f(&n, Some(&m)) as i32); }
