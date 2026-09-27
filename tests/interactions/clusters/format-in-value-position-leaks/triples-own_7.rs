use std::rc::Rc; use std::collections::HashMap; use std::cell::RefCell;
fn run() { let mk = |s: i64| -> String { return format!("n-{}", s); }; let t = mk(1); println!("{}", t); }
fn main() { run(); }
