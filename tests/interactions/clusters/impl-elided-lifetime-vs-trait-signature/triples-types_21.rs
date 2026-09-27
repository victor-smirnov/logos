use std::ops::Add;
struct V { x: i64 }
impl<'a> Add<&'a V> for &'a V { type Output = V; fn add(self, o: &V) -> V { V { x: self.x + o.x } } }
fn main() { let a = V { x: 1 }; let b = V { x: 2 }; let c = &a + &b; println!("{}", c.x); }
