// TWIN: Eq -> PartialEq ; vec_new -> Vec::new
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let mut v: Vec<D> = Vec::new();
    v.push(D { v: 2 }); v.push(D { v: 3 }); v.push(D { v: 2 });
    let t = D { v: 2 };
    let mut n: i64 = 0;
    for x in v.iter() { if x == &t { n = n + 1; } }
    if n != 2 { return 1 + n as i32; }
    0
}
fn main() { std::process::exit(run()); }
