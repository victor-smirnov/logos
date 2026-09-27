trait Sh { fn area(&self) -> i64; }
struct Sq(i64);
impl Sh for Sq { fn area(&self) -> i64 { return self.0; } }
struct Gen { i: i64 }
impl Iterator for Gen { type Item = Box<dyn Sh>;
    fn next(&mut self) -> Option<Box<dyn Sh>> { if self.i >= 3 { return None; } self.i += 1; return Some(Box::new(Sq(self.i))); }
}
fn main() {
    let mut g = Gen { i: 0 };
    while let Some(s) = g.next() { println!("{}", s.area()); }
}
