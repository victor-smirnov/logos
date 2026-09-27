trait Area { fn area(&self) -> i64; }
struct Tri; struct Sq2 { k: i64 }
impl Area for Tri { fn area(&self) -> i64 { 30 } }
impl Area for Sq2 { fn area(&self) -> i64 { self.k } }
fn main() {
    let mut shapes: Vec<Box<dyn Area>> = Vec::new();
    shapes.push(Box::new(Tri)); shapes.push(Box::new(Sq2 { k: 16 }));
    let t: i64 = shapes.iter().map(|s| s.area()).sum();
    println!("{}", t);
}
