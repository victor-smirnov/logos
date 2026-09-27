use std::ops::{Add, Neg, Index, Sub};
#[derive(Clone, Copy, PartialEq, Debug)]
struct P { x: i64, y: i64 }
impl Add for P { type Output = P; fn add(self, o: P) -> P { P { x: self.x + o.x, y: self.y + o.y } } }
impl Sub for P { type Output = P; fn sub(self, o: P) -> P { P { x: self.x - o.x, y: self.y - o.y } } }
impl Neg for P { type Output = P; fn neg(self) -> P { P { x: -self.x, y: -self.y } } }
#[derive(Clone, Copy, Debug, PartialEq)]
enum Sh { Pt(P), Seg(P, P), Empty }
struct Path { pts: Vec<P> }
impl Index<usize> for Path { type Output = P; fn index(&self, i: usize) -> &P { &self.pts[i] } }
fn classify(p: P) -> &'static str {
    match p + P { x: 1, y: 1 } {
        P { x: 0, y: 0 } => "origin",
        P { x, y: 0 } if x > 0 => "pos-x-axis",
        P { x: 0, .. } => "y-axis",
        P { x: a @ 1..=5, y: b @ 1..=5 } if a == b => "diag-small",
        P { x, y } if x == -y => "anti",
        _ => "other",
    }
}
fn len2(s: Sh) -> i64 {
    match s {
        Sh::Pt(_) | Sh::Empty => 0,
        Sh::Seg(a, b) => { let d = b - a; match (d.x, d.y) { (0, dy) => dy * dy, (dx, 0) => dx * dx, (dx, dy) => dx * dx + dy * dy } }
    }
}
fn main() {
    let pts = [P { x: -1, y: -1 }, P { x: 3, y: -1 }, P { x: -1, y: 4 }, P { x: 2, y: 2 }, P { x: 4, y: -6 }, P { x: 9, y: 0 }];
    for p in pts.iter() { println!("{}", classify(*p)); }
    let path = Path { pts: vec![P { x: 0, y: 0 }, P { x: 3, y: 4 }, P { x: 3, y: 0 }] };
    let shapes = vec![Sh::Seg(path[0], path[1]), Sh::Seg(path[1], path[2]), Sh::Pt(-path[1]), Sh::Empty];
    let mut t: i64 = 0;
    for s in shapes.iter() { t += len2(*s); }
    println!("{}", t);
    match -path[1] { P { x: -3, y } => println!("neg y={}", y), _ => println!("no") }
    if let Sh::Pt(P { x, .. }) = shapes[2] { println!("pt x {}", x); }
    let q = path[1] + path[2];
    let r = match q == P { x: 6, y: 4 } { true => "eq", false => "ne" };
    println!("{} {:?}", r, q);
    match shapes[0] { Sh::Seg(a, b) if a + b == path[1] => println!("sum matches"), _ => println!("nope") }
}
