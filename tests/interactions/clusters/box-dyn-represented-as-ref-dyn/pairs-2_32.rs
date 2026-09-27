trait Shape { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Shape for Sq { fn area(&self) -> i64 { self.s * self.s } }
fn one<T: Shape + ?Sized>(x: &Box<T>) -> i64 { x.area() }
fn main() {
    let b: Box<dyn Shape> = Box::new(Sq { s: 3 });
    let r: i64 = one(&b);
    println!("{}", r);
    let r2: i64 = one::<dyn Shape>(&b);
    println!("{}", r2);
}
