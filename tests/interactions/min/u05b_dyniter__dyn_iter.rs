struct Up { i: i64 }
impl Iterator for Up { type Item = i64; fn next(&mut self) -> Option<i64> { if self.i < 3 { self.i += 1; Some(self.i) } else { None } } }
fn drain(it: &mut dyn Iterator<Item = i64>) -> i64 { let mut s = 0i64; while let Some(x) = it.next() { s += x; } s }
fn main() { let mut u = Up { i: 0 }; println!("{}", drain(&mut u)); }
