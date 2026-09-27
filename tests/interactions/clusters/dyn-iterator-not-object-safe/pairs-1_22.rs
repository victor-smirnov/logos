struct Up { i: i64, n: i64 }
impl Iterator for Up { type Item = i64; fn next(&mut self) -> Option<i64> { if self.i < self.n { self.i += 1; Some(self.i) } else { None } } }
fn pick(which: i64) -> Box<dyn Iterator<Item = i64>> { if which == 0 { Box::new(Up { i: 0, n: 3 }) } else { Box::new(vec![10i64, 20, 30].into_iter()) } }
fn drain(it: &mut dyn Iterator<Item = i64>) -> i64 { let mut s = 0i64; while let Some(x) = it.next() { s += x; } s }
trait Shape { const SIDES: i64; fn sides(&self) -> i64 { Self::SIDES } }
trait Area { fn area(&self) -> i64; }
struct Tri; struct Sq2;
impl Shape for Tri { const SIDES: i64 = 3; }
impl Shape for Sq2 { const SIDES: i64 = 4; }
impl Area for Tri { fn area(&self) -> i64 { Tri.sides() * 10 } }
impl Area for Sq2 { fn area(&self) -> i64 { self.sides() * <Sq2 as Shape>::SIDES } }
fn main() {
    let mut a = pick(0);
    println!("{}", drain(&mut *a));
    let mut b = pick(1);
    println!("{:?}", b.next());
    println!("{}", drain(&mut *b));
    let shapes: Vec<Box<dyn Area>> = vec![Box::new(Tri), Box::new(Sq2)];
    let tot: i64 = shapes.iter().map(|s| s.area()).sum();
    println!("{}", tot);
}
