trait Sp { fn v(&self) -> i64; }
struct A { x: i64 }
impl Sp for A { fn v(&self) -> i64 { self.x } }
fn takes_boxref(b: &Box<dyn Sp>) -> i64 { b.v() }
fn main() {
    let b: Box<dyn Sp> = Box::new(A { x: 42 });
    assert_eq!(takes_boxref(&b), 42);
}
