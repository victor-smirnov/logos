trait Container { type Item; fn get(&self, i: i64) -> Self::Item; }
struct Flags { a: bool }
impl Container for Flags { type Item = bool; fn get(&self, _i: i64) -> bool { self.a } }
fn f1<C: Container<Item = bool>>(c: &C) -> i64 { if c.get(0) { 1 } else { 0 } }
fn f2<C>(c: &C) -> i64 where C: Container<Item = bool> { if c.get(0) { 1 } else { 0 } }
fn main() { let f = Flags { a: true }; println!("{} {}", f1(&f), f2(&f)); }
