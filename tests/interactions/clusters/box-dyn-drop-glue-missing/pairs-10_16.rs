trait S { fn a(&self) -> i64; }
struct N { nm: String, side: i64 }
impl S for N { fn a(&self) -> i64 { self.side } }
fn main() { let mut v: Vec<Box<dyn S>> = Vec::new(); v.push(Box::new(N { nm: String::from("sq"), side: 4 })); println!("{}", v[0].a()); }
