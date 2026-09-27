use std::rc::Rc; use std::collections::HashMap; use std::cell::RefCell;
trait Act { fn act(&mut self) -> i64; }
struct Counter { n: i64 }
impl Act for Counter { fn act(&mut self) -> i64 { self.n += 1; return self.n; } }
fn run() {
    let mut b: Box<dyn Act> = Box::new(Counter { n: 0 });
    let a: &mut Box<dyn Act> = &mut b;
    a.act();
    println!("{}", a.act());
}
fn main() { run(); }
