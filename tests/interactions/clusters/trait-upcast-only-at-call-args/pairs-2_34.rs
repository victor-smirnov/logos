trait Named { fn name(&self) -> i64; }
trait Shape: Named { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Named for Sq { fn name(&self) -> i64 { 100 } }
impl Shape for Sq { fn area(&self) -> i64 { self.s * self.s } }
fn nm(n: &dyn Named) -> i64 { n.name() }
fn main() {
    let sq = Sq { s: 2 };
    let s: &dyn Shape = &sq;
    let n1: &dyn Named = s;
    println!("{}", n1.name());
    let b: Box<dyn Shape> = Box::new(Sq { s: 3 });
    println!("{}", nm(&*b));
    let n2: &dyn Named = &*b;
    println!("{}", n2.name());
    let bn: Box<dyn Named> = b;
    println!("{}", bn.name());
}
