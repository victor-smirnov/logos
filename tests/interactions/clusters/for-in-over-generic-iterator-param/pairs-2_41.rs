struct Counter { n: i64 }
impl Iterator for Counter { type Item = i64; fn next(&mut self) -> Option<i64> { if self.n < 5 { self.n += 1; Some(self.n) } else { None } } }
fn total_g<I: Iterator<Item = i64>>(it: I) -> i64 { let mut t: i64 = 0; for x in it { t += x; } return t; }
fn total(it: impl Iterator<Item = i64>) -> i64 { let mut t: i64 = 0; for x in it { t += x; } return t; }
fn main() { println!("{} {}", total_g(Counter { n: 2 }), total(Counter { n: 0 })); }
