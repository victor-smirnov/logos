



trait Shape { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Shape for Sq { fn area(&self) -> i64 { return self.s * self.s; } }
fn side(s: &dyn Shape) -> i64 { return s.area(); }
fn main() {
    let mut v: Vec<Box<dyn Shape>> = Vec::new();
    v.push(Box::new(Sq { s: 4 }));
    for b in v.iter() { println!("side {}", side(&**b)); }
}
