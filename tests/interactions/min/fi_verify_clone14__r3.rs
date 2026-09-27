#[derive(Clone)]
struct Pt { x: i64, y: i64 }
impl Pt { fn dup(&self) -> Pt { self.clone() } }
fn main() { let p = Pt { x: 1, y: 2 }; println!("{} {}", p.dup().y, p.x); }
