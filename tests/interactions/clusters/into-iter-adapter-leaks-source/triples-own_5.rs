use std::rc::Rc; use std::collections::HashMap; use std::cell::RefCell;
fn run() { let w: Vec<i64> = vec![1, 2, 3]; let n: Vec<i64> = w.into_iter().filter(|s| *s > 1).collect(); println!("{}", n.len()); }
fn main() { run(); }
