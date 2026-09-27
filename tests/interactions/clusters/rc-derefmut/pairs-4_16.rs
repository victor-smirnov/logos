use std::rc::Rc;
fn main() { let mut r = Rc::new(vec![1i64, 2]); let r2 = Rc::clone(&r); r.push(3); println!("{} {}", r.len(), r2.len()); }
