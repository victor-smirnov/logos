trait Sh { fn area(&self) -> i64; }
struct Sq { s: i64 }
struct Ci { r: i64 }
impl Sh for Sq { fn area(&self) -> i64 { self.s * self.s } }
impl Sh for Ci { fn area(&self) -> i64 { 3 * self.r * self.r } }
fn main() {
    let n: i64 = 2;
    let b: Box<dyn Sh> = match n { 0 => Box::new(Sq { s: 1 }), _ => Box::new(Ci { r: n }) };
    println!("{}", b.area());
}
