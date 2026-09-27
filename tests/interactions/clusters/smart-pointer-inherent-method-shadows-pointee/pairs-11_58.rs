struct C { n: i32 }
impl C { fn get(&self) -> i32 { self.n } }
fn main() { let b = Box::new(C { n: 10 }); let r = &b; println!("{} {}", b.get(), r.get()); }
