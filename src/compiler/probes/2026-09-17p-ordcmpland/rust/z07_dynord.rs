trait Tr { fn v(&self) -> i64; }
struct S { a: i64 }
impl Tr for S { fn v(&self) -> i64 { self.a } }
fn main() { let x = S { a: 1 }; let y = S { a: 2 }; let p: &dyn Tr = &x; let q: &dyn Tr = &y;
    if p < q { std::process::exit(1); } let _ = p.v() + q.v(); }
