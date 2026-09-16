fn t3<'a, 'b, 'c>(x: &'a i64, y: &'b i64, z: &'c i64) -> i64 where 'a: 'b, 'b: 'c { *x + *y + *z }
fn f<'p, 'q, 'r>(x: &'p i64, y: &'q i64, z: &'r i64) -> i64 { t3(x, y, z) }
fn main() { let n = 1i64; let m = 2i64; let o = 4i64; std::process::exit(f(&n, &m, &o) as i32); }
