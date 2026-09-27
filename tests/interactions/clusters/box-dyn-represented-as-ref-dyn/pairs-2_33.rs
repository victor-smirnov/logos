trait Shape { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Shape for Sq { fn area(&self) -> i64 { self.s * self.s } }
impl Shape for Box<dyn Shape> { fn area(&self) -> i64 { (**self).area() + 1 } }
fn get<T: Shape>(t: &T) -> i64 { t.area() }
fn main() {
    let b: Box<dyn Shape> = Box::new(Sq { s: 3 });
    println!("{}", get(&b));
}
