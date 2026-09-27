fn rem(a: u64, m: u64) -> u64 { a % m }
fn id(a: u64) -> u64 { a }
fn main() { println!("{}", rem(1u64 << 62, u64::MAX)); println!("{}", id(1 << 40)); }
