trait Tr { fn v(&self) -> i64; }
struct L { a: i64 }
impl Tr for L { fn v(&self) -> i64 { self.a } }
fn walk(t: &dyn Tr) -> i64 { t.v() }
fn main() { let t: Box<dyn Tr> = Box::new(L { a: 4 }); println!("{}", walk(&*t)); }
