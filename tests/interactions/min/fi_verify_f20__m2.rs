trait HasZero { const ZERO: Self; }
impl HasZero for i64 { const ZERO: i64 = 0; }
fn main() { println!("{}", i64::ZERO); }
