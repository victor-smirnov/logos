// twin of the CLOSED queue row outlives_method_call_nonstatic_bound_refuses
struct K { b: i64 }
impl K { fn two<'a, 'b>(&self, x: &'a i64, y: &'b i64) -> i64 where 'a: 'b { let _ = self.b; *x + *y } }
fn f<'p, 'q>(k: &K, x: &'p i64, y: &'q i64) -> i64 { k.two(x, y) }
fn main() { let n: i64 = 1; let m: i64 = 2; let k = K { b: 0 }; std::process::exit(f(&k, &n, &m) as i32); }
