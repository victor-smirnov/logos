trait St { fn total(&self) -> i64; fn twice(&self) -> i64 { self.total() * 2 } }
impl<const N: usize> St for [i32; N] { fn total(&self) -> i64 { N as i64 } }
fn main() { let a = [5i32, 6, 7]; println!("{}", a.twice()); }
