trait Base { fn v(&self) -> i64; }
trait Ext: Base { fn w(&self) -> i64; }
struct A { x: i64 }
impl Base for A { fn v(&self) -> i64 { self.x } }
impl Ext for A { fn w(&self) -> i64 { self.x + 1 } }
fn up(p: &dyn Ext) -> &dyn Base { p }
fn main() {
    let a = A { x: 3 };
    let e: &dyn Ext = &a;
    let b: &dyn Base = e;
    println!("{} {} {}", b.v(), up(e).v(), e.w());
    let be: Box<dyn Ext> = Box::new(A { x: 5 });
    let bb: Box<dyn Base> = be;
    println!("{}", bb.v());
}
