struct C { n: i64 }
impl C { fn get(&self) -> i64 { self.n } }
fn main() { let b = Box::new(C { n: 4 }); println!("{}", b.get()); }
