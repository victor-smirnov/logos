use std::rc::Rc;
trait Sp { fn v(&self) -> i64; }
struct A { x: i64 }
impl Sp for A { fn v(&self) -> i64 { return self.x; } }
fn use_ref(r: &dyn Sp) -> i64 { return r.v(); }
fn logos_main() -> i32 {
    let r: Rc<dyn Sp> = Rc::new(A { x: 42i64 });
    if use_ref(&*r) != 42i64 { return 3i32; }
    return 0i32;
}
fn main() { std::process::exit(logos_main()); }
