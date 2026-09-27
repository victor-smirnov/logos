trait Shape { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Shape for Sq { fn area(&self) -> i64 { self.s * self.s } }
fn side(s: &dyn Shape) -> i64 { s.area() }
fn main() {
    let x: Box<dyn Shape> = Box::new(Sq { s: 4 });
    std::process::exit(side(&(*x)) as i32);
}
