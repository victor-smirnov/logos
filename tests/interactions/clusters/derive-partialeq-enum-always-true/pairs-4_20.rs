#[derive(PartialEq)]
enum Kind { Round, Poly(i64) }
fn main() { let k = Kind::Poly(3); println!("{} {} {}", k == Kind::Round, k == Kind::Poly(3), k == Kind::Poly(4)); }
