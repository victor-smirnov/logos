trait Sp { fn v(&self) -> i64; }
struct A { x: i64 }
impl Sp for A { fn v(&self) -> i64 { self.x } }
fn main() {
    let b: Box<dyn Sp> = Box::new(A { x: 42 });
    let r: &dyn Sp = &b as &dyn Sp;
    assert_eq!(r.v(), 42);
}
