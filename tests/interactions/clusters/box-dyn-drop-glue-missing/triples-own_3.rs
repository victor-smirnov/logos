use std::rc::Rc; use std::collections::HashMap; use std::cell::RefCell;
trait Shape { fn area(&self) -> i64; }
struct Rect { w: i64 }
impl Shape for Rect { fn area(&self) -> i64 { return self.w; } }
impl Drop for Rect { fn drop(&mut self) { println!("drop rect {}", self.w); } }
struct Z { k: i64 }
impl Z { fn eat(&self, s: Box<dyn Shape>) -> i64 { return s.area() + self.k; } }
fn run() {
    let z = Z { k: 10 };
    println!("{}", z.eat(Box::new(Rect { w: 1 })));
    println!("end");
}
fn main() { run(); }
