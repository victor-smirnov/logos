use std::cell::RefCell;
fn main() { let s = RefCell::new(Some(4)); let t = s.borrow_mut().take(); println!("{:?}", t); }
