trait Tr { fn count(&self) -> i64; fn last(&self) -> i64; }
struct L { a: i64 }
impl Tr for L { fn count(&self) -> i64 { self.a } fn last(&self) -> i64 { self.a + 1 } }
fn main() { let b: Box<dyn Tr> = Box::new(L { a: 4 }); println!("{}", b.last()); println!("{}", b.count()); }
