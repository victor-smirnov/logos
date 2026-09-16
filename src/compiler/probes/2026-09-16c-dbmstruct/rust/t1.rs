struct K { b: i64 }
impl K { fn two<'a,'b>(&self, x: &'a i64, y: &'b i64) -> i64 where 'a: 'b { return *x + *y; } }
fn g<'p,'q>(k: &K, x: &'p i64, y: &'q i64) -> i64 { return k.two(x, y); }
fn main() { let n=1i64; let m=2i64; let k=K{b:0}; std::process::exit(g(&k,&n,&m) as i32); }
