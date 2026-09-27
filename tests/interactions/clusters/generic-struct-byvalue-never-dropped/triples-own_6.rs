use std::rc::Rc; use std::collections::HashMap; use std::cell::RefCell;
struct Noisy { name: String } impl Drop for Noisy { fn drop(&mut self) { println!("drop {}", self.name); } } struct P<T> { n: Noisy, v: T } fn run() { let a: P<i64> = P { n: Noisy { name: String::from("inner") }, v: 1 }; drop(a); println!("end"); }
fn main() { run(); }
