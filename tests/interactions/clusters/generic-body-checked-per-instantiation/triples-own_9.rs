use std::rc::Rc; use std::collections::HashMap; use std::cell::RefCell;
struct Pool<T> { items: Vec<T> }
impl<T> Pool<T> { fn dup(&self) -> Pool<T> { return Pool { items: self.items.clone() }; } }
fn run() { let p: Pool<String> = Pool { items: vec![String::from("a")] }; let q = p.dup(); println!("{}", q.items.len()); }
fn main() { run(); }
