trait Res { fn name(&self) -> i64; }
struct G { id: i64 }
impl Res for G { fn name(&self) -> i64 { self.id } }
struct Holder { a: Box<dyn Res> }
fn main() { let h = Holder { a: Box::new(G { id: 1 }) }; println!("{}", h.a.name()); }
