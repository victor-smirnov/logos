struct K;
impl K { fn stret<'a, 'b>(_x: &'a i64, y: &'b i64) -> &'b i64 where 'a: 'b { y } }
fn f<'p, 'q>(x: &'p i64, y: &'q i64) -> &'q i64 { K::stret(x, y) }
fn main() { let n = 1i64; let m = 6i64; std::process::exit(*f(&n, &m) as i32); }
