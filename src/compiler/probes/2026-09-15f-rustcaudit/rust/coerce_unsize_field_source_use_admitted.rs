use std::rc::Rc;
trait Sp { fn v(&self) -> i64; }
struct A { x: i64 }
impl Sp for A { fn v(&self) -> i64 { return self.x; } }
struct H { r: Rc<A> }
fn sinkrc(r: Rc<dyn Sp>) -> i64 { return r.v(); }
fn main() {
    let h: H = H { r: Rc::new(A { x: 7i64 }) };
    let a: i64 = sinkrc(h.r);
    if a != 7i64 { std::process::exit(1); }
    if h.r.v() != 7i64 { std::process::exit(2); }
    std::process::exit(0);
}
