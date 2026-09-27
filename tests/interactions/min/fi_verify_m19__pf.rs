trait St { fn total(&self) -> i64; fn twice(&self) -> i64 { self.total() } }
impl<const N: usize> St for [i32; N] { fn total(&self) -> i64 { 1 } }
fn main() { let a = [5i32, 6, 7]; std::process::exit(a.twice() as i32) }
