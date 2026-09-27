trait S { fn a(&self) -> i64; }
struct R { w: i64, h: i64 }
struct Q { s: i64, t: i64 }
impl S for R { fn a(&self) -> i64 { self.w * self.h } }
impl S for Q { fn a(&self) -> i64 { self.s + self.t } }
fn main() { let v: Vec<Box<dyn S>> = vec![Box::new(R { w: 2, h: 3 }), Box::new(Q { s: 1, t: 1 })]; let mut t = 0i64; for x in v.iter() { t += x.a(); } println!("{}", t); }
