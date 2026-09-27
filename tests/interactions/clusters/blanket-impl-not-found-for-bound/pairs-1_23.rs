trait Shape { fn area(&self) -> i64; }
trait Named: Shape { fn tag(&self) -> i64 { self.area() + 1 } }
struct Rect<T> { w: T }
impl<T> Shape for Rect<T> { fn area(&self) -> i64 { 5 } }
impl Named for Rect<i32> {}
fn main() { let r = Rect { w: 1i32 }; println!("{} {}", r.tag(), r.w); }
