trait Named {
    fn tag() -> i64;
    fn full(&self) -> i64 { Self::tag() + 1 }
}
struct Rect<T> { w: T }
impl<T> Named for Rect<T> { fn tag() -> i64 { 9 } }
fn main() {
    let r = Rect { w: 3i32 };
    println!("{} {}", r.full(), r.w);
}
