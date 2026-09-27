trait Shape { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Shape for Sq { fn area(&self) -> i64 { self.s * self.s } }
fn fill<T: Shape + 'static>(t: T) -> Vec<Box<dyn Shape>> {
    let mut out: Vec<Box<dyn Shape>> = Vec::new();
    out.push(Box::new(t));
    out
}
fn main() {
    let v = fill(Sq { s: 5 });
    println!("{}", v[0].area());
}
