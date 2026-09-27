fn d(x: &u8) -> u8 { x.wrapping_mul(2) }
fn e(x: &i64) -> i64 { x.pow(2) + x.abs() }
fn main() { println!("{} {}", d(&200), e(&-3)); }
