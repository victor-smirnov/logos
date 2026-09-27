trait Shape { fn area(&self) -> i64; }
struct Sq(i64); struct R { w: i64, h: i64 }
impl Shape for Sq { fn area(&self) -> i64 { self.0 * self.0 } }
impl Shape for R { fn area(&self) -> i64 { self.w * self.h } }
impl PartialEq for dyn Shape { fn eq(&self, o: &dyn Shape) -> bool { self.area() == o.area() } }
fn main() { let x: &dyn Shape = &Sq(4); let y: &dyn Shape = &R { w: 8, h: 2 }; let z: &dyn Shape = &Sq(1); println!("{} {} {}", *x == *y, *x == *z, x.eq(y)); }
