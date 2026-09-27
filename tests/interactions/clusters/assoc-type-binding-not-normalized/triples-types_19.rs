trait Container { type Item; fn first(&self) -> Self::Item; }
struct Bag { x: i64 }
impl<'a> Container for &'a Bag { type Item = &'a i64; fn first(&self) -> &'a i64 { &self.x } }
fn get<C: Container>(c: C) -> C::Item { c.first() }
fn main() { let b = Bag { x: 5 }; let r = get(&b); println!("{}", *r + 1); }
