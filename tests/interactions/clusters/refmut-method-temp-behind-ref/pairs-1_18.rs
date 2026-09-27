struct C { n: i64 }
impl C { fn bump(&mut self) -> i64 { self.n += 1; self.n } }
fn main() { let mut v: Vec<Box<C>> = Vec::new(); v.push(Box::new(C { n: 0 })); println!("{}", v[0].bump()); }
