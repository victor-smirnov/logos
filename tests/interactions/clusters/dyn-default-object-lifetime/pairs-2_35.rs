trait Shape { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Clone for Sq { fn clone(&self) -> Sq { Sq { s: self.s } } }
impl Shape for Sq { fn area(&self) -> i64 { self.s * self.s } }
fn mk(t: &Sq) -> Box<dyn Shape> { Box::new(t.clone()) }
fn main() {
    let n: i64 = 5;
    let b = mk(&Sq { s: n });
    println!("{}", b.area());
}
