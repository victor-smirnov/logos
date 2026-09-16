struct K { _b: i64 }
impl K { fn two<'a, 'b>(&self, _x: &'a i64, y: &'b i64) -> &'b i64 where 'a: 'b { y } }
fn f<'p, 'q>(k: &K, x: &'p i64, y: &'q i64) -> &'q i64 { k.two(x, y) }
fn main() { let n = 1i64; let m = 5i64; let k = K { _b: 0i64 }; std::process::exit(*f(&k, &n, &m) as i32); }
