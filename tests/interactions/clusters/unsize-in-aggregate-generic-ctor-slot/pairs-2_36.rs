trait Sh { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Sh for Sq { fn area(&self) -> i64 { self.s * self.s } }
fn pick(n: i64) -> Box<dyn Sh> { return Box::new(Sq { s: n }); }
fn main() {
    let shapes: Vec<Box<dyn Sh>> = vec![pick(1), pick(2)];
    println!("{}", shapes[1].area());
}
