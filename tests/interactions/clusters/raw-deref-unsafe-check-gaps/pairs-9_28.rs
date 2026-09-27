trait C { fn get(&self) -> i64; }
struct A { n: i64 }
impl C for A { fn get(&self) -> i64 { self.n } }
fn main() { let a = A { n: 4 }; let p: *const dyn C = &a; println!("{}", (*p).get()); }
