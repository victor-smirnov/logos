use std::rc::Rc;
fn main() { let mut r = Rc::new(vec![1]); let r2 = r.clone(); r.push(2); println!("{} {}", r.len(), r2.len()); }
