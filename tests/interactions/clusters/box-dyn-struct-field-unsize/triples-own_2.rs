trait Shape { fn area(&self) -> i64; }
struct Rect { w: i64 }
impl Shape for Rect { fn area(&self) -> i64 { return self.w; } }
struct Slot { cur: Box<dyn Shape> }
fn main() { let slot = Slot { cur: Box::new(Rect { w: 5 }) }; std::process::exit(slot.cur.area() as i32); }
