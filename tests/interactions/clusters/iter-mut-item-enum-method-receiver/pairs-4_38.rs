enum Shape { Circle(i64), Named(String) }
impl Shape {
    fn grow(&mut self, k: i64) { match self { Shape::Circle(r) => { *r *= k; } Shape::Named(n) => { n.push('*'); } } }
    fn area(&self) -> i64 { match self { Shape::Circle(r) => *r, Shape::Named(n) => n.len() as i64 } }
}
fn main() {
    let mut shapes: Vec<Shape> = Vec::new();
    shapes.push(Shape::Circle(2));
    shapes.push(Shape::Named(String::from("n")));
    for s in shapes.iter_mut() { s.grow(2); }
    for s in &shapes { println!("{}", s.area()); }
}
