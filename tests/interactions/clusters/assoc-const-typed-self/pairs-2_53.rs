trait Node { type Val; const ZERO: Self::Val; }
struct N;
impl Node for N { type Val = i64; const ZERO: i64 = 5; }
fn main() { println!("{}", N::ZERO); }
