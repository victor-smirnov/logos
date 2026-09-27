use std::ops::Add;
struct V2<T> { x: T, y: T }
impl<T: Add<Output = T>> Add for V2<T> { type Output = V2<T>; fn add(self, o: V2<T>) -> V2<T> { V2 { x: self.x + o.x, y: self.y + o.y } } }
fn main() { let a = V2 { x: 1i64, y: 2i64 }; let b = V2 { x: 10i64, y: 20i64 }; let c = a + b; println!("{} {}", c.x, c.y); }
