use std::cell::RefCell;
fn main() { let c = RefCell::new(vec![10i64, 20]); let x = c.borrow().get(1).cloned(); println!("{:?}", x); let y = *c.borrow().get(0).unwrap(); println!("{}", y); }
