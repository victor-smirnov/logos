trait Area { fn area(&self) -> i64; }
struct Rect { w: i64 }
impl Area for Rect { fn area(&self) -> i64 { self.w } }
fn mk() -> impl Area { Rect { w: 7 } }
fn main() { let r = mk(); std::process::exit(r.w as i32); }
