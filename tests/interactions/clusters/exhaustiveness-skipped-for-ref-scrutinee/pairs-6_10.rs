enum Shape { Circle(i32), Sq(i32) }
fn f(s: &Shape) -> i32 { match s { Shape::Circle(r) => *r } }
fn main() { std::process::exit(f(&Shape::Sq(2))); }
