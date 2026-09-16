trait Sp { fn v(&self) -> i64; }
struct A { x: i64 }
impl Sp for A { fn v(&self) -> i64 { self.x } }
struct H<'a> { r: &'a dyn Sp }
fn main() {
    let b: Box<dyn Sp> = Box::new(A { x: 42 });
    let h = H { r: &b };
    assert_eq!(h.r.v(), 42);
}
