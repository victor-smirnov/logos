use std::rc::Rc;
fn main() { let s = Rc::new(String::from("hi")); let b = Box::new(5i64); println!("{} {} {:?}", s, b, Rc::new(3i64)); }
