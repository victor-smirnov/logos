trait Sh { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Sh for Sq { fn area(&self) -> i64 { self.s } }
fn main() {
    let c = true;
    let b: Box<dyn Sh> = if c { Box::new(Sq { s: 5 }) } else { Box::new(Sq { s: 7 }) };
    println!("{}", b.area());
}
