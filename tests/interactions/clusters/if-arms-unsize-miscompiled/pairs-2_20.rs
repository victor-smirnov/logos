trait Sh { fn area(&self) -> i64; }
struct Sq { s: i64 }
struct Ci { r: i64 }
impl Sh for Sq { fn area(&self) -> i64 { self.s * self.s } }
impl Sh for Ci { fn area(&self) -> i64 { 3 * self.r * self.r } }
fn main() {
    for n in 0..2 {
        let b: Box<dyn Sh> = if n % 2 == 0 { Box::new(Sq { s: n }) } else { Box::new(Ci { r: n }) };
        println!("{}", b.area());
    }
}
