use std::rc::Rc;
fn main() { let mut r = Rc::new(String::from("a")); let r2 = Rc::clone(&r); r.push_str("x"); println!("{} {}", r.len(), r2); }
