trait Named {
    fn tag() -> i64;
    fn full(&self) -> i64 { Self::tag() + 1 }
}
struct Rect<T> { w: T }
struct Sq { w: i32 }
impl Named for Sq { fn tag() -> i64 { 4 } }
impl<T> Named for Rect<T> { fn tag() -> i64 { 9 } }
fn main() {
    let r = Sq { w: 3i32 };
    println!("{} {}", r.full(), r.w);
}
