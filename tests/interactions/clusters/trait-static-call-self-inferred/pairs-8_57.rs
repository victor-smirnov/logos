trait Make { fn make(v: i32) -> Self; }
enum G { A(i32), B }
impl Make for G { fn make(v: i32) -> Self { if v > 0 { G::A(v) } else { G::B } } }
fn main() { let g = Make::make(3); match g { G::A(x) => println!("{}", x), G::B => {} } }
