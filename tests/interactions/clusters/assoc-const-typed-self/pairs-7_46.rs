trait Z { const ZERO: Self; }
#[derive(Clone, Copy)]
struct M(i64);
impl Z for M { const ZERO: M = M(0); }
fn main() { println!("{}", M::ZERO.0); }
