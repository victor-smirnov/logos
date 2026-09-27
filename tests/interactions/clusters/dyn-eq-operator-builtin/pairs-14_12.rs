trait Shape { fn area(&self) -> i64; }
struct Sq(i64);
impl Shape for Sq { fn area(&self) -> i64 { self.0 * self.0 } }
fn main() { let x: &dyn Shape = &Sq(4); let y: &dyn Shape = &Sq(4); println!("{}", *x == *y); }
