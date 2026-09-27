#[derive(Debug)]
enum Kind { Round, Poly(i64) }
#[derive(Debug)]
struct P { a: i64, k: Kind }
fn main() { println!("{:?} {:?}", Kind::Poly(3), P { a: 1, k: Kind::Round }); }
