



trait Shape { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Shape for Sq { fn area(&self) -> i64 { return self.s * self.s; } }
fn first(shapes: &Vec<Box<dyn Shape>>, k: i64) -> Option<&Box<dyn Shape>> {
    for s in shapes.iter() { if s.area() > k { return Some(s); } }
    return None;
}
fn main() {
    let mut v: Vec<Box<dyn Shape>> = Vec::new();
    v.push(Box::new(Sq { s: 2 }));
    v.push(Box::new(Sq { s: 4 }));
    if let Some(b) = first(&v, 5) { println!("{}", b.area()); }
}
