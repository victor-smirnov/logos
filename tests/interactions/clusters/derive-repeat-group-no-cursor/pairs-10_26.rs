#[derive(Clone)]
struct P { x: i64 }
fn main() { let p = P { x: 1 }; let q = p.clone(); println!("{}", q.x + p.x); }
