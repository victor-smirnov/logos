trait Src { fn get(&mut self) -> Option<i64>; }
struct Seq { cur: i64, end: i64 }
impl Src for Seq { fn get(&mut self) -> Option<i64> { if self.cur < self.end { self.cur += 1; Some(self.cur) } else { None } } }
fn make(n: i64) -> impl Src { Seq { cur: 0, end: n } }
fn lookup(n: i64) -> Option<impl Src> { if n > 0 { Some(make(n)) } else { None } }
fn main() { if let Some(mut s) = lookup(4) { println!("found {:?}", s.get()); } }
