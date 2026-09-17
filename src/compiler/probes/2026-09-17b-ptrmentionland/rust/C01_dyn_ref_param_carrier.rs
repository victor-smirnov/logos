// twin of hand/C01 — LEGAL
trait Shape { fn area(&self) -> i64; }
struct Sq { s: i64 }
impl Shape for Sq { fn area(&self) -> i64 { self.s * self.s } }
fn take<'a>(d: &'a dyn Shape) -> i64 { d.area() }
fn main() { let q = Sq { s: 3 }; std::process::exit((take(&q) - 9) as i32); }
