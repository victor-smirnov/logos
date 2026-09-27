#[derive(Clone, Debug)]
struct Pt { x: i64, y: i64 }
struct L { a: Pt }
impl L { fn g(&self) -> Pt { self.a.clone() } }
fn main() { let l = L { a: Pt { x: 1, y: 2 } }; println!("{:?}", l.g()); }
