trait Container { type Item; fn first(&self) -> Self::Item; }
struct Bag { x: i64 }
impl Container for Bag { type Item = i64; fn first(&self) -> i64 { self.x } }
fn inline_b<C: Container<Item = i64>>(c: &C) -> i64 { c.first() * 2 }
fn main() { println!("{}", inline_b(&Bag { x: 5 })); }
