trait Area { fn area(&self) -> i64; }
struct Rect { w: i64 }
impl Area for Rect { fn area(&self) -> i64 { self.w } }
fn mk() -> impl Area { Rect { w: 1 } }
fn main() { let r = mk(); println!("{}", r.w); }
